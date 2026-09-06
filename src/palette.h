#pragma once

#include <QColor>
#include <QString>

// Named inks, resolved at draw time so a theme change never hides a stroke.
// "ink" is always the contrasting writing colour. Blue/red/gray stay chromatic
// and readable on both light and dark paper. Unknown hex from older files is
// mapped onto a name when it was clearly a black/white pen.

inline QColor resolveColor(const QString &id, bool dark)
{
    if (id == QLatin1String("ink"))
        return dark ? QColor(QStringLiteral("#f2f0ea")) : QColor(QStringLiteral("#1a1a1a"));
    if (id == QLatin1String("blue"))
        return dark ? QColor(QStringLiteral("#6db3e0")) : QColor(QStringLiteral("#1d6fa8"));
    if (id == QLatin1String("red"))
        return dark ? QColor(QStringLiteral("#e07070")) : QColor(QStringLiteral("#c0392b"));
    if (id == QLatin1String("gray"))
        return dark ? QColor(QStringLiteral("#a8adb4")) : QColor(QStringLiteral("#5c6370"));

    const QColor raw(id);
    return raw.isValid() ? raw : resolveColor(QStringLiteral("ink"), dark);
}

inline QString canonicalizeColorId(const QString &raw)
{
    if (raw == QLatin1String("ink") || raw == QLatin1String("blue")
        || raw == QLatin1String("red") || raw == QLatin1String("gray"))
        return raw;

    const QColor c(raw);
    if (!c.isValid())
        return QStringLiteral("ink");

    const double luminance = 0.299 * c.redF() + 0.587 * c.greenF() + 0.114 * c.blueF();
    if (c.saturationF() < 0.18) {
        if (luminance < 0.22 || luminance > 0.82)
            return QStringLiteral("ink");
        return QStringLiteral("gray");
    }

    const int hue = c.hue();
    if (hue < 0)
        return QStringLiteral("gray");
    if (hue <= 20 || hue >= 340)
        return QStringLiteral("red");
    if (hue >= 185 && hue <= 255)
        return QStringLiteral("blue");
    return raw;
}
