#include "PlotTypes.h"

#include <QHash>

namespace gp {

QColor paletteColor(int index)
{
    static const QColor colors[] = {
        QColor(0x2a, 0x78, 0xd6),  // blue
        QColor(0xeb, 0x68, 0x34),  // orange
        QColor(0x1b, 0xaf, 0x7a),  // aqua
        QColor(0xed, 0xa1, 0x00),  // yellow
        QColor(0xe8, 0x7b, 0xa4),  // magenta
        QColor(0x00, 0x83, 0x00),  // green
        QColor(0x62, 0x50, 0xd6),  // violet
        QColor(0xe3, 0x49, 0x48),  // red
    };
    constexpr int n = int(sizeof(colors) / sizeof(colors[0]));
    if (index < 0) index = 0;
    return colors[index % n];
}

QColor parseColor(const QString& nameIn)
{
    const QString name = nameIn.trimmed().toLower();
    if (name.isEmpty()) return {};
    // Short matplotlib-style names and the palette names map to the palette
    // tones so that "blue"/"red" look consistent with the default styling.
    static const QHash<QString, QColor> named = {
        {"b", paletteColor(0)},       {"blue", paletteColor(0)},
        {"orange", paletteColor(1)},  {"aqua", paletteColor(2)},
        {"teal", paletteColor(2)},    {"y", paletteColor(3)},
        {"yellow", paletteColor(3)},  {"m", paletteColor(4)},
        {"magenta", paletteColor(4)}, {"g", paletteColor(5)},
        {"green", paletteColor(5)},   {"violet", paletteColor(6)},
        {"purple", paletteColor(6)},  {"r", paletteColor(7)},
        {"red", paletteColor(7)},     {"k", QColor(0x0b, 0x0b, 0x0b)},
        {"black", QColor(0x0b, 0x0b, 0x0b)},
        {"w", QColor(Qt::white)},     {"c", QColor(0x00, 0xa6, 0xc8)},
        {"cyan", QColor(0x00, 0xa6, 0xc8)},
        {"gray", QColor(0x7a, 0x79, 0x74)}, {"grey", QColor(0x7a, 0x79, 0x74)},
    };
    if (auto it = named.find(name); it != named.end()) return *it;
    if (name.startsWith("c") && name.size() >= 2) {  // "C0".."C7" like matplotlib
        bool ok = false;
        const int idx = name.mid(1).toInt(&ok);
        if (ok) return paletteColor(idx);
    }
    QColor c = QColor::fromString(name);  // "#rrggbb", "#aarrggbb", SVG names
    return c.isValid() ? c : QColor();
}

LineStyle parseLineStyle(const QString& sIn, bool* ok)
{
    const QString s = sIn.trimmed().toLower();
    if (ok) *ok = true;
    if (s == "-" || s == "solid" || s.isEmpty()) return LineStyle::Solid;
    if (s == "--" || s == "dash" || s == "dashed") return LineStyle::Dash;
    if (s == ":" || s == "dot" || s == "dotted") return LineStyle::Dot;
    if (s == "-." || s == "dashdot") return LineStyle::DashDot;
    if (s == "none" || s == " ") return LineStyle::None;
    if (ok) *ok = false;
    return LineStyle::Solid;
}

QString lineStyleName(LineStyle s)
{
    switch (s) {
    case LineStyle::Solid: return "solid";
    case LineStyle::Dash: return "dash";
    case LineStyle::Dot: return "dot";
    case LineStyle::DashDot: return "dashdot";
    case LineStyle::None: return "none";
    }
    return "solid";
}

ScatterShape parseScatterShape(const QString& sIn, bool* ok)
{
    const QString s = sIn.trimmed().toLower();
    if (ok) *ok = true;
    if (s.isEmpty() || s == "none") return ScatterShape::None;
    if (s == "o" || s == "circle") return ScatterShape::Circle;
    if (s == "s" || s == "square") return ScatterShape::Square;
    if (s == "^" || s == "triangle") return ScatterShape::Triangle;
    if (s == "x" || s == "cross") return ScatterShape::Cross;
    if (s == "+" || s == "plus") return ScatterShape::Plus;
    if (s == "d" || s == "diamond") return ScatterShape::Diamond;
    if (ok) *ok = false;
    return ScatterShape::None;
}

QString scatterShapeName(ScatterShape s)
{
    switch (s) {
    case ScatterShape::None: return "none";
    case ScatterShape::Circle: return "circle";
    case ScatterShape::Square: return "square";
    case ScatterShape::Triangle: return "triangle";
    case ScatterShape::Cross: return "cross";
    case ScatterShape::Plus: return "plus";
    case ScatterShape::Diamond: return "diamond";
    }
    return "none";
}

}  // namespace gp
