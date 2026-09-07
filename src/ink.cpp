#include "ink.h"

#include <QPainter>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kJoinAngle = 0.35f;   // ~20 degrees; sharper turns get a round outer join
constexpr int kCapSegments = 8;       // line segments per half-circle cap
constexpr int kDotSegments = 16;
constexpr float kArcStep = float(M_PI) / 8.f;

QPointF unit(QPointF d)
{
    const qreal len = std::hypot(d.x(), d.y());
    if (len < 1e-6)
        return {0, 0};
    return d / len;
}

QPointF perp(QPointF d)
{
    return {-d.y(), d.x()};
}

QPointF rotated(QPointF v, qreal angle)
{
    const qreal c = std::cos(angle);
    const qreal s = std::sin(angle);
    return {v.x() * c - v.y() * s, v.x() * s + v.y() * c};
}

// Half circle from `from` to `to` around `c`, passing through `via`, endpoints excluded.
void capPoints(QPointF c, float r, QPointF fromDir, QPointF viaDir, QVector<QPointF> &out)
{
    for (int k = 1; k < kCapSegments; ++k) {
        const qreal t = M_PI * qreal(k) / kCapSegments;
        out.append(c + (fromDir * std::cos(t) + viaDir * std::sin(t)) * r);
    }
}
}

float strokeRadius(const Stroke &stroke, const InkPoint &point)
{
    return stroke.width * (0.38f + 0.62f * std::clamp(point.pressure, 0.05f, 1.f)) * 0.5f;
}

Ribbon strokeRibbon(const Stroke &stroke)
{
    Ribbon rb;
    QVector<QPointF> pts;
    QVector<float> radii;
    pts.reserve(stroke.points.size());
    radii.reserve(stroke.points.size());
    for (const InkPoint &ip : stroke.points) {
        const QPointF p(ip.x, ip.y);
        if (!pts.isEmpty() && QLineF(pts.last(), p).length() < 1e-3)
            continue;
        pts.append(p);
        radii.append(strokeRadius(stroke, ip));
    }
    if (pts.isEmpty())
        return rb;
    rb.startCenter = pts.first();
    rb.endCenter = pts.last();
    rb.startRadius = radii.first();
    rb.endRadius = radii.last();
    if (pts.size() == 1) {
        rb.dot = true;
        return rb;
    }

    const int n = pts.size();
    QVector<QPointF> dirs(n - 1);
    for (int i = 0; i < n - 1; ++i)
        dirs[i] = unit(pts[i + 1] - pts[i]);
    rb.startDir = dirs.first();
    rb.endDir = dirs.last();
    rb.left.reserve(n + 8);
    rb.right.reserve(n + 8);

    for (int i = 0; i < n; ++i) {
        const QPointF p = pts[i];
        const float r = radii[i];
        const QPointF dPrev = i == 0 ? dirs[0] : dirs[i - 1];
        const QPointF dNext = i == n - 1 ? dirs[n - 2] : dirs[i];
        const QPointF nPrev = perp(dPrev);
        const QPointF nNext = perp(dNext);
        const qreal cross = dPrev.x() * dNext.y() - dPrev.y() * dNext.x();
        const qreal dot = QPointF::dotProduct(dPrev, dNext);
        const qreal angle = std::atan2(std::abs(cross), dot);
        QPointF nAvg = unit(nPrev + nNext);
        if (nAvg.isNull())
            nAvg = nPrev;

        if (angle < kJoinAngle || i == 0 || i == n - 1) {
            const qreal m = QPointF::dotProduct(nAvg, nPrev);
            const qreal scale = m > 0.5 ? 1.0 / m : 2.0;
            rb.left.append(p + nAvg * (r * scale));
            rb.right.append(p - nAvg * (r * scale));
            continue;
        }

        // Sharp turn: the inner side collapses onto one point, the outer side
        // walks an arc from the previous normal to the next one.
        const bool innerIsLeft = cross > 0;
        const qreal side = innerIsLeft ? -1.0 : 1.0;
        const QPointF inner = p + nAvg * (r * -side);
        const QPointF u0 = nPrev * side;
        const int steps = std::max(1, int(std::ceil(angle / kArcStep)));
        const qreal signedAngle = cross > 0 ? angle : -angle;
        for (int k = 0; k <= steps; ++k) {
            const QPointF u = rotated(u0, signedAngle * k / steps);
            const QPointF outer = p + u * r;
            if (innerIsLeft) {
                rb.left.append(inner);
                rb.right.append(outer);
            } else {
                rb.left.append(outer);
                rb.right.append(inner);
            }
        }
    }
    return rb;
}

QPainterPath strokeOutline(const Stroke &stroke)
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    const Ribbon rb = strokeRibbon(stroke);
    if (rb.dot) {
        path.addEllipse(rb.startCenter, rb.startRadius, rb.startRadius);
        return path;
    }
    if (rb.left.isEmpty())
        return path;

    path.moveTo(rb.left.first());
    for (int i = 1; i < rb.left.size(); ++i)
        path.lineTo(rb.left[i]);

    QVector<QPointF> cap;
    capPoints(rb.endCenter, rb.endRadius, perp(rb.endDir), rb.endDir, cap);
    for (const QPointF &c : cap)
        path.lineTo(c);

    for (int i = rb.right.size() - 1; i >= 0; --i)
        path.lineTo(rb.right[i]);

    cap.clear();
    capPoints(rb.startCenter, rb.startRadius, -perp(rb.startDir), -rb.startDir, cap);
    for (const QPointF &c : cap)
        path.lineTo(c);
    path.closeSubpath();
    return path;
}

void appendStrokeTriangles(const Stroke &stroke, QVector<QPointF> &out)
{
    const Ribbon rb = strokeRibbon(stroke);
    if (rb.dot) {
        const QPointF c = rb.startCenter;
        const float r = rb.startRadius;
        for (int k = 0; k < kDotSegments; ++k) {
            const qreal a0 = 2 * M_PI * k / kDotSegments;
            const qreal a1 = 2 * M_PI * (k + 1) / kDotSegments;
            out.append(c);
            out.append(c + QPointF(std::cos(a0), std::sin(a0)) * r);
            out.append(c + QPointF(std::cos(a1), std::sin(a1)) * r);
        }
        return;
    }
    const int m = rb.left.size();
    if (m < 2)
        return;
    out.reserve(out.size() + (m - 1) * 6 + kCapSegments * 6);
    for (int i = 0; i < m - 1; ++i) {
        out.append(rb.left[i]);
        out.append(rb.right[i]);
        out.append(rb.left[i + 1]);
        out.append(rb.right[i]);
        out.append(rb.right[i + 1]);
        out.append(rb.left[i + 1]);
    }

    auto fan = [&out](QPointF c, const QVector<QPointF> &ring) {
        for (int i = 0; i < ring.size() - 1; ++i) {
            out.append(c);
            out.append(ring[i]);
            out.append(ring[i + 1]);
        }
    };
    QVector<QPointF> ring;
    ring.append(rb.left.last());
    capPoints(rb.endCenter, rb.endRadius, perp(rb.endDir), rb.endDir, ring);
    ring.append(rb.right.last());
    fan(rb.endCenter, ring);

    ring.clear();
    ring.append(rb.right.first());
    capPoints(rb.startCenter, rb.startRadius, -perp(rb.startDir), -rb.startDir, ring);
    ring.append(rb.left.first());
    fan(rb.startCenter, ring);
}

void paintStroke(QPainter *painter, const Stroke &stroke, const QColor &color)
{
    if (!painter || stroke.points.isEmpty())
        return;
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawPath(strokeOutline(stroke));
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
