// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
//
// Colour maps for 2D images, and the colour bar drawn beside a panel.
//
// Colour maps: "auto" (white -> blue, or blue-white-red around zero for
// signed data), the perceptually uniform "viridis", "magma", "inferno",
// "plasma", "cividis", and "gray", "blues", "rdbu" (diverging). A "_r"
// suffix reverses any of them.
//
// The colour bar is drawn in the panel's right margin (the renderer reserves
// the space), with nice tick values and an optional rotated label.
#pragma once

#include "qcustomplot.h"
#include "RichText.h"

#include <QStringList>

namespace gp {

QStringList colormapNames();  // without the "_r" variants
// Gradient of a colour map; returns false (and the "auto" map) for unknown names.
bool colormapGradient(const QString& name, bool signedData, QCPColorGradient* out);
// Diverging maps are centred on zero (symmetric data range).
bool colormapIsDiverging(const QString& name, bool signedData);

class ColorBar : public QCPLayerable {
    Q_OBJECT
public:
    ColorBar(QCustomPlot* plot, QCPAxisRect* axisRect);
    // Space between the plot area and the bar (a twin panel's right axis).
    void setOffset(int px) { m_offset = px; }

    void set(const QCPColorGradient& gradient, const QCPRange& range, const QString& label, const QFont& font,
             const QColor& edge, const QColor& text, bool math);
    // Horizontal space the bar needs to the right of the axis rect (px).
    int requiredWidth() const;

protected:
    void applyDefaultAntialiasingHint(QCPPainter* painter) const override;
    void draw(QCPPainter* painter) override;

private:
    QList<double> ticks() const;
    QString tickText(double v) const;

    QCPAxisRect* m_rect;
    QCPColorGradient m_gradient;
    QCPRange m_range;
    QFont m_font;
    QColor m_edge, m_text;
    RichTextBlock m_label;
    static constexpr int kGap = 10, kBarWidth = 14, kTick = 4;
    int m_offset = 0;
};

}  // namespace gp
