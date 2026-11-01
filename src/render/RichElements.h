// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
// QCustomPlot elements that render rich text (see RichText.h).
#pragma once

#include "RichText.h"
#include "qcustomplot.h"

namespace gp {

// Text item anchored at one position (data or axis-rect coordinates), with
// optional frame/background. Used for annotations, marker labels and the
// in-frame corner label.
class RichTextItem : public QCPAbstractItem {
    Q_OBJECT
public:
    explicit RichTextItem(QCustomPlot* plot);

    QCPItemPosition* const position;

    void setText(const QString& text, const QFont& font, bool math);
    void setHtml(const QString& html, const QFont& font);
    QString text() const { return m_text; }
    void setColor(const QColor& c) { m_color = c; }
    void setPositionAlignment(Qt::Alignment a) { m_align = a; }
    void setPadding(const QMargins& m) { m_padding = m; }
    void setPen(const QPen& p) { m_pen = p; }
    void setBrush(const QBrush& b) { m_brush = b; }
    QRectF boxRect() const;  // pixels, including padding

    double selectTest(const QPointF& pos, bool onlySelectable, QVariant* details = nullptr) const override;

protected:
    void draw(QCPPainter* painter) override;

private:
    RichTextBlock m_block;
    QString m_text;
    QColor m_color = Qt::black;
    Qt::Alignment m_align = Qt::AlignLeft | Qt::AlignBottom;
    QMargins m_padding;
    QPen m_pen = Qt::NoPen;
    QBrush m_brush = Qt::NoBrush;
};

// Row above the axis rect: title (centred) plus optional left/right labels
// aligned with the frame edges (HEP style: "CMS Preliminary" ... "13 TeV").
class HeaderElement : public QCPLayoutElement {
    Q_OBJECT
public:
    HeaderElement(QCustomPlot* plot, QCPAxisRect* axisRect);
    void setTexts(const QString& title, const QFont& titleFont, const QColor& titleColor,
                  const QString& left, const QString& right, const QFont& sideFont,
                  const QColor& sideColor, bool math);
    bool isEmpty() const { return m_title.isEmpty() && m_left.isEmpty() && m_right.isEmpty(); }
    // Centre the title over the whole figure instead of the first panel's frame.
    void setCentreOnFigure(bool on) { m_centreOnFigure = on; }

protected:
    QSize minimumOuterSizeHint() const override;
    QSize maximumOuterSizeHint() const override;  // fixed height: never stretches
    void draw(QCPPainter* painter) override;

private:
    // True when left, title and right labels do not fit side by side above
    // the frame; the right label then goes on a second line.
    bool stacked() const;

    QCPAxisRect* m_axisRect;
    RichTextBlock m_title, m_left, m_right;
    bool m_centreOnFigure = false;
    QColor m_titleColor, m_sideColor;
};

// Draws rich-text axis labels for the bottom and left axes. QCustomPlot
// reserves the space (its own label is set to a blank of the right font),
// this layerable paints the formatted text into it.
class RichAxisLabels : public QCPLayerable {
    Q_OBJECT
public:
    RichAxisLabels(QCustomPlot* plot, QCPAxisRect* axisRect);
    void setLabels(const QString& x, const QString& y, const QFont& font, const QColor& color,
                   bool math);
    // Twin panels: the y label is drawn right of `outer`'s frame (the host's
    // right margin).
    void setYOnRight(QCPAxisRect* outer) { m_rightOuter = outer; }
    // Panel title above the frame (multi-panel figures; empty = none).
    void setTitle(const QString& title, const QFont& font, const QColor& color, bool math);
    QSizeF titleSize() const { return m_title.size(); }
    QSizeF xSize() const { return m_x.size(); }
    QSizeF ySize() const { return m_y.size(); }

protected:
    void applyDefaultAntialiasingHint(QCPPainter* painter) const override;
    void draw(QCPPainter* painter) override;

private:
    QCPAxisRect* m_axisRect;
    RichTextBlock m_x, m_y, m_title;
    QColor m_color, m_titleColor;
    QCPAxisRect* m_rightOuter = nullptr;
};

// Legend entry with a rich-text name.
class RichLegendItem : public QCPPlottableLegendItem {
    Q_OBJECT
public:
    RichLegendItem(QCPLegend* legend, QCPAbstractPlottable* plottable, bool math);

protected:
    void draw(QCPPainter* painter) override;
    QSize minimumOuterSizeHint() const override;

private:
    void layoutText() const;
    mutable RichTextBlock m_block;
    mutable QString m_laidOutName;
    mutable QFont m_laidOutFont;
    bool m_math;
};

}  // namespace gp
