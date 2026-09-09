#include "document.h"
#include "palette.h"

#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonObject>
#include <QPolygonF>
#include <QUuid>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kMaxHistory = 80;
constexpr qreal kEmptyHeight = 1400;
constexpr qreal kGrowPad = 480;
constexpr qreal kTextSize = 17;          // px, interface font
constexpr qreal kNormalTextWidth = 620;  // reading width when the ink does not set margins
constexpr qreal kPageMargin = 24;
constexpr qreal kInkCoversPage = 0.6;    // ink this wide (of the page) sets the text margins
constexpr qreal kNearbyInk = 400;        // look this far above and below the tap for ink

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

float dist2(QPointF a, QPointF b)
{
    const qreal dx = a.x() - b.x();
    const qreal dy = a.y() - b.y();
    return float(dx * dx + dy * dy);
}

float distToSegment(QPointF p, QPointF a, QPointF b)
{
    const QPointF ab = b - a;
    const qreal len2 = QPointF::dotProduct(ab, ab);
    if (len2 <= 1e-8)
        return std::sqrt(dist2(p, a));
    qreal t = QPointF::dotProduct(p - a, ab) / len2;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF proj = a + ab * t;
    return std::sqrt(dist2(p, proj));
}

bool pointInPolygon(QPointF p, const QVector<QPointF> &poly)
{
    if (poly.size() < 3)
        return false;
    bool inside = false;
    for (int i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const QPointF &a = poly[i];
        const QPointF &b = poly[j];
        const bool intersect = ((a.y() > p.y()) != (b.y() > p.y()))
            && (p.x() < (b.x() - a.x()) * (p.y() - a.y()) / ((b.y() - a.y()) + 1e-12) + a.x());
        if (intersect)
            inside = !inside;
    }
    return inside;
}

double round2(double v)
{
    return std::round(v * 100.0) / 100.0;
}

double round3(double v)
{
    return std::round(v * 1000.0) / 1000.0;
}
}

void Stroke::recomputeBounds()
{
    if (points.isEmpty()) {
        bounds = QRectF();
        return;
    }
    float minX = points[0].x;
    float minY = points[0].y;
    float maxX = minX;
    float maxY = minY;
    float pad = width;
    for (const InkPoint &pt : points) {
        minX = std::min(minX, pt.x);
        minY = std::min(minY, pt.y);
        maxX = std::max(maxX, pt.x);
        maxY = std::max(maxY, pt.y);
        pad = std::max(pad, width * (0.35f + 0.65f * pt.pressure));
    }
    bounds = QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).adjusted(-pad, -pad, pad, pad);
}

QJsonObject Stroke::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("tool"), tool);
    o.insert(QStringLiteral("color"), colorId);
    o.insert(QStringLiteral("width"), round2(width));
    QJsonArray pts;
    for (const InkPoint &pt : points) {
        pts.append(round2(pt.x));
        pts.append(round2(pt.y));
        pts.append(round3(pt.pressure));
    }
    o.insert(QStringLiteral("p"), pts);
    return o;
}

Stroke Stroke::fromJson(const QJsonObject &obj)
{
    Stroke s;
    s.id = obj.value(QStringLiteral("id")).toString();
    if (s.id.isEmpty())
        s.id = newId();
    s.tool = obj.value(QStringLiteral("tool")).toString(QStringLiteral("fineliner"));
    s.colorId = canonicalizeColorId(
        obj.value(QStringLiteral("color")).toString(QStringLiteral("ink")));
    s.width = float(obj.value(QStringLiteral("width")).toDouble(2.4));
    const QJsonArray pts = obj.value(QStringLiteral("p")).toArray();
    s.points.reserve(pts.size() / 3);
    for (int i = 0; i + 2 < pts.size(); i += 3) {
        InkPoint pt;
        pt.x = float(pts.at(i).toDouble());
        pt.y = float(pts.at(i + 1).toDouble());
        pt.pressure = float(pts.at(i + 2).toDouble(1.0));
        s.points.append(pt);
    }
    s.recomputeBounds();
    return s;
}

bool Stroke::hits(QPointF p, float radius) const
{
    const float reach = radius + width;
    if (!bounds.intersects(QRectF(p - QPointF(reach, reach), p + QPointF(reach, reach))))
        return false;
    if (points.isEmpty())
        return false;
    if (points.size() == 1)
        return dist2(p, QPointF(points[0].x, points[0].y)) <= reach * reach;
    for (int i = 0; i < points.size() - 1; ++i) {
        const QPointF a(points[i].x, points[i].y);
        const QPointF b(points[i + 1].x, points[i + 1].y);
        const float w = width * (0.35f + 0.65f * std::max(points[i].pressure, points[i + 1].pressure));
        if (distToSegment(p, a, b) <= radius + w * 0.5f)
            return true;
    }
    return false;
}

bool Stroke::intersectsPolygon(const QVector<QPointF> &poly) const
{
    if (poly.size() < 3 || points.isEmpty())
        return false;
    QRectF polyBounds;
    for (const QPointF &pt : poly)
        polyBounds |= QRectF(pt, QSizeF(0.1, 0.1));
    if (!bounds.intersects(polyBounds.adjusted(-8, -8, 8, 8)))
        return false;
    for (const InkPoint &pt : points) {
        if (pointInPolygon(QPointF(pt.x, pt.y), poly))
            return true;
    }
    QPolygonF qpoly(poly);
    return qpoly.intersects(QPolygonF(QVector<QPointF>{
        bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()}));
}

void Stroke::translate(QPointF delta)
{
    for (InkPoint &pt : points) {
        pt.x += float(delta.x());
        pt.y += float(delta.y());
    }
    bounds.translate(delta);
}

QJsonObject TextBlock::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("x"), round2(x));
    o.insert(QStringLiteral("y"), round2(y));
    o.insert(QStringLiteral("width"), round2(width));
    o.insert(QStringLiteral("size"), round2(size));
    o.insert(QStringLiteral("text"), text);
    return o;
}

TextBlock TextBlock::fromJson(const QJsonObject &obj)
{
    TextBlock b;
    b.id = obj.value(QStringLiteral("id")).toString();
    if (b.id.isEmpty())
        b.id = newId();
    b.x = obj.value(QStringLiteral("x")).toDouble();
    b.y = obj.value(QStringLiteral("y")).toDouble();
    b.width = std::max(40.0, obj.value(QStringLiteral("width")).toDouble(320));
    b.size = std::max(6.0, obj.value(QStringLiteral("size")).toDouble(kTextSize));
    b.text = obj.value(QStringLiteral("text")).toString();
    return b;
}

QFont TextBlock::font() const
{
    // The family resolves to Bold unless the weight is asked for explicitly.
    QFont f(QStringLiteral("iA Writer Quattro S"));
    f.setWeight(QFont::Normal);
    f.setPixelSize(int(std::round(size)));
    return f;
}

QRectF TextBlock::rect() const
{
    const QFontMetricsF fm(font());
    const QString body = text.isEmpty() ? QStringLiteral(" ") : text;
    const QRectF laid = fm.boundingRect(QRectF(0, 0, width, 1e6), Qt::TextWordWrap, body);
    const qreal h = std::max(laid.height(), fm.height());
    return QRectF(x, y, width, h);
}

TextModel::TextModel(Document *doc)
    : QAbstractListModel(doc)
    , m_doc(doc)
{
}

int TextModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_doc->m_texts.size();
}

QVariant TextModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_doc->m_texts.size())
        return {};
    const TextBlock &b = m_doc->m_texts.at(index.row());
    switch (role) {
    case IdRole: return b.id;
    case XRole: return b.x;
    case YRole: return b.y;
    case WidthRole: return b.width;
    case TextRole: return b.text;
    case SizeRole: return b.size;
    default: return {};
    }
}

QHash<int, QByteArray> TextModel::roleNames() const
{
    return {
        {IdRole, "textId"},
        {XRole, "bx"},
        {YRole, "by"},
        {WidthRole, "bw"},
        {TextRole, "body"},
        {SizeRole, "size"},
    };
}

Document::Document(QObject *parent)
    : QObject(parent)
    , m_textModel(new TextModel(this))
{
}

void Document::setTitle(const QString &title)
{
    if (m_title == title)
        return;
    const QString before = m_title;
    m_title = title;
    emit titleChanged();
    touchModifiedTime();
    setModified(true);
    // Consecutive title edits share one undo step, so typing (or a stuck key)
    // does not push the ink history out of the stack.
    if (!m_undo.isEmpty() && m_undo.last().kind == EditKind::Title && m_redo.isEmpty()) {
        m_undo.last().afterTitle = title;
        return;
    }
    Edit e;
    e.kind = EditKind::Title;
    e.beforeTitle = before;
    e.afterTitle = title;
    pushUndo(e);
}

qreal Document::contentHeight() const
{
    qreal h = kEmptyHeight;
    for (const Stroke &s : m_strokes)
        h = std::max(h, s.bounds.bottom() + kGrowPad);
    for (const TextBlock &t : m_texts)
        h = std::max(h, t.rect().bottom() + kGrowPad);
    return h;
}

QRectF Document::selectionBounds() const
{
    QRectF r;
    bool any = false;
    for (const Stroke &s : m_strokes) {
        if (!m_selected.contains(s.id))
            continue;
        r = any ? r.united(s.bounds) : s.bounds;
        any = true;
    }
    return r;
}

void Document::addStroke(Stroke stroke)
{
    if (stroke.id.isEmpty())
        stroke.id = newStrokeId();
    stroke.recomputeBounds();
    Edit e;
    e.kind = EditKind::Add;
    e.strokes = {stroke};
    e.indices = {int(m_strokes.size())};
    m_strokes.append(stroke);
    touchModifiedTime();
    setModified(true);
    pushUndo(e);
    emit contentsChanged();
    emit geometryChanged();
}

void Document::beginErase()
{
    m_erasing = true;
    m_eraseBuffer.clear();
}

void Document::eraseAt(QPointF p, float radius)
{
    if (!m_erasing)
        beginErase();
    for (int i = m_strokes.size() - 1; i >= 0; --i) {
        if (!m_strokes[i].hits(p, radius))
            continue;
        m_eraseBuffer.append(m_strokes[i]);
        m_selected.remove(m_strokes[i].id);
        m_strokes.removeAt(i);
    }
}

void Document::endErase()
{
    if (!m_erasing)
        return;
    m_erasing = false;
    if (m_eraseBuffer.isEmpty())
        return;
    Edit e;
    e.kind = EditKind::Remove;
    e.strokes = m_eraseBuffer;
    m_eraseBuffer.clear();
    touchModifiedTime();
    setModified(true);
    pushUndo(e);
    emit contentsChanged();
    emit geometryChanged();
    emit selectionChanged();
}

void Document::selectLasso(const QVector<QPointF> &poly)
{
    m_selected.clear();
    for (const Stroke &s : m_strokes) {
        if (s.intersectsPolygon(poly))
            m_selected.insert(s.id);
    }
    emit selectionChanged();
}

void Document::clearSelection()
{
    if (m_selected.isEmpty())
        return;
    m_selected.clear();
    emit selectionChanged();
}

void Document::beginTranslate()
{
    m_translating = true;
    m_translateAccum = QPointF();
}

void Document::translateSelection(QPointF delta)
{
    if (delta.manhattanLength() < 0.01 || m_selected.isEmpty())
        return;
    if (!m_translating)
        beginTranslate();
    for (Stroke &s : m_strokes) {
        if (m_selected.contains(s.id))
            s.translate(delta);
    }
    m_translateAccum += delta;
    emit contentsChanged();
    emit geometryChanged();
    emit selectionChanged();
}

void Document::endTranslate()
{
    if (!m_translating)
        return;
    m_translating = false;
    if (m_translateAccum.manhattanLength() < 0.01 || m_selected.isEmpty())
        return;
    Edit e;
    e.kind = EditKind::Translate;
    e.delta = m_translateAccum;
    for (const Stroke &s : m_strokes) {
        if (m_selected.contains(s.id))
            e.strokes.append(s);
    }
    m_translateAccum = QPointF();
    touchModifiedTime();
    setModified(true);
    pushUndo(e);
}

void Document::deleteSelection()
{
    if (m_selected.isEmpty())
        return;
    Edit e;
    e.kind = EditKind::Remove;
    QVector<Stroke> kept;
    kept.reserve(m_strokes.size());
    for (const Stroke &s : m_strokes) {
        if (m_selected.contains(s.id))
            e.strokes.append(s);
        else
            kept.append(s);
    }
    m_strokes = kept;
    m_selected.clear();
    touchModifiedTime();
    setModified(true);
    pushUndo(e);
    emit contentsChanged();
    emit geometryChanged();
    emit selectionChanged();
}

QString Document::addTextAt(qreal x, qreal y, qreal pageWidth)
{
    // Margins from the handwriting near the tap; the whole page if none nearby.
    QRectF near;
    QRectF all;
    for (const Stroke &s : m_strokes) {
        if (s.points.isEmpty())
            continue;
        all |= s.bounds;
        if (s.bounds.bottom() >= y - kNearbyInk && s.bounds.top() <= y + kNearbyInk)
            near |= s.bounds;
    }
    const QRectF ink = near.isNull() ? all : near;

    TextBlock b;
    b.id = newId();
    b.size = kTextSize;
    const bool covers = !ink.isNull() && ink.width() >= kInkCoversPage * pageWidth;
    if (covers) {
        b.x = std::max(0.0, ink.left());
        b.width = std::min(ink.width(), pageWidth - b.x - 8);
    } else {
        b.x = std::max(kPageMargin, x);
        b.width = std::min(kNormalTextWidth, pageWidth - kPageMargin - b.x);
        if (b.width < 200) {
            b.width = std::min(kNormalTextWidth, pageWidth - 2 * kPageMargin);
            b.x = std::max(kPageMargin, pageWidth - kPageMargin - b.width);
        }
    }
    b.width = std::max(40.0, b.width);
    b.y = std::max(0.0, y - b.size * 0.75);
    return addText(b);
}

QString Document::addText(TextBlock block)
{
    if (block.id.isEmpty())
        block.id = newId();
    insertBlock(block, m_texts.size());
    Edit e;
    e.kind = EditKind::AddText;
    e.block = block;
    pushUndo(e);
    afterTextChange();
    return block.id;
}

QString Document::textAt(qreal x, qreal y) const
{
    const QPointF p(x, y);
    for (int i = m_texts.size() - 1; i >= 0; --i) {
        if (m_texts[i].rect().adjusted(-4, -4, 4, 4).contains(p))
            return m_texts[i].id;
    }
    return {};
}

void Document::setTextContent(const QString &id, const QString &text)
{
    const int i = textIndex(id);
    if (i < 0 || m_texts[i].text == text)
        return;
    const QString before = m_texts[i].text;
    applyText(id, text);
    // Typing into one block is one undo step, like the title.
    if (!m_undo.isEmpty() && m_undo.last().kind == EditKind::EditText
        && m_undo.last().block.id == id && m_redo.isEmpty()) {
        m_undo.last().textAfter = text;
    } else {
        Edit e;
        e.kind = EditKind::EditText;
        e.block = m_texts[i];
        e.block.text = before;
        e.textAfter = text;
        pushUndo(e);
    }
    afterTextChange();
}

void Document::removeText(const QString &id)
{
    int index = -1;
    const TextBlock block = takeBlock(id, &index);
    if (index < 0)
        return;
    if (block.text.trimmed().isEmpty()) {
        // An empty block leaves no trace, not even in the history: collapse the
        // AddText (and any typing) that produced it.
        while (!m_undo.isEmpty() && m_undo.last().block.id == id
               && (m_undo.last().kind == EditKind::EditText
                   || m_undo.last().kind == EditKind::AddText)) {
            const bool wasAdd = m_undo.last().kind == EditKind::AddText;
            m_undo.removeLast();
            if (wasAdd)
                break;
        }
        emit historyChanged();
        afterTextChange();
        return;
    }
    Edit e;
    e.kind = EditKind::RemoveText;
    e.block = block;
    e.textIndex = index;
    pushUndo(e);
    afterTextChange();
}

int Document::textIndex(const QString &id) const
{
    for (int i = 0; i < m_texts.size(); ++i) {
        if (m_texts[i].id == id)
            return i;
    }
    return -1;
}

void Document::insertBlock(const TextBlock &block, int index)
{
    index = std::clamp(index, 0, int(m_texts.size()));
    m_textModel->beginInsertRows(QModelIndex(), index, index);
    m_texts.insert(index, block);
    m_textModel->endInsertRows();
}

TextBlock Document::takeBlock(const QString &id, int *index)
{
    const int i = textIndex(id);
    *index = i;
    if (i < 0)
        return {};
    m_textModel->beginRemoveRows(QModelIndex(), i, i);
    const TextBlock block = m_texts.takeAt(i);
    m_textModel->endRemoveRows();
    return block;
}

void Document::applyText(const QString &id, const QString &text)
{
    const int i = textIndex(id);
    if (i < 0)
        return;
    m_texts[i].text = text;
    const QModelIndex mi = m_textModel->index(i);
    emit m_textModel->dataChanged(mi, mi, {TextModel::TextRole});
}

void Document::afterTextChange()
{
    touchModifiedTime();
    setModified(true);
    emit contentsChanged();
    emit geometryChanged();
}

void Document::undo()
{
    if (m_undo.isEmpty())
        return;
    Edit e = m_undo.takeLast();
    switch (e.kind) {
    case EditKind::Add:
        for (int i = m_strokes.size() - 1; i >= 0; --i) {
            for (const Stroke &s : e.strokes) {
                if (m_strokes[i].id == s.id) {
                    m_strokes.removeAt(i);
                    break;
                }
            }
        }
        break;
    case EditKind::Remove:
        for (const Stroke &s : e.strokes)
            m_strokes.append(s);
        break;
    case EditKind::Translate:
        for (Stroke &s : m_strokes) {
            for (const Stroke &sel : e.strokes) {
                if (s.id == sel.id)
                    s.translate(-e.delta);
            }
        }
        break;
    case EditKind::Title:
        m_title = e.beforeTitle;
        emit titleChanged();
        break;
    case EditKind::AddText: {
        int index = -1;
        takeBlock(e.block.id, &index);
        break;
    }
    case EditKind::RemoveText:
        insertBlock(e.block, e.textIndex);
        break;
    case EditKind::EditText:
        applyText(e.block.id, e.block.text);
        break;
    }
    m_redo.append(e);
    touchModifiedTime();
    setModified(true);
    emit contentsChanged();
    emit geometryChanged();
    emit selectionChanged();
    emit historyChanged();
}

void Document::redo()
{
    if (m_redo.isEmpty())
        return;
    Edit e = m_redo.takeLast();
    switch (e.kind) {
    case EditKind::Add:
        for (const Stroke &s : e.strokes)
            m_strokes.append(s);
        break;
    case EditKind::Remove:
        for (int i = m_strokes.size() - 1; i >= 0; --i) {
            for (const Stroke &s : e.strokes) {
                if (m_strokes[i].id == s.id) {
                    m_strokes.removeAt(i);
                    break;
                }
            }
        }
        break;
    case EditKind::Translate:
        for (Stroke &s : m_strokes) {
            for (const Stroke &sel : e.strokes) {
                if (s.id == sel.id)
                    s.translate(e.delta);
            }
        }
        break;
    case EditKind::Title:
        m_title = e.afterTitle;
        emit titleChanged();
        break;
    case EditKind::AddText:
        insertBlock(e.block, m_texts.size());
        break;
    case EditKind::RemoveText: {
        int index = -1;
        takeBlock(e.block.id, &index);
        break;
    }
    case EditKind::EditText:
        applyText(e.block.id, e.textAfter);
        break;
    }
    m_undo.append(e);
    touchModifiedTime();
    setModified(true);
    emit contentsChanged();
    emit geometryChanged();
    emit selectionChanged();
    emit historyChanged();
}

QJsonObject Document::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("format"), QStringLiteral("omascribe"));
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("id"), m_id);
    o.insert(QStringLiteral("title"), m_title.trimmed());
    o.insert(QStringLiteral("created"), m_created.toUTC().toString(Qt::ISODateWithMs));
    o.insert(QStringLiteral("modified"), m_modifiedTime.toUTC().toString(Qt::ISODateWithMs));
    o.insert(QStringLiteral("height"), contentHeight());
    QJsonArray arr;
    for (const Stroke &s : m_strokes)
        arr.append(s.toJson());
    o.insert(QStringLiteral("strokes"), arr);
    QJsonArray texts;
    for (const TextBlock &t : m_texts)
        texts.append(t.toJson());
    if (!texts.isEmpty())
        o.insert(QStringLiteral("texts"), texts);
    return o;
}

Document *Document::fromJson(const QJsonObject &obj, QObject *parent)
{
    auto *doc = new Document(parent);
    doc->m_id = obj.value(QStringLiteral("id")).toString();
    if (doc->m_id.isEmpty())
        doc->m_id = newId();
    doc->m_title = obj.value(QStringLiteral("title")).toString().trimmed();
    doc->m_created = QDateTime::fromString(obj.value(QStringLiteral("created")).toString(),
                                           Qt::ISODateWithMs);
    if (!doc->m_created.isValid())
        doc->m_created = QDateTime::fromString(obj.value(QStringLiteral("created")).toString(),
                                               Qt::ISODate);
    if (!doc->m_created.isValid())
        doc->m_created = QDateTime::currentDateTimeUtc();
    doc->m_modifiedTime = QDateTime::fromString(obj.value(QStringLiteral("modified")).toString(),
                                                Qt::ISODateWithMs);
    if (!doc->m_modifiedTime.isValid())
        doc->m_modifiedTime = QDateTime::fromString(obj.value(QStringLiteral("modified")).toString(),
                                                    Qt::ISODate);
    if (!doc->m_modifiedTime.isValid())
        doc->m_modifiedTime = doc->m_created;
    const QJsonArray arr = obj.value(QStringLiteral("strokes")).toArray();
    doc->m_strokes.reserve(arr.size());
    for (const QJsonValue &v : arr)
        doc->m_strokes.append(Stroke::fromJson(v.toObject()));
    const QJsonArray texts = obj.value(QStringLiteral("texts")).toArray();
    for (const QJsonValue &v : texts)
        doc->m_texts.append(TextBlock::fromJson(v.toObject()));
    doc->m_modified = false;
    return doc;
}

Document *Document::createNew(QObject *parent)
{
    auto *doc = new Document(parent);
    doc->m_id = newId();
    doc->m_title = QString();
    doc->m_created = QDateTime::currentDateTimeUtc();
    doc->m_modifiedTime = doc->m_created;
    doc->m_modified = true;
    return doc;
}

void Document::markSaved()
{
    setModified(false);
}

void Document::setModified(bool modified)
{
    if (m_modified == modified)
        return;
    m_modified = modified;
    emit modifiedChanged();
}

void Document::pushUndo(Edit edit)
{
    m_undo.append(std::move(edit));
    if (m_undo.size() > kMaxHistory)
        m_undo.removeFirst();
    m_redo.clear();
    emit historyChanged();
}

QString Document::newStrokeId() const
{
    return newId();
}

void Document::touchModifiedTime()
{
    m_modifiedTime = QDateTime::currentDateTimeUtc();
}
