#include "inkcanvas.h"
#include "ink.h"
#include "palette.h"

#include <QCursor>
#include <QHoverEvent>
#include <QImage>
#include <QPixmap>
#include <QVector>
#include <QInputDevice>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QPointerEvent>
#include <QPointingDevice>
#include <QPainterPath>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QQuickWindow>
#include <QWindow>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kMinStep = 0.7f;
constexpr float kEraserRadius = 18.f;
constexpr qreal kAngleStep = 15.0;
}

InkCanvas::InkCanvas(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
    setAcceptTouchEvents(true);
    setKeepTouchGrab(true);
    setAcceptHoverEvents(true);
    setAntialiasing(true);
    setOpaquePainting(true);
    setFillColor(Qt::white);
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setPerformanceHint(QQuickPaintedItem::FastFBOResizing, true);
    QPixmap blank(1, 1);
    blank.fill(Qt::transparent);
    setCursor(QCursor(blank, 0, 0));
    m_clock.start();
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
            invalidateCache();
            update();
        });
        connect(m_document, &Document::selectionChanged, this, [this]() { update(); });
        connect(m_document, &Document::geometryChanged, this, [this]() {
            emit documentHeightChanged();
            clampView();
            invalidateCache();
            update();
        });
    }
    m_viewY = 0;
    m_liveActive = false;
    m_lasso.clear();
    invalidateCache();
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
    invalidateCache();
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
    setFillColor(color);
    emit paperColorChanged();
    invalidateCache();
    update();
}

void InkCanvas::setGridColor(const QColor &color)
{
    if (m_gridColor == color)
        return;
    m_gridColor = color;
    emit gridColorChanged();
    invalidateCache();
    update();
}

void InkCanvas::setDarkMode(bool dark)
{
    if (m_darkMode == dark)
        return;
    m_darkMode = dark;
    emit darkModeChanged();
    invalidateCache();
    update();
}

void InkCanvas::paint(QPainter *painter)
{
    ensureCache();
    if (!m_cache.isNull())
        painter->drawImage(QPointF(0, 0), m_cache);
    else
        painter->fillRect(boundingRect(), m_paperColor);
    painter->save();
    painter->translate(0, -m_viewY);
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (m_liveActive)
        drawStroke(painter, m_live);
    drawLasso(painter);
    drawSelection(painter);
    painter->restore();
    drawCursor(painter);
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
    setViewY(m_viewY + dy);
    event->accept();
}

void InkCanvas::touchEvent(QTouchEvent *event)
{
    event->accept();
    if (m_penDown)
        return;

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
            if (!m_touchCentroid.isNull() && m_touchMoved)
                setViewY(m_viewY - (centroid.y() - m_touchCentroid.y()));
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
        handleTablet(static_cast<QTabletEvent *>(event));
        return true;
    default:
        return QQuickPaintedItem::event(event);
    }
}

void InkCanvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    emit documentHeightChanged();
    clampView();
    invalidateCache();
}

void InkCanvas::handleTablet(QTabletEvent *event)
{
    const QPointF local = mapFromScene(event->scenePosition());
    const bool eraserTip = event->pointerType() == QPointingDevice::PointerType::Eraser;
    float pressure = float(event->pressure());
    if (event->type() == QEvent::TabletPress) {
        m_penDown = true;
        pointerDown(local, pressure, Pointer::Pen, eraserTip);
    } else if (event->type() == QEvent::TabletMove) {
        if (event->buttons() == Qt::NoButton) {
            m_hovering = true;
            m_hoverDoc = toDoc(local);
            update();
        } else {
            pointerMove(local, pressure, Pointer::Pen);
        }
    } else if (event->type() == QEvent::TabletRelease) {
        pointerUp(local, Pointer::Pen);
        m_penDown = false;
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
        if (m_document)
            m_document->beginErase();
        if (m_document)
            m_document->eraseAt(m_pressDoc, kEraserRadius);
        invalidateCache();
        update();
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
        setViewY(m_viewY - delta.y());
        return;
    }
    const QPointF doc = toDoc(local);
    m_hoverDoc = doc;
    m_hovering = true;

    if (m_document && m_tool == QStringLiteral("eraser") && !m_liveActive && !m_lassoing
        && !m_movingSelection && (m_activePointer == Pointer::Pen || m_activePointer == Pointer::Mouse)) {
        m_document->eraseAt(doc, kEraserRadius);
        invalidateCache();
        update();
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

void InkCanvas::drawStroke(QPainter *painter, const Stroke &stroke) const
{
    paintStroke(painter, stroke, strokePaintColor(stroke));
}

void InkCanvas::drawGrid(QPainter *painter) const
{
    const qreal step = 28;
    const qreal y0 = std::floor(m_viewY / step) * step;
    const qreal y1 = m_viewY + height() + step;
    QPolygonF dots;
    dots.reserve(int((width() / step) * ((y1 - y0) / step) + 8));
    for (qreal y = y0; y <= y1; y += step) {
        for (qreal x = step; x < width(); x += step)
            dots.append(QPointF(x, y));
    }
    painter->setPen(QPen(m_gridColor, 1.1));
    painter->drawPoints(dots);
}

void InkCanvas::drawLasso(QPainter *painter) const
{
    if (m_lasso.size() < 2)
        return;
    QPainterPath path;
    path.moveTo(m_lasso.first());
    for (int i = 1; i < m_lasso.size(); ++i)
        path.lineTo(m_lasso[i]);
    QPen pen(m_darkMode ? QColor(220, 220, 220, 180) : QColor(40, 40, 40, 160), 1.2, Qt::DashLine);
    painter->setPen(pen);
    painter->setBrush(m_darkMode ? QColor(120, 170, 220, 40) : QColor(32, 119, 178, 40));
    painter->drawPath(path);
}

void InkCanvas::drawSelection(QPainter *painter) const
{
    if (!m_document || m_document->selectedCount() == 0)
        return;
    const QRectF r = m_document->selectionBounds().adjusted(-6, -6, 6, 6);
    QPen pen(m_darkMode ? QColor(QStringLiteral("#5584aa")) : QColor(QStringLiteral("#2077b2")), 1.4,
             Qt::DashLine);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(r, 4, 4);
}

void InkCanvas::drawCursor(QPainter *painter) const
{
    if (!m_hovering && !m_liveActive)
        return;
    const QPointF p(m_hoverDoc.x(), m_hoverDoc.y() - m_viewY);
    if (m_tool == QStringLiteral("eraser")) {
        painter->setPen(QPen(m_darkMode ? QColor(255, 255, 255, 160) : QColor(0, 0, 0, 140), 1.2));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(p, kEraserRadius, kEraserRadius);
        return;
    }
    if (m_tool == QStringLiteral("select")) {
        painter->setPen(QPen(m_darkMode ? QColor(255, 255, 255, 160) : QColor(0, 0, 0, 140), 1.1));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(p, 5, 5);
        return;
    }
    const QColor c = resolveColor(m_colorId, m_darkMode);
    const qreal r = m_inkWidth * 0.5;
    painter->setBrush(c);
    painter->setPen(QPen(m_darkMode ? QColor(0, 0, 0, 90) : QColor(255, 255, 255, 90), 1));
    painter->drawEllipse(p, r, r);
}

void InkCanvas::invalidateCache()
{
    m_cacheDirty = true;
}

void InkCanvas::ensureCache()
{
    const qreal dpr = window() ? window()->devicePixelRatio() : 1.0;
    const int pxW = int(std::ceil(width() * dpr));
    const int pxH = int(std::ceil(height() * dpr));
    if (pxW <= 0 || pxH <= 0)
        return;
    if (!m_cacheDirty && m_cache.size() == QSize(pxW, pxH)
        && qFuzzyCompare(m_cacheViewY, m_viewY) && qFuzzyCompare(m_cacheDpr, dpr))
        return;

    m_cache = QImage(pxW, pxH, QImage::Format_RGB32);
    m_cache.setDevicePixelRatio(dpr);
    QPainter cachePainter(&m_cache);
    cachePainter.setRenderHint(QPainter::Antialiasing, true);
    cachePainter.fillRect(QRectF(0, 0, width(), height()), m_paperColor);
    cachePainter.translate(0, -m_viewY);
    drawGrid(&cachePainter);
    if (m_document) {
        const QRectF view(0, m_viewY, width(), height());
        const QRectF padded = view.adjusted(-40, -40, 40, 40);
        for (const Stroke &s : m_document->strokes()) {
            if (!s.bounds.intersects(padded))
                continue;
            paintStroke(&cachePainter, s, strokePaintColor(s));
        }
    }
    cachePainter.end();
    m_cacheDirty = false;
    m_cacheViewY = m_viewY;
    m_cacheDpr = dpr;
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
