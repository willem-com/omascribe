#pragma once

#include "document.h"

#include <QColor>
#include <QElapsedTimer>
#include <QQuickPaintedItem>
#include <QString>
#include <QVector>

class QTabletEvent;

class InkCanvas : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Document *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(QString colorId READ colorId WRITE setColorId NOTIFY colorIdChanged)
    Q_PROPERTY(qreal inkWidth READ inkWidth WRITE setInkWidth NOTIFY inkWidthChanged)
    Q_PROPERTY(qreal viewY READ viewY WRITE setViewY NOTIFY viewYChanged)
    Q_PROPERTY(qreal documentHeight READ documentHeight NOTIFY documentHeightChanged)
    Q_PROPERTY(QColor paperColor READ paperColor WRITE setPaperColor NOTIFY paperColorChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY gridColorChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)

public:
    explicit InkCanvas(QQuickItem *parent = nullptr);

    Document *document() const { return m_document; }
    void setDocument(Document *document);

    QString tool() const { return m_tool; }
    void setTool(const QString &tool);

    QString colorId() const { return m_colorId; }
    void setColorId(const QString &id);

    qreal inkWidth() const { return m_inkWidth; }
    void setInkWidth(qreal width);

    qreal viewY() const { return m_viewY; }
    void setViewY(qreal y);

    qreal documentHeight() const;

    QColor paperColor() const { return m_paperColor; }
    void setPaperColor(const QColor &color);

    QColor gridColor() const { return m_gridColor; }
    void setGridColor(const QColor &color);

    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool dark);

    void paint(QPainter *painter) override;

    enum class Pointer { None, Pen, Mouse, Finger };

    Q_INVOKABLE void zoomReset();
    Q_INVOKABLE void scrollBy(qreal dy);

signals:
    void documentChanged();
    void toolChanged();
    void colorIdChanged();
    void inkWidthChanged();
    void engaged();
    void viewYChanged();
    void documentHeightChanged();
    void paperColorChanged();
    void gridColorChanged();
    void darkModeChanged();
    void drawingChanged();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void touchEvent(QTouchEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    bool event(QEvent *event) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void handleTablet(QTabletEvent *event);
    void pointerDown(QPointF local, float pressure, Pointer pointer, bool eraserTip);
    void pointerMove(QPointF local, float pressure, Pointer pointer);
    void pointerUp(QPointF local, Pointer pointer);
    QPointF toDoc(QPointF local) const;
    void beginStroke(QPointF doc, float pressure);
    void extendStroke(QPointF doc, float pressure);
    void endStroke();
    void drawStroke(QPainter *painter, const Stroke &stroke) const;
    void drawGrid(QPainter *painter) const;
    void drawLasso(QPainter *painter) const;
    void drawSelection(QPainter *painter) const;
    void drawCursor(QPainter *painter) const;
    void clampView();
    void autoGrowAndFollow(QPointF doc);
    QPointF rulerPoint(QPointF start, QPointF current) const;
    float effectivePressure(float pressure) const;
    bool selectionContains(QPointF doc) const;
    QColor strokePaintColor(const Stroke &stroke) const;

    Document *m_document = nullptr;
    QString m_tool = QStringLiteral("pen");
    QString m_colorId = QStringLiteral("ink");
    QColor m_paperColor = QColor(QStringLiteral("#f7f4ec"));
    QColor m_gridColor = QColor(0, 0, 0, 28);
    qreal m_inkWidth = 2.4;
    qreal m_viewY = 0;
    bool m_darkMode = false;

    Stroke m_live;
    bool m_liveActive = false;
    bool m_panning = false;
    bool m_movingSelection = false;
    bool m_lassoing = false;
    bool m_penDown = false;
    Pointer m_activePointer = Pointer::None;
    QPointF m_lastLocal;
    QPointF m_pressDoc;
    QVector<QPointF> m_lasso;
    QPointF m_hoverDoc;
    bool m_hovering = false;
    QElapsedTimer m_clock;
};
