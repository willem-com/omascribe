#pragma once

#include "document.h"

#include <QColor>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QVector>

class QPainter;

// Radius of the ink at one point: pen width scaled by pressure.
float strokeRadius(const Stroke &stroke, const InkPoint &point);

// A variable-width stroke as paired left/right edge points. Sharp turns get
// extra pairs on the outer side so the join stays round. The same ribbon feeds
// the scene graph (triangles) and the exporters (one closed outline path).
struct Ribbon {
    QVector<QPointF> left;
    QVector<QPointF> right;
    QPointF startCenter;
    QPointF endCenter;
    QPointF startDir;   // unit direction of travel leaving the first point
    QPointF endDir;     // unit direction of travel arriving at the last point
    float startRadius = 0;
    float endRadius = 0;
    bool dot = false;   // single point: a disc at startCenter
};

Ribbon strokeRibbon(const Stroke &stroke);

// One closed path per stroke (winding fill): edges plus two round caps.
QPainterPath strokeOutline(const Stroke &stroke);

// Triangle list (three points per triangle) covering the stroke.
void appendStrokeTriangles(const Stroke &stroke, QVector<QPointF> &out);

void paintStroke(QPainter *painter, const Stroke &stroke, const QColor &color);
QRectF strokesBounds(const QVector<Stroke> &strokes);
