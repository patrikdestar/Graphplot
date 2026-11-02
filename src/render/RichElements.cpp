// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "RichElements.h"

#include <cmath>

namespace gp {

// ---- RichTextItem ----------------------------------------------------------------

RichTextItem::RichTextItem(QCustomPlot* plot)
    : QCPAbstractItem(plot), position(createPosition(QLatin1String("position")))
{
    position->setCoords(0, 0);
}

void RichTextItem::setText(const QString& text, const QFont& font, bool math)
{
    m_text = text;
    m_block.set(text, font, math);
}

void RichTextItem::setHtml(const QString& html, const QFont& font)
{
    m_text = html;
    m_block.setHtml(html, font);
}

QRectF RichTextItem::boxRect() const
{
    const QSizeF s = m_block.size();
    const QRectF box(0, 0, s.width() + m_padding.left() + m_padding.right(),
                     s.height() + m_padding.top() + m_padding.bottom());
    const QPointF p = position->pixelPosition();
    double x = p.x(), y = p.y();
    if (m_align & Qt::AlignRight) x -= box.width();
    else if (m_align & Qt::AlignHCenter) x -= box.width() / 2;
    if (m_align & Qt::AlignBottom) y -= box.height();
    else if (m_align & Qt::AlignVCenter) y -= box.height() / 2;
    return box.translated(x, y);
}

double RichTextItem::selectTest(const QPointF& pos, bool onlySelectable, QVariant*) const
{
    if (onlySelectable && !mSelectable) return -1;
    const QRectF r = boxRect();
    if (r.contains(pos)) return 0;
    const double dx = std::max({r.left() - pos.x(), 0.0, pos.x() - r.right()});
    const double dy = std::max({r.top() - pos.y(), 0.0, pos.y() - r.bottom()});
    return std::hypot(dx, dy);
}

void RichTextItem::draw(QCPPainter* painter)
{
    if (m_text.isEmpty()) return;
    const QRectF r = boxRect();
    if (!r.intersects(clipRect())) return;
    if (m_pen.style() != Qt::NoPen || m_brush.style() != Qt::NoBrush) {
        painter->setPen(m_pen);
        painter->setBrush(m_brush);
        painter->drawRect(r);
    }
    m_block.draw(painter, r.topLeft() + QPointF(m_padding.left(), m_padding.top()), m_color);
}

// ---- HeaderElement -------------------------------------------------------------------

HeaderElement::HeaderElement(QCustomPlot* plot, QCPAxisRect* axisRect)
    : QCPLayoutElement(plot), m_axisRect(axisRect)
{
    setMargins(QMargins(0, 6, 0, 2));
}

void HeaderElement::setTexts(const QString& title, const QFont& titleFont, const QColor& titleColor,
                             const QString& left, const QString& right, const QFont& sideFont,
                             const QColor& sideColor, bool math)
{
    m_title.set(title, titleFont, math, Qt::AlignHCenter);
    m_left.set(left, sideFont, math);
    m_right.set(right, sideFont, math, Qt::AlignRight);
    m_titleColor = titleColor;
    m_sideColor = sideColor;
}

bool HeaderElement::stacked() const
{
    const double need = m_left.size().width() + m_right.size().width() + m_title.size().width() +
                        (m_title.isEmpty() ? 16 : 40);
    const int avail = m_axisRect->rect().width();
    return avail > 0 && !m_right.isEmpty() && need > avail;
}

QSize HeaderElement::minimumOuterSizeHint() const
{
    double h = std::max({m_title.size().height(), m_left.size().height(), m_right.size().height()});
    if (stacked())
        h = std::max(m_title.size().height(), m_left.size().height()) + m_right.size().height() + 2;
    return QSize(int(std::ceil(m_title.size().width())) + mMargins.left() + mMargins.right(),
                 int(std::ceil(h)) + mMargins.top() + mMargins.bottom());
}

QSize HeaderElement::maximumOuterSizeHint() const
{
    return QSize(QWIDGETSIZE_MAX, minimumOuterSizeHint().height());
}

void HeaderElement::draw(QCPPainter* painter)
{
    const QRect ar = m_axisRect->rect();
    const double bottom = mRect.bottom();
    // Side labels sit on the frame (aligned to its left/right edges), the
    // title is centred over the frame.
    const double leftLift = stacked() ? m_right.size().height() + 2 : 0;
    if (!m_left.isEmpty())
        m_left.draw(painter, QPointF(ar.left(), bottom - leftLift - m_left.size().height()), m_sideColor);
    if (!m_right.isEmpty())
        m_right.draw(painter,
                     QPointF(ar.right() - m_right.size().width(), bottom - m_right.size().height()),
                     m_sideColor);
    // When stacked, the left label and title move up one line.
    const double lift = stacked() ? m_right.size().height() + 2 : 0;
    // A figure-wide title (multi-panel figures) is centred over the figure.
    const double centre = m_centreOnFigure ? mRect.center().x() : ar.center().x();
    if (!m_title.isEmpty())
        m_title.draw(painter,
                     QPointF(centre - m_title.size().width() / 2,
                             bottom - lift - m_title.size().height()),
                     m_titleColor);
}

// ---- RichAxisLabels ----------------------------------------------------------------------

RichAxisLabels::RichAxisLabels(QCustomPlot* plot, QCPAxisRect* axisRect)
    : QCPLayerable(plot), m_axisRect(axisRect)
{
}

void RichAxisLabels::setLabels(const QString& x, const QString& y, const QFont& font,
                               const QColor& color, bool math)
{
    m_x.set(x, font, math);
    m_y.set(y, font, math);
    m_color = color;
}

void RichAxisLabels::setTitle(const QString& title, const QFont& font, const QColor& color, bool math)
{
    m_title.set(title, font, math);
    m_titleColor = color;
}

void RichAxisLabels::applyDefaultAntialiasingHint(QCPPainter* painter) const
{
    painter->setAntialiasing(true);
}

void RichAxisLabels::draw(QCPPainter* painter)
{
    const QRect ar = m_axisRect->rect();
    const QRect outer = m_axisRect->outerRect();
    // The label is the outermost element of the axis margin, so it sits
    // against the outer edge of the reserved space.
    if (!m_x.isEmpty()) {
        const QSizeF s = m_x.size();
        m_x.draw(painter, QPointF(ar.center().x() - s.width() / 2, outer.bottom() - s.height() - 1), m_color);
    }
    if (!m_y.isEmpty()) {
        const QSizeF s = m_y.size();
        if (m_rightOuter && m_rightBand > 0)
            m_y.drawVertical(painter, QPointF(m_rightOuter->rect().right() + m_rightBand - s.height() / 2 - 4,
                                              ar.center().y()), m_color);
        else if (m_rightOuter)
            m_y.drawVertical(painter, QPointF(m_rightOuter->outerRect().right() - s.height() / 2 - 1, ar.center().y()),
                             m_color);
        else if (s.width() <= 2.0 * std::min(ar.center().y() - outer.top(), outer.bottom() - ar.center().y())) {
            m_y.drawVertical(painter, QPointF(outer.left() + s.height() / 2 + 1, ar.center().y()), m_color);
        } else {
            // Longer than the room around the frame's centre (many stacked
            // timeline rows, short panels): smaller, and kept inside the panel
            // so neighbouring names never overlap.
            const double room = 2.0 * std::min(ar.center().y() - outer.top(), outer.bottom() - ar.center().y());
            const double f = std::max(0.55, (room - 2) / s.width());
            painter->save();
            painter->setClipRect(QRect(outer.left(), outer.top(), int(s.height()) + 4, outer.height()));
            painter->translate(outer.left() + s.height() * f / 2 + 1, ar.center().y());
            painter->scale(f, f);
            m_y.drawVertical(painter, QPointF(0, 0), m_color);
            painter->restore();
        }
    }
    if (!m_title.isEmpty()) {  // in the top margin (reserved by the top axis' label)
        const QSizeF s = m_title.size();
        m_title.draw(painter, QPointF(ar.center().x() - s.width() / 2, outer.top() + 1), m_titleColor);
    }
}

// ---- RichLegendItem ------------------------------------------------------------------------

RichLegendItem::RichLegendItem(QCPLegend* legend, QCPAbstractPlottable* plottable, bool math)
    : QCPPlottableLegendItem(legend, plottable), m_math(math)
{
}

void RichLegendItem::layoutText() const
{
    // Re-layout only when name or font changed (called on every repaint).
    const QString name = mPlottable ? mPlottable->name() : QString();
    const QFont font = getFont();
    if (name == m_laidOutName && font == m_laidOutFont && !m_block.isEmpty()) return;
    m_laidOutName = name;
    m_laidOutFont = font;
    m_block.set(name, font, m_math);
}

QSize RichLegendItem::minimumOuterSizeHint() const
{
    if (!mPlottable) return {};
    layoutText();
    const QSize icon = mParentLegend->iconSize();
    const QSizeF t = m_block.size();
    return QSize(icon.width() + mParentLegend->iconTextPadding() + int(std::ceil(t.width())) +
                     mMargins.left() + mMargins.right(),
                 std::max(int(std::ceil(t.height())), icon.height()) + mMargins.top() + mMargins.bottom());
}

void RichLegendItem::draw(QCPPainter* painter)
{
    if (!mPlottable) return;
    layoutText();
    const QSize iconSize = mParentLegend->iconSize();
    const QSizeF t = m_block.size();
    // The icon is drawn by the base class (drawLegendIcon is only accessible
    // to QCPPlottableLegendItem itself); an empty name suppresses its text.
    const QString name = mPlottable->name();
    mPlottable->setName(QString());
    QCPPlottableLegendItem::draw(painter);
    mPlottable->setName(name);
    m_block.draw(painter,
                 QPointF(mRect.x() + iconSize.width() + mParentLegend->iconTextPadding(),
                         mRect.y() + std::max(0.0, (iconSize.height() - t.height()) / 2)),
                 getTextColor());
}

}  // namespace gp
