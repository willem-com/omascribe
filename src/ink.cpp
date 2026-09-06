#include "ink.h"

#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>

namespace {
QPointF perpNorm(QPointF d)
{
    const qreal len = std::hypot(d.x(), d.y());
    if (len < 1e-5)
        return {0, 0};
    return {-d.y() / len, d.x() / len};
}
}

float strokeRadius(const Stroke &stroke, const InkPoint &point)
{
    return stroke.width * (0.38f + 0.62f * std::clamp(point.pressure, 0.05f, 1.f)) * 0.5f;
}

void paintStroke(QPainter *painter, const Stroke &stroke, const QColor &color)
{
    if (!painter || stroke.points.isEmpty())
        return;
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    if (stroke.points.size() == 1) {
        const float r = strokeRadius(stroke, stroke.points[0]);
        painter->drawEllipse(QPointF(stroke.points[0].x, stroke.points[0].y), r, r);
        return;
    }
    for (int i = 0; i < stroke.points.size() - 1; ++i) {
        const InkPoint &a = stroke.points[i];
        const InkPoint &b = stroke.points[i + 1];
        const QPointF pa(a.x, a.y);
        const QPointF pb(b.x, b.y);
        const float r1 = strokeRadius(stroke, a);
        const float r2 = strokeRadius(stroke, b);
        const QPointF n = perpNorm(pb - pa);
        if (n.isNull()) {
            painter->drawEllipse(pa, r1, r1);
            continue;
        }
        QPolygonF quad;
        quad << pa + n * r1 << pa - n * r1 << pb - n * r2 << pb + n * r2;
        painter->drawPolygon(quad);
        painter->drawEllipse(pa, r1, r1);
    }
    const InkPoint &last = stroke.points.last();
    const float r = strokeRadius(stroke, last);
    painter->drawEllipse(QPointF(last.x, last.y), r, r);
}

QRectF strokesBounds(const QVector<Stroke> &strokes)
{
    QRectF bounds;
    bool any = false;
    for (const Stroke &s : strokes) {
        if (s.points.isEmpty())
            continue;
        bounds = any ? bounds.united(s.bounds) : s.bounds;
        any = true;
    }
    return bounds;
}
