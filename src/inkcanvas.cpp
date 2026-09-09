#include "inkcanvas.h"
#include "ink.h"
#include "palette.h"

#include <QCursor>
#include <QHoverEvent>
#include <QMatrix4x4>
#include <QPixmap>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGOpacityNode>
#include <QSGSimpleRectNode>
#include <QSGTransformNode>
#include <QInputDevice>
#include <QLineF>
#include <QMouseEvent>
#include <QSet>
#include <QPointerEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QWindow>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
// Keep the pen raw: only a sample that did not move at all is dropped.
// (Was 0.7 px until 8 Sep 2026; libinput smoothing is off via a quirk too.)
constexpr float kMinStep = 0.05f;
constexpr float kEraserRadius = 18.f;
constexpr qreal kAngleStep = 15.0;
}

InkCanvas::InkCanvas(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
    setAcceptTouchEvents(true);
    setKeepTouchGrab(true);
    setAcceptHoverEvents(true);
    QPixmap blank(1, 1);
    blank.fill(Qt::transparent);
    setCursor(QCursor(blank, 0, 0));
    m_clock.start();

    m_gridHold.setSingleShot(true);
    m_gridHold.setInterval(650);
    m_gridFade.setDuration(400);
    m_gridFade.setStartValue(1.0);
    m_gridFade.setEndValue(0.0);
    m_gridFade.setEasingCurve(QEasingCurve::OutQuad);
    connect(&m_gridHold, &QTimer::timeout, &m_gridFade, [this]() { m_gridFade.start(); });
    connect(&m_gridFade, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_gridOpacity = v.toReal();
        update();
    });
}

void InkCanvas::revealGrid()
{
    m_gridFade.stop();
    m_gridOpacity = 1.0;
    m_gridHold.start();
    update();
}

void InkCanvas::setDocument(Document *document)
{
    if (m_document == document)
        return;
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (m_document) {
        connect(m_document, &Document::contentsChanged, this, [this]() {
            m_strokesDirty = true;
            update();
        });
        connect(m_document, &Document::selectionChanged, this, [this]() { update(); });
        connect(m_document, &Document::geometryChanged, this, [this]() {
            emit documentHeightChanged();
            clampView();
            update();
        });
    }
    m_viewY = 0;
    m_liveActive = false;
    m_lasso.clear();
    m_strokesDirty = true;
    emit documentChanged();
    emit viewYChanged();
    emit documentHeightChanged();
    update();
}

void InkCanvas::setTool(const QString &tool)
{
    if (m_tool == tool)
        return;
    m_tool = tool;
    if (m_document && m_tool != QStringLiteral("select"))
        m_document->clearSelection();
    emit toolChanged();
    update();
}

void InkCanvas::setColorId(const QString &id)
{
    const QString next = canonicalizeColorId(id);
    if (m_colorId == next)
        return;
    m_colorId = next;
    emit colorIdChanged();
    update();
}

void InkCanvas::setInkWidth(qreal width)
{
    width = std::clamp(width, 0.6, 16.0);
    if (qFuzzyCompare(m_inkWidth, width))
        return;
    m_inkWidth = width;
    emit inkWidthChanged();
    update();
}

void InkCanvas::setViewY(qreal y)
{
    const qreal next = std::max(0.0, y);
    if (qFuzzyCompare(m_viewY, next))
        return;
    m_viewY = next;
    clampView();
    emit viewYChanged();
    update();
}

qreal InkCanvas::documentHeight() const
{
    const qreal content = m_document ? m_document->contentHeight() : 1400;
    return std::max(content, height());
}

void InkCanvas::setPaperColor(const QColor &color)
{
    if (m_paperColor == color)
        return;
    m_paperColor = color;
    emit paperColorChanged();
    update();
}

void InkCanvas::setGridColor(const QColor &color)
{
    if (m_gridColor == color)
        return;
    m_gridColor = color;
    emit gridColorChanged();
    update();
}

void InkCanvas::setDarkMode(bool dark)
{
    if (m_darkMode == dark)
        return;
    m_darkMode = dark;
    ++m_paletteEpoch;
    m_strokesDirty = true;
    emit darkModeChanged();
    update();
}

namespace {
constexpr qreal kGridStep = 28;
constexpr qreal kGridDot = 1.3;
constexpr qreal kGridChunk = 1000;
constexpr int kRingSegments = 28;

// Root of the canvas scene: paper in item space, everything else in document
// space under one translate node so a scroll is a matrix change, not a repaint.
struct CanvasNode : public QSGNode {
    QSGSimpleRectNode *paper = nullptr;
    QSGTransformNode *xform = nullptr;
    QSGOpacityNode *gridWrap = nullptr;
    QSGGeometryNode *grid = nullptr;
    QSGNode *strokes = nullptr;
    QSGOpacityNode *liveWrap = nullptr;
    QSGGeometryNode *live = nullptr;
    QSGOpacityNode *lassoWrap = nullptr;
    QSGGeometryNode *lasso = nullptr;
    QSGOpacityNode *selectionWrap = nullptr;
    QSGGeometryNode *selection = nullptr;
    QSGOpacityNode *cursorWrap = nullptr;
    QSGGeometryNode *cursorDot = nullptr;
    QSGGeometryNode *cursorRing = nullptr;
    qreal viewY = -1;
};

QSGGeometryNode *makeGeometryNode(QSGGeometry::DrawingMode mode, const QColor &color)
{
    auto *node = new QSGGeometryNode;
    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    geometry->setDrawingMode(mode);
    geometry->setLineWidth(1);
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    auto *material = new QSGFlatColorMaterial;
    material->setColor(color);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

void setNodeColor(QSGGeometryNode *node, const QColor &color)
{
    auto *material = static_cast<QSGFlatColorMaterial *>(node->material());
    if (material->color() == color)
        return;
    material->setColor(color);
    node->markDirty(QSGNode::DirtyMaterial);
}

// An empty geometry is replaced by one degenerate primitive so the renderer
// never sees a zero-vertex node.
void setNodePoints(QSGGeometryNode *node, const QVector<QPointF> &points)
{
    QSGGeometry *geometry = node->geometry();
    const int count = std::max(3, int(points.size()));
    geometry->allocate(count);
    QSGGeometry::Point2D *v = geometry->vertexDataAsPoint2D();
    for (int i = 0; i < count; ++i) {
        const QPointF p = points.isEmpty() ? QPointF(-10, -10) : points[std::min(i, int(points.size()) - 1)];
        v[i].set(float(p.x()), float(p.y()));
    }
    node->markDirty(QSGNode::DirtyGeometry);
}

QSGOpacityNode *wrap(QSGGeometryNode *child)
{
    auto *node = new QSGOpacityNode;
    node->appendChildNode(child);
    node->setOpacity(0);
    return node;
}

void ringPoints(QPointF c, qreal r, QVector<QPointF> &out)
{
    for (int k = 0; k <= kRingSegments; ++k) {
        const qreal a = 2 * M_PI * k / kRingSegments;
        out.append(c + QPointF(std::cos(a) * r, std::sin(a) * r));
    }
}

void discTriangles(QPointF c, qreal r, QVector<QPointF> &out)
{
    for (int k = 0; k < kRingSegments; ++k) {
        const qreal a0 = 2 * M_PI * k / kRingSegments;
        const qreal a1 = 2 * M_PI * (k + 1) / kRingSegments;
        out.append(c);
        out.append(c + QPointF(std::cos(a0) * r, std::sin(a0) * r));
        out.append(c + QPointF(std::cos(a1) * r, std::sin(a1) * r));
    }
}
}

QSGNode *InkCanvas::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *root = static_cast<CanvasNode *>(oldNode);
    if (!root) {
        // A fresh tree: every cached stroke node went away with the old one.
        m_strokeNodes.clear();
        m_nodesDocument = nullptr;
        m_gridWidth = 0;
        root = new CanvasNode;
        root->paper = new QSGSimpleRectNode(QRectF(), m_paperColor);
        root->appendChildNode(root->paper);
        root->xform = new QSGTransformNode;
        root->appendChildNode(root->xform);
        root->grid = makeGeometryNode(QSGGeometry::DrawTriangles, m_gridColor);
        root->gridWrap = wrap(root->grid);
        root->xform->appendChildNode(root->gridWrap);
        root->strokes = new QSGNode;
        root->xform->appendChildNode(root->strokes);
        root->live = makeGeometryNode(QSGGeometry::DrawTriangles, Qt::black);
        root->liveWrap = wrap(root->live);
        root->xform->appendChildNode(root->liveWrap);
        root->lasso = makeGeometryNode(QSGGeometry::DrawLineStrip, Qt::black);
        root->lassoWrap = wrap(root->lasso);
        root->xform->appendChildNode(root->lassoWrap);
        root->selection = makeGeometryNode(QSGGeometry::DrawLineStrip, Qt::black);
        root->selectionWrap = wrap(root->selection);
        root->xform->appendChildNode(root->selectionWrap);
        root->cursorDot = makeGeometryNode(QSGGeometry::DrawTriangles, Qt::black);
        root->cursorRing = makeGeometryNode(QSGGeometry::DrawLineStrip, Qt::black);
        root->cursorWrap = new QSGOpacityNode;
        root->cursorWrap->appendChildNode(root->cursorDot);
        root->cursorWrap->appendChildNode(root->cursorRing);
        root->cursorWrap->setOpacity(0);
        root->xform->appendChildNode(root->cursorWrap);
    }

    const QRectF paperRect(0, 0, width(), height());
    if (root->paper->rect() != paperRect)
        root->paper->setRect(paperRect);
    if (root->paper->color() != m_paperColor)
        root->paper->setColor(m_paperColor);

    if (!qFuzzyCompare(root->viewY, m_viewY)) {
        QMatrix4x4 m;
        m.translate(0, float(-m_viewY));
        root->xform->setMatrix(m);
        root->viewY = m_viewY;
    }

    syncGrid(root->grid);
    if (!qFuzzyCompare(root->gridWrap->opacity(), m_gridOpacity))
        root->gridWrap->setOpacity(m_gridOpacity);
    syncStrokes(root->strokes);

    // Live stroke: rebuilt while the pen is down, hidden otherwise.
    if (m_liveActive && !m_live.points.isEmpty()) {
        QVector<QPointF> tris;
        appendStrokeTriangles(m_live, tris);
        setNodePoints(root->live, tris);
        setNodeColor(root->live, strokePaintColor(m_live));
        root->liveWrap->setOpacity(1);
    } else if (root->liveWrap->opacity() > 0) {
        root->liveWrap->setOpacity(0);
    }

    if (m_lasso.size() >= 2) {
        setNodePoints(root->lasso, m_lasso);
        setNodeColor(root->lasso, m_darkMode ? QColor(220, 220, 220, 180) : QColor(40, 40, 40, 160));
        root->lassoWrap->setOpacity(1);
    } else if (root->lassoWrap->opacity() > 0) {
        root->lassoWrap->setOpacity(0);
    }

    if (m_document && m_document->selectedCount() > 0) {
        const QRectF r = m_document->selectionBounds().adjusted(-6, -6, 6, 6);
        const QVector<QPointF> box{r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft(), r.topLeft()};
        setNodePoints(root->selection, box);
        setNodeColor(root->selection, m_darkMode ? QColor(QStringLiteral("#5584aa"))
                                                 : QColor(QStringLiteral("#2077b2")));
        root->selectionWrap->setOpacity(1);
    } else if (root->selectionWrap->opacity() > 0) {
        root->selectionWrap->setOpacity(0);
    }

    if (m_hovering || m_liveActive) {
        syncCursor(root->cursorDot, root->cursorRing);
        root->cursorWrap->setOpacity(1);
    } else if (root->cursorWrap->opacity() > 0) {
        root->cursorWrap->setOpacity(0);
    }

    return root;
}

void InkCanvas::syncGrid(QSGGeometryNode *node)
{
    const qreal wanted = std::max(documentHeight(), m_viewY + height()) + kGridStep;
    const qreal bottom = std::ceil(wanted / kGridChunk) * kGridChunk;
    if (qFuzzyCompare(m_gridWidth, width()) && qFuzzyCompare(m_gridBottom, bottom)
        && m_gridBuilt == m_gridColor)
        return;
    m_gridWidth = width();
    m_gridBottom = bottom;
    m_gridBuilt = m_gridColor;

    QVector<QPointF> tris;
    const int cols = int(std::max(0.0, (width() - kGridStep) / kGridStep)) + 1;
    const int rows = int(bottom / kGridStep) + 1;
    tris.reserve(cols * rows * 6);
    const qreal h = kGridDot / 2;
    for (int r = 0; r < rows; ++r) {
        const qreal y = r * kGridStep;
        for (qreal x = kGridStep; x < width(); x += kGridStep) {
            tris.append({x - h, y - h});
            tris.append({x + h, y - h});
            tris.append({x - h, y + h});
            tris.append({x + h, y - h});
            tris.append({x + h, y + h});
            tris.append({x - h, y + h});
        }
    }
    setNodePoints(node, tris);
    setNodeColor(node, m_gridColor);
}

void InkCanvas::syncStrokes(QSGNode *parent)
{
    if (!m_document) {
        if (parent->childCount() > 0) {
            while (QSGNode *child = parent->firstChild()) {
                parent->removeChildNode(child);
                delete child;
            }
        }
        m_strokeNodes.clear();
        m_nodesDocument = nullptr;
        return;
    }
    if (m_nodesDocument != m_document) {
        while (QSGNode *child = parent->firstChild()) {
            parent->removeChildNode(child);
            delete child;
        }
        m_strokeNodes.clear();
        m_nodesDocument = m_document;
        m_strokesDirty = true;
    }
    if (!m_strokesDirty && m_document->strokeCount() == m_strokeNodes.size())
        return;
    m_strokesDirty = false;

    const QVector<Stroke> &strokes = m_document->strokes();
    QSet<QString> alive;
    alive.reserve(strokes.size());
    for (const Stroke &s : strokes)
        alive.insert(s.id);

    // Drop nodes whose stroke is gone.
    for (auto it = m_strokeNodes.begin(); it != m_strokeNodes.end();) {
        if (alive.contains(it.key())) {
            ++it;
            continue;
        }
        parent->removeChildNode(it->node);
        delete it->node;
        it = m_strokeNodes.erase(it);
    }

    // Walk in document order; re-append so z-order follows the document.
    while (QSGNode *child = parent->firstChild())
        parent->removeChildNode(child);
    for (const Stroke &s : strokes) {
        StrokeNode &entry = m_strokeNodes[s.id];
        const bool fresh = entry.node == nullptr;
        const bool stale = fresh || entry.points != s.points.size() || entry.bounds != s.bounds
            || entry.colorId != s.colorId || entry.epoch != m_paletteEpoch;
        if (fresh)
            entry.node = makeGeometryNode(QSGGeometry::DrawTriangles, strokePaintColor(s));
        if (stale) {
            QVector<QPointF> tris;
            appendStrokeTriangles(s, tris);
            setNodePoints(entry.node, tris);
            setNodeColor(entry.node, strokePaintColor(s));
            entry.points = s.points.size();
            entry.bounds = s.bounds;
            entry.colorId = s.colorId;
            entry.epoch = m_paletteEpoch;
        }
        parent->appendChildNode(entry.node);
    }
}

void InkCanvas::syncCursor(QSGGeometryNode *dot, QSGGeometryNode *ring)
{
    QVector<QPointF> dotPts;
    QVector<QPointF> ringPts;
    const QPointF p = m_hoverDoc;
    if (wantErase()) {
        ringPoints(p, kEraserRadius, ringPts);
        setNodeColor(ring, m_darkMode ? QColor(255, 255, 255, 160) : QColor(0, 0, 0, 140));
    } else if (m_tool == QStringLiteral("select")) {
        ringPoints(p, 5, ringPts);
        setNodeColor(ring, m_darkMode ? QColor(255, 255, 255, 160) : QColor(0, 0, 0, 140));
    } else {
        discTriangles(p, m_inkWidth * 0.5, dotPts);
        setNodeColor(dot, resolveColor(m_colorId, m_darkMode));
    }
    setNodePoints(dot, dotPts);
    setNodePoints(ring, ringPts);
}

void InkCanvas::invalidateStrokeNodes()
{
    m_strokesDirty = true;
    update();
}

void InkCanvas::zoomReset()
{
    setViewY(0);
}

void InkCanvas::scrollBy(qreal dy)
{
    setViewY(m_viewY + dy);
}

namespace {
struct PointerInfo {
    InkCanvas::Pointer kind = InkCanvas::Pointer::Mouse;
    float pressure = 0.85f;
    bool eraser = false;
};

PointerInfo inspectPointer(const QPointerEvent *event)
{
    PointerInfo info;
    if (!event || event->points().isEmpty())
        return info;
    const QEventPoint &pt = event->points().first();
    if (pt.pressure() > 0.01)
        info.pressure = float(pt.pressure());
    const QPointingDevice *dev = pt.device();
    if (!dev)
        return info;
    static bool logged = false;
    if (!logged) {
        logged = true;
        qInfo() << "omascribe input device:" << dev->name()
                << "type" << dev->type()
                << "pointer" << dev->pointerType()
                << "pressure" << pt.pressure();
    }
    switch (dev->pointerType()) {
    case QPointingDevice::PointerType::Pen:
        info.kind = InkCanvas::Pointer::Pen;
        break;
    case QPointingDevice::PointerType::Eraser:
        info.kind = InkCanvas::Pointer::Pen;
        info.eraser = true;
        break;
    case QPointingDevice::PointerType::Finger:
        info.kind = InkCanvas::Pointer::Finger;
        break;
    default:
        if (dev->type() == QInputDevice::DeviceType::Stylus)
            info.kind = InkCanvas::Pointer::Pen;
        else if (dev->type() == QInputDevice::DeviceType::TouchScreen)
            info.kind = InkCanvas::Pointer::Finger;
        break;
    }
    return info;
}
}

void InkCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastLocal = event->position();
        event->accept();
        return;
    }
    const PointerInfo info = inspectPointer(event);
    pointerDown(event->position(), info.pressure, info.kind, info.eraser);
    event->accept();
}

void InkCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPointF delta = event->position() - m_lastLocal;
        m_lastLocal = event->position();
        revealGrid();
        setViewY(m_viewY - delta.y());
        event->accept();
        return;
    }
    const PointerInfo info = inspectPointer(event);
    pointerMove(event->position(), info.pressure, info.kind);
    event->accept();
}

void InkCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning && event->button() == Qt::MiddleButton) {
        m_panning = false;
        event->accept();
        return;
    }
    pointerUp(event->position(), Pointer::Mouse);
    event->accept();
}

void InkCanvas::wheelEvent(QWheelEvent *event)
{
    qreal dy = 0;
    if (!event->pixelDelta().isNull())
        dy = -event->pixelDelta().y();
    else
        dy = -event->angleDelta().y() * 0.8;
    revealGrid();
    setViewY(m_viewY + dy);
    event->accept();
}

// The pen owns the page while it is down or hovering: a palm that lands
// then is ignored. Before 9 Sep 2026 a palm that touched first started a
// finger pan the pen never cancelled, so the next stroke became a dot and
// dragged the page instead.
bool InkCanvas::penNear() const
{
    return m_penNear && m_penClock.isValid() && m_penClock.elapsed() < 1500;
}

void InkCanvas::cancelFingerPan()
{
    if (m_panning && m_activePointer == Pointer::Finger) {
        m_panning = false;
        m_activePointer = Pointer::None;
    }
}

void InkCanvas::touchEvent(QTouchEvent *event)
{
    event->accept();
    if (m_penDown || penNear()) {
        cancelFingerPan();
        m_touchMaxFingers = 0;
        m_touchMoved = false;
        return;
    }

    int down = 0;
    qreal travel = 0;
    QPointF centroid;
    for (const QEventPoint &pt : event->points()) {
        travel = std::max(travel, QLineF(pt.pressPosition(), pt.position()).length());
        if (pt.state() == QEventPoint::Released)
            continue;
        centroid += pt.position();
        ++down;
    }
    if (down > 0)
        centroid /= down;
    if (travel > 32)
        m_touchMoved = true;

    if (event->type() == QEvent::TouchBegin) {
        m_touchMaxFingers = std::max(1, down);
        m_touchMoved = travel > 32;
        m_touchCentroid = centroid;
        m_touchClock.restart();
        if (down == 1)
            pointerDown(event->points().first().position(), 0.8f, Pointer::Finger, false);
        return;
    }

    if (event->type() == QEvent::TouchCancel) {
        if (m_panning)
            pointerUp(m_lastLocal, Pointer::Finger);
        m_touchMaxFingers = 0;
        m_touchMoved = false;
        m_panning = false;
        return;
    }

    m_touchMaxFingers = std::max(m_touchMaxFingers, down);

    if (event->type() == QEvent::TouchUpdate) {
        if (m_touchMaxFingers >= 2 && m_panning) {
            m_panning = false;
            m_activePointer = Pointer::None;
        }
        if (down >= 2) {
            if (!m_touchCentroid.isNull() && m_touchMoved) {
                revealGrid();
                setViewY(m_viewY - (centroid.y() - m_touchCentroid.y()));
            }
            m_touchCentroid = centroid;
            return;
        }
        if (down == 1 && m_touchMaxFingers == 1)
            pointerMove(event->points().first().position(), 0.8f, Pointer::Finger);
        return;
    }

    // TouchEnd: last finger lifted. Two-finger tap = undo, three-finger tap = redo.
    if (m_touchMaxFingers >= 2 && !m_touchMoved && m_touchClock.isValid()
        && m_touchClock.elapsed() < 500 && m_document) {
        if (m_touchMaxFingers == 2)
            m_document->undo();
        else
            m_document->redo();
    } else if (m_touchMaxFingers <= 1) {
        const QPointF pos = event->points().isEmpty()
            ? m_lastLocal
            : event->points().first().position();
        pointerUp(pos, Pointer::Finger);
    } else {
        m_panning = false;
        m_activePointer = Pointer::None;
    }
    m_touchMaxFingers = 0;
    m_touchMoved = false;
    m_touchCentroid = QPointF();
}

void InkCanvas::hoverMoveEvent(QHoverEvent *event)
{
    m_hovering = true;
    m_hoverDoc = toDoc(event->position());
    update();
}

void InkCanvas::hoverLeaveEvent(QHoverEvent *event)
{
    Q_UNUSED(event);
    m_hovering = false;
    update();
}

bool InkCanvas::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
    case QEvent::TabletEnterProximity:
    case QEvent::TabletLeaveProximity:
        handleTablet(static_cast<QTabletEvent *>(event));
        return true;
    default:
        return QQuickItem::event(event);
    }
}

void InkCanvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    emit documentHeightChanged();
    clampView();
}

void InkCanvas::applyStylusButtons(const QTabletEvent *event)
{
    m_hwEraser = event->pointerType() == QPointingDevice::PointerType::Eraser;
    const Qt::MouseButtons buttons = event->buttons();
    const bool upper = buttons.testFlag(Qt::RightButton) || event->button() == Qt::RightButton;
    const bool lower = buttons.testFlag(Qt::MiddleButton) || event->button() == Qt::MiddleButton;
    if (event->type() == QEvent::TabletPress) {
        if (upper && !buttons.testFlag(Qt::LeftButton)) {
            m_upperDown = true;
            m_upperMoved = false;
        }
        if ((lower || m_hwEraser) && !buttons.testFlag(Qt::LeftButton)) {
            m_lowerDown = true;
            m_lowerMoved = false;
        }
    }
}

bool InkCanvas::wantErase() const
{
    return m_hwEraser || m_lowerDown || m_tool == QStringLiteral("eraser");
}

void InkCanvas::toggleEraserTool()
{
    setTool(m_tool == QStringLiteral("eraser") ? QStringLiteral("pen") : QStringLiteral("eraser"));
}

void InkCanvas::handleTablet(QTabletEvent *event)
{
    const QPointF local = mapFromScene(event->scenePosition());
    m_penClock.restart();
    m_penNear = event->type() != QEvent::TabletLeaveProximity;
    cancelFingerPan();
    applyStylusButtons(event);
    float pressure = float(event->pressure());
    const Qt::MouseButtons buttons = event->buttons();
    const bool tip = buttons.testFlag(Qt::LeftButton) || event->button() == Qt::LeftButton;

    if (event->type() == QEvent::TabletEnterProximity
        || event->type() == QEvent::TabletLeaveProximity) {
        m_hovering = event->type() == QEvent::TabletEnterProximity;
        m_hoverDoc = toDoc(local);
        // Framework lower button: tool type flips to eraser without a mouse button.
        if (event->type() == QEvent::TabletEnterProximity && m_hwEraser && !m_penDown) {
            m_lowerDown = true;
            m_lowerMoved = false;
            m_lastLocal = local;
            m_clock.restart();
        } else if (event->type() == QEvent::TabletEnterProximity && !m_hwEraser
                   && m_lowerDown && !m_penDown && !m_lowerMoved && m_clock.elapsed() < 450) {
            toggleEraserTool();
            m_lowerDown = false;
        }
        if (event->type() == QEvent::TabletLeaveProximity)
            m_upperDown = false;
        update();
        event->accept();
        return;
    }

    if (event->type() == QEvent::TabletPress) {
        // Upper barrel: pan. Framework default is right-click; here it scrolls the page.
        if (m_upperDown && !tip) {
            m_panning = true;
            m_lastLocal = local;
            event->accept();
            return;
        }
        // Lower barrel / eraser tool-type: hover press is a tap candidate.
        if (m_lowerDown && !tip) {
            event->accept();
            return;
        }
        m_penDown = true;
        if (m_upperDown) {
            m_panning = true;
            m_lastLocal = local;
            event->accept();
            return;
        }
        pointerDown(local, pressure, Pointer::Pen, wantErase());
    } else if (event->type() == QEvent::TabletMove) {
        m_hovering = true;
        m_hoverDoc = toDoc(local);
        if (m_upperDown && (local - m_lastLocal).manhattanLength() > 4)
            m_upperMoved = true;
        if (m_lowerDown && (local - m_lastLocal).manhattanLength() > 8)
            m_lowerMoved = true;

        if (m_panning || m_upperDown) {
            if (m_liveActive)
                endStroke();
            if (!m_panning) {
                m_panning = true;
                m_lastLocal = local;
            }
            const QPointF delta = local - m_lastLocal;
            m_lastLocal = local;
            if (delta.manhattanLength() > 1)
                m_upperMoved = true;
            revealGrid();
            setViewY(m_viewY - delta.y());
            event->accept();
            return;
        }

        if (tip || m_penDown)
            pointerMove(local, pressure, Pointer::Pen);
        else
            update();
    } else if (event->type() == QEvent::TabletRelease) {
        if (event->button() == Qt::RightButton) {
            m_panning = false;
            m_upperDown = false;
            event->accept();
            return;
        }
        if (event->button() == Qt::MiddleButton || (!tip && m_hwEraser && !m_penDown)) {
            if (m_lowerDown && !m_lowerMoved && !m_penDown)
                toggleEraserTool();
            m_lowerDown = false;
            m_lowerMoved = false;
            event->accept();
            return;
        }
        pointerUp(local, Pointer::Pen);
        m_penDown = false;
        m_panning = false;
    }
    event->accept();
}

void InkCanvas::pointerDown(QPointF local, float pressure, Pointer pointer, bool eraserTip)
{
    emit engaged();
    m_activePointer = pointer;
    m_lastLocal = local;
    m_pressDoc = toDoc(local);
    const bool fingerPan = pointer == Pointer::Finger;
    if (fingerPan) {
        m_panning = true;
        return;
    }

    QString tool = m_tool;
    if (eraserTip)
        tool = QStringLiteral("eraser");

    if (tool == QStringLiteral("eraser")) {
        if (m_document) {
            m_document->beginErase();
            m_document->eraseAt(m_pressDoc, kEraserRadius);
        }
        invalidateStrokeNodes();
        return;
    }

    if (tool == QStringLiteral("select")) {
        if (selectionContains(m_pressDoc)) {
            m_movingSelection = true;
            if (m_document)
                m_document->beginTranslate();
        } else {
            m_lassoing = true;
            m_lasso = {m_pressDoc};
            if (m_document)
                m_document->clearSelection();
        }
        update();
        return;
    }

    beginStroke(m_pressDoc, effectivePressure(pressure));
}

void InkCanvas::pointerMove(QPointF local, float pressure, Pointer pointer)
{
    Q_UNUSED(pointer);
    if (m_panning) {
        const QPointF delta = local - m_lastLocal;
        m_lastLocal = local;
        revealGrid();
        setViewY(m_viewY - delta.y());
        return;
    }
    const QPointF doc = toDoc(local);
    m_hoverDoc = doc;
    m_hovering = true;

    if (m_document && wantErase() && !m_liveActive && !m_lassoing
        && !m_movingSelection && (m_activePointer == Pointer::Pen || m_activePointer == Pointer::Mouse)) {
        m_document->eraseAt(doc, kEraserRadius);
        invalidateStrokeNodes();
        return;
    }

    if (m_movingSelection && m_document) {
        const QPointF delta = doc - m_pressDoc;
        m_pressDoc = doc;
        m_document->translateSelection(delta);
        return;
    }

    if (m_lassoing) {
        if (m_lasso.isEmpty() || QLineF(m_lasso.last(), doc).length() >= 1.5)
            m_lasso.append(doc);
        update();
        return;
    }

    if (m_liveActive)
        extendStroke(doc, effectivePressure(pressure));
}

void InkCanvas::pointerUp(QPointF local, Pointer pointer)
{
    Q_UNUSED(pointer);
    const QPointF doc = toDoc(local);
    if (m_panning) {
        m_panning = false;
        m_activePointer = Pointer::None;
        return;
    }
    if (m_movingSelection) {
        if (m_document)
            m_document->endTranslate();
        m_movingSelection = false;
        m_activePointer = Pointer::None;
        return;
    }
    if (m_lassoing) {
        m_lasso.append(doc);
        if (m_document && m_lasso.size() >= 3)
            m_document->selectLasso(m_lasso);
        m_lasso.clear();
        m_lassoing = false;
        m_activePointer = Pointer::None;
        update();
        return;
    }
    if (m_tool == QStringLiteral("eraser") || m_activePointer != Pointer::None) {
        if (m_document)
            m_document->endErase();
    }
    if (m_liveActive)
        endStroke();
    m_activePointer = Pointer::None;
}

QPointF InkCanvas::toDoc(QPointF local) const
{
    return {local.x(), local.y() + m_viewY};
}

void InkCanvas::beginStroke(QPointF doc, float pressure)
{
    m_live = Stroke();
    m_live.tool = QStringLiteral("fineliner");
    m_live.colorId = m_colorId;
    m_live.width = float(m_inkWidth);
    m_live.points.append(InkPoint{float(doc.x()), float(doc.y()), pressure});
    m_liveActive = true;
    m_pressDoc = doc;
    update();
}

void InkCanvas::extendStroke(QPointF doc, float pressure)
{
    if (!m_liveActive)
        return;
    if (m_tool == QStringLiteral("ruler"))
        doc = rulerPoint(m_pressDoc, doc);

    if (m_tool == QStringLiteral("ruler")) {
        m_live.points = {
            InkPoint{float(m_pressDoc.x()), float(m_pressDoc.y()), pressure},
            InkPoint{float(doc.x()), float(doc.y()), pressure},
        };
        m_live.recomputeBounds();
        autoGrowAndFollow(doc);
        update();
        return;
    }

    if (!m_live.points.isEmpty()) {
        const InkPoint &last = m_live.points.last();
        const float d = std::hypot(doc.x() - last.x, doc.y() - last.y);
        if (d < kMinStep)
            return;
    }
    m_live.points.append(InkPoint{float(doc.x()), float(doc.y()), pressure});
    m_live.recomputeBounds();
    autoGrowAndFollow(doc);
    update();
}

void InkCanvas::endStroke()
{
    if (!m_liveActive)
        return;
    m_liveActive = false;
    if (m_live.points.size() == 1) {
        InkPoint extra = m_live.points.first();
        extra.x += 0.15f;
        m_live.points.append(extra);
    }
    m_live.recomputeBounds();
    if (m_document)
        m_document->addStroke(m_live);
    m_live = Stroke();
    emit drawingChanged();
    update();
}

QColor InkCanvas::strokePaintColor(const Stroke &stroke) const
{
    return resolveColor(stroke.colorId, m_darkMode);
}

void InkCanvas::clampView()
{
    const qreal maxY = std::max(0.0, documentHeight() - height());
    const qreal clamped = std::clamp(m_viewY, 0.0, maxY);
    if (!qFuzzyCompare(clamped, m_viewY)) {
        m_viewY = clamped;
        emit viewYChanged();
    }
}

void InkCanvas::autoGrowAndFollow(QPointF doc)
{
    if (doc.y() > m_viewY + height() - 72)
        setViewY(doc.y() - height() + 72);
    emit documentHeightChanged();
}

QPointF InkCanvas::rulerPoint(QPointF start, QPointF current) const
{
    const QPointF d = current - start;
    const qreal len = std::hypot(d.x(), d.y());
    if (len < 1)
        return current;
    const qreal angle = std::atan2(d.y(), d.x());
    const qreal snapped = std::round(angle / qDegreesToRadians(kAngleStep))
        * qDegreesToRadians(kAngleStep);
    return start + QPointF(std::cos(snapped) * len, std::sin(snapped) * len);
}

float InkCanvas::effectivePressure(float pressure) const
{
    if (pressure <= 0.01f)
        return 0.82f;
    return std::clamp(pressure, 0.08f, 1.f);
}

bool InkCanvas::selectionContains(QPointF doc) const
{
    if (!m_document || m_document->selectedCount() == 0)
        return false;
    return m_document->selectionBounds().adjusted(-12, -12, 12, 12).contains(doc);
}
