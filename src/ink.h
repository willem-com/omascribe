#pragma once

#include "document.h"

#include <QColor>
#include <QRectF>

class QPainter;

float strokeRadius(const Stroke &stroke, const InkPoint &point);
void paintStroke(QPainter *painter, const Stroke &stroke, const QColor &color);
QRectF strokesBounds(const QVector<Stroke> &strokes);
