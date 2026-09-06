#include "exporter.h"

#include "document.h"
#include "ink.h"
#include "palette.h"

#include <QFont>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSvgGenerator>
#include <algorithm>

namespace {
constexpr qreal kPageWidthPt = 595.28; // A4
constexpr qreal kMarginPt = 36;
constexpr qreal kTitlePt = 18;
const QColor kPaper(QStringLiteral("#f7f4ec"));

struct Layout {
    QSizeF page;
    qreal scale = 1;
    QPointF origin;
    bool hasTitle = false;
};

Layout layoutFor(const Document *doc)
{
    Layout layout;
    QRectF ink = strokesBounds(doc->strokes());
    if (ink.isNull())
        ink = QRectF(0, 0, 400, 240);
    const qreal usable = kPageWidthPt - 2 * kMarginPt;
    layout.scale = usable / std::max(ink.width(), 80.0);
    layout.hasTitle = !doc->title().trimmed().isEmpty();
    const qreal titleBand = layout.hasTitle ? kTitlePt + 16 : 0;
    const qreal contentH = ink.height() * layout.scale;
    layout.page = QSizeF(kPageWidthPt, kMarginPt + titleBand + contentH + kMarginPt);
    if (layout.page.height() < 400)
        layout.page.setHeight(400);
    layout.origin = QPointF(kMarginPt - ink.left() * layout.scale,
                            kMarginPt + titleBand - ink.top() * layout.scale);
    return layout;
}

void renderNote(QPainter *painter, const Document *doc, const Layout &layout)
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->fillRect(QRectF(QPointF(0, 0), layout.page), kPaper);

    if (layout.hasTitle) {
        QFont font(QStringLiteral("iA Writer Quattro S"));
        font.setPointSizeF(kTitlePt);
        painter->setFont(font);
        painter->setPen(QColor(QStringLiteral("#1a1a1a")));
        painter->drawText(QRectF(kMarginPt, kMarginPt, layout.page.width() - 2 * kMarginPt, kTitlePt + 8),
                          Qt::AlignLeft | Qt::AlignVCenter, doc->title());
    }

    painter->save();
    painter->translate(layout.origin);
    painter->scale(layout.scale, layout.scale);
    for (const Stroke &stroke : doc->strokes())
        paintStroke(painter, stroke, resolveColor(stroke.colorId, false));
    painter->restore();
}

QString sanitizedTitle(const Document *doc)
{
    QString title = doc && !doc->title().trimmed().isEmpty()
        ? doc->title().trimmed()
        : QStringLiteral("Note");
    title.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("-"));
    title.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (title.isEmpty())
        title = QStringLiteral("Note");
    return title;
}
}

QString suggestedExportName(const Document *doc)
{
    return sanitizedTitle(doc);
}

bool exportNotePdf(const Document *doc, const QString &path)
{
    if (!doc || path.isEmpty())
        return false;
    const Layout layout = layoutFor(doc);
    QPdfWriter writer(path);
    writer.setTitle(doc->title().isEmpty() ? QStringLiteral("Omascribe note") : doc->title());
    writer.setCreator(QStringLiteral("Omascribe"));
    writer.setResolution(72);
    writer.setPageSize(QPageSize(QSizeF(layout.page.width() * 25.4 / 72.0,
                                        layout.page.height() * 25.4 / 72.0),
                                 QPageSize::Millimeter));
    QPainter painter;
    if (!painter.begin(&writer))
        return false;
    renderNote(&painter, doc, layout);
    painter.end();
    return true;
}

bool exportNoteSvg(const Document *doc, const QString &path)
{
    if (!doc || path.isEmpty())
        return false;
    const Layout layout = layoutFor(doc);
    QSvgGenerator gen;
    gen.setFileName(path);
    gen.setSize(layout.page.toSize());
    gen.setViewBox(QRectF(QPointF(0, 0), layout.page));
    gen.setTitle(doc->title().isEmpty() ? QStringLiteral("Omascribe note") : doc->title());
    gen.setDescription(QStringLiteral("Handwritten note from Omascribe"));
    QPainter painter;
    if (!painter.begin(&gen))
        return false;
    renderNote(&painter, doc, layout);
    painter.end();
    return true;
}
