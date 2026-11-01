// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "ColorBar.h"

#include <cmath>

namespace gp {

namespace {

struct Map {
    const char* name;
    std::initializer_list<const char*> stops;
};

// Stops sampled from the published maps (matplotlib's viridis family, ColorBrewer).
const Map kMaps[] = {
    {"viridis", {"#440154", "#482878", "#3e4989", "#31688e", "#26828e", "#1f9e89", "#35b779", "#6ece58", "#b5de2b", "#fde725"}},
    {"magma", {"#000004", "#1c1044", "#4f127b", "#812581", "#b5367a", "#e55964", "#fb8761", "#fec287", "#fcfdbf"}},
    {"inferno", {"#000004", "#1f0c48", "#550f6d", "#88226a", "#ba3655", "#e35933", "#f98c0a", "#f9c932", "#fcffa4"}},
    {"plasma", {"#0d0887", "#46039f", "#7201a8", "#9c179e", "#bd3786", "#d8576b", "#ed7953", "#fb9f3a", "#fdca26", "#f0f921"}},
    {"cividis", {"#00224e", "#123570", "#3b496c", "#575d6d", "#707173", "#8a8678", "#a59c74", "#c3b369", "#e1cc55", "#fee838"}},
    {"gray", {"#000000", "#ffffff"}},
    {"greys", {"#ffffff", "#d9d9d9", "#969696", "#525252", "#000000"}},
    {"reds", {"#fff5f0", "#fcbba1", "#fb6a4a", "#cb181d", "#67000d"}},
    {"greens", {"#f7fcf5", "#c7e9c0", "#74c476", "#238b45", "#00441b"}},
    {"oranges", {"#fff5eb", "#fdd0a2", "#fd8d3c", "#d94801", "#7f2704"}},
    {"purples", {"#fcfbfd", "#dadaeb", "#9e9ac8", "#6a51a3", "#3f007d"}},
    {"hot", {"#0b0000", "#ff0000", "#ffff00", "#ffffff"}},
    {"turbo", {"#30123b", "#4662d7", "#36aaf9", "#1ae4b6", "#72fe5e", "#c8ef34", "#faba39", "#f66b19", "#ca2a04", "#7a0403"}},
    {"jet", {"#000080", "#0000ff", "#00ffff", "#ffff00", "#ff0000", "#800000"}},
    {"coolwarm", {"#3b4cc0", "#7396f5", "#b0cbfc", "#dddcdc", "#f6bfa6", "#ea7b60", "#b40426"}},
    {"bwr", {"#0000ff", "#ffffff", "#ff0000"}},
    {"seismic", {"#00004c", "#0000ff", "#ffffff", "#ff0000", "#7f0000"}},
    {"blues", {"#f7fbff", "#c6dbef", "#6baed6", "#2171b5", "#08306b"}},
    // As matplotlib's RdBu: red for low values, blue for high ones ("rdbu_r" is the reverse).
    {"rdbu", {"#67001f", "#b2182b", "#d6604d", "#f4a582", "#f7f7f7", "#92c5de", "#4393c3", "#2166ac", "#053061"}},
};

}  // namespace

QStringList colormapNames()
{
    QStringList out{"auto"};
    for (const Map& m : kMaps) out << m.name;
    return out;
}

bool colormapIsDiverging(const QString& nameIn, bool signedData)
{
    QString name = nameIn.trimmed().toLower();
    if (name.endsWith("_r")) name.chop(2);
    // Centred on zero only for data with both signs (otherwise the range is
    // the data's, and half the map would be unused).
    return signedData && (name == "rdbu" || name == "coolwarm" || name == "bwr" || name == "seismic" ||
                          name.isEmpty() || name == "auto");
}

bool colormapGradient(const QString& nameIn, bool signedData, QCPColorGradient* out)
{
    QString name = nameIn.trimmed().toLower();
    const bool reversed = name.endsWith("_r");
    if (reversed) name.chop(2);
    QList<QColor> stops;
    bool known = name.isEmpty() || name == "auto";
    if (known) {  // the original look: white -> blue, or blue-white-red around zero
        stops = signedData ? QList<QColor>{QColor(0x2a, 0x78, 0xd6), Qt::white, QColor(0xe3, 0x49, 0x48)}
                           : QList<QColor>{Qt::white, QColor(0x2a, 0x78, 0xd6)};
    }
    for (const Map& m : kMaps)
        if (name == m.name) {
            known = true;
            for (const char* c : m.stops) stops << QColor(c);
        }
    if (!known) {
        colormapGradient("auto", signedData, out);
        return false;
    }
    if (reversed) std::reverse(stops.begin(), stops.end());
    QCPColorGradient g;
    g.clearColorStops();
    for (int i = 0; i < stops.size(); ++i) g.setColorStopAt(double(i) / (stops.size() - 1), stops[i]);
    g.setColorInterpolation(QCPColorGradient::ciRGB);
    *out = g;
    return true;
}

// ---- colour bar -----------------------------------------------------------------

ColorBar::ColorBar(QCustomPlot* plot, QCPAxisRect* axisRect) : QCPLayerable(plot), m_rect(axisRect) {}

void ColorBar::set(const QCPColorGradient& gradient, const QCPRange& range, const QString& label,
                   const QFont& font, const QColor& edge, const QColor& text, bool math)
{
    m_gradient = gradient;
    m_range = range;
    m_font = font;
    m_edge = edge;
    m_text = text;
    m_label.set(label, font, math);
}

QList<double> ColorBar::ticks() const
{
    // About five "nice" values (1, 2, 5 x 10^n steps) inside the range.
    QList<double> out;
    const double span = m_range.upper - m_range.lower;
    if (!(span > 0) || !std::isfinite(span)) return out;
    // A range of a few rounding steps (nearly constant data) has no useful
    // ticks; v += step would not even advance. Show the value once.
    const double scale = std::max(std::abs(m_range.lower), std::abs(m_range.upper));
    if (span < scale * 1e-12) return {m_range.lower};
    const double raw = span / 5;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double step = raw / mag < 1.5 ? mag : raw / mag < 3.5 ? 2 * mag : raw / mag < 7.5 ? 5 * mag : 10 * mag;
    const double first = std::ceil(m_range.lower / step) * step;
    for (int i = 0; i < 50; ++i) {  // an integer counter: always terminates
        const double v = first + i * step;
        if (v > m_range.upper + step * 1e-9) break;
        out << (std::abs(v) < step * 1e-9 ? 0.0 : v);
    }
    return out;
}

QString ColorBar::tickText(double v) const
{
    return QString::number(v, 'g', 4).replace('-', QChar(0x2212));
}

int ColorBar::requiredWidth() const
{
    const QFontMetrics fm(m_font);
    int labels = 0;
    for (double v : ticks()) labels = std::max(labels, fm.horizontalAdvance(tickText(v)));
    const int title = m_label.isEmpty() ? 0 : int(std::ceil(m_label.size().height())) + 6;
    return kGap + kBarWidth + kTick + 3 + labels + title + 12;  // + space before a neighbouring panel
}

void ColorBar::applyDefaultAntialiasingHint(QCPPainter* painter) const
{
    applyAntialiasingHint(painter, false, QCP::aeNone);
}

void ColorBar::draw(QCPPainter* painter)
{
    const QRect ar = m_rect->rect();
    if (!ar.isValid() || !(m_range.upper > m_range.lower)) return;
    const QRect bar(ar.right() + kGap, ar.top(), kBarWidth, ar.height());
    // The gradient, one line per pixel (top = maximum).
    for (int y = 0; y < bar.height(); ++y) {
        const double v = m_range.upper - (m_range.upper - m_range.lower) * (y + 0.5) / bar.height();
        painter->setPen(QColor::fromRgba(m_gradient.color(v, m_range)));
        painter->drawLine(QLineF(bar.left(), bar.top() + y + 0.5, bar.right() + 1, bar.top() + y + 0.5));
    }
    painter->setPen(QPen(m_edge, 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(bar.adjusted(0, 0, -1, -1));
    // Ticks and values.
    painter->setFont(m_font);
    const QFontMetrics fm(m_font);
    int labelRight = bar.right();
    for (double v : ticks()) {
        const double y = bar.bottom() - (v - m_range.lower) / (m_range.upper - m_range.lower) * bar.height();
        painter->setPen(QPen(m_edge, 1));
        painter->drawLine(QPointF(bar.right(), y), QPointF(bar.right() + kTick, y));
        painter->setPen(m_text);
        const QString t = tickText(v);
        const int x = bar.right() + kTick + 3;
        painter->drawText(QPointF(x, y + fm.ascent() / 2.0 - 1), t);
        labelRight = std::max(labelRight, x + fm.horizontalAdvance(t));
    }
    // Rotated label to the right of the values.
    if (!m_label.isEmpty())
        m_label.drawVertical(painter, QPointF(labelRight + 6 + m_label.size().height() / 2, ar.center().y()), m_text);
}

}  // namespace gp
