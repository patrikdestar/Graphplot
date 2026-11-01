// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "ContourPlottable.h"

#include <algorithm>

namespace gp {

ContourPlottable::ContourPlottable(QCPAxis* keyAxis, QCPAxis* valueAxis)
    : QCPAbstractPlottable(keyAxis, valueAxis)
{
    setSelectable(QCP::stNone);
    setAntialiased(true);
}

void ContourPlottable::setMatrix(const Matrix& m)
{
    m_grid.setMatrix(m);
    m_hasData = !m_grid.isEmpty();
    m_x0 = m.x0;
    m_x1 = m.x1;
    m_y0 = m.y0;
    m_y1 = m.y1;
    m_style = m.style;
    recompute();
}

void ContourPlottable::setContourStyle(const ContourStyle& s)
{
    const bool levelsChanged = s.baseLevel != m_style.baseLevel || s.factor != m_style.factor ||
                               s.levelCount != m_style.levelCount ||
                               s.showNegative != m_style.showNegative;
    m_style = s;
    if (levelsChanged) recompute();
}

void ContourPlottable::recompute() { m_lines = m_grid.compute(contourLevels(m_style)); }

qsizetype ContourPlottable::segmentCount() const
{
    qsizetype n = 0;
    for (const auto& l : m_lines) n += l.segments.size();
    return n;
}

double ContourPlottable::selectTest(const QPointF&, bool, QVariant*) const { return -1; }

QCPRange ContourPlottable::getKeyRange(bool& foundRange, QCP::SignDomain) const
{
    foundRange = m_hasData;
    return QCPRange(std::min(m_x0, m_x1), std::max(m_x0, m_x1));
}

QCPRange ContourPlottable::getValueRange(bool& foundRange, QCP::SignDomain, const QCPRange&) const
{
    foundRange = m_hasData;
    return QCPRange(std::min(m_y0, m_y1), std::max(m_y0, m_y1));
}

void ContourPlottable::draw(QCPPainter* painter)
{
    if (m_lines.empty()) return;
    QCPAxis* kx = keyAxis();
    QCPAxis* vy = valueAxis();
    if (!kx || !vy) return;
    applyDefaultAntialiasingHint(painter);

    // Linear axes: project with one affine map instead of per-point calls.
    const bool linear = kx->scaleType() == QCPAxis::stLinear && vy->scaleType() == QCPAxis::stLinear;
    const double ax = kx->coordToPixel(1.0) - kx->coordToPixel(0.0), bx = kx->coordToPixel(0.0);
    const double ay = vy->coordToPixel(1.0) - vy->coordToPixel(0.0), by = vy->coordToPixel(0.0);
    auto project = [&](const QPointF& p) {
        return linear ? QPointF(ax * p.x() + bx, ay * p.y() + by)
                      : QPointF(kx->coordToPixel(p.x()), vy->coordToPixel(p.y()));
    };

    // Clip to the visible data range to avoid projecting off-screen segments.
    const QCPRange xr = kx->range(), yr = vy->range();
    QVector<QLineF> px;
    for (const ContourLines& level : m_lines) {
        if (level.segments.isEmpty()) continue;
        px.clear();
        px.reserve(level.segments.size());
        for (const QLineF& s : level.segments) {
            const double sx0 = std::min(s.x1(), s.x2()), sx1 = std::max(s.x1(), s.x2());
            const double sy0 = std::min(s.y1(), s.y2()), sy1 = std::max(s.y1(), s.y2());
            if (sx1 < xr.lower || sx0 > xr.upper || sy1 < yr.lower || sy0 > yr.upper) continue;
            px.push_back(QLineF(project(s.p1()), project(s.p2())));
        }
        QPen pen(level.negative ? m_style.negativeColor : m_style.positiveColor, m_style.lineWidth);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->drawLines(px);
    }
}

void ContourPlottable::drawLegendIcon(QCPPainter* painter, const QRectF& rect) const
{
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(m_style.positiveColor, 1.2));
    const QPointF c = rect.center();
    painter->drawEllipse(c, rect.width() * 0.45, rect.height() * 0.45);
    painter->drawEllipse(c, rect.width() * 0.25, rect.height() * 0.22);
    if (m_style.showNegative) {
        painter->setPen(QPen(m_style.negativeColor, 1.2));
        painter->drawEllipse(c, rect.width() * 0.08, rect.height() * 0.08);
    }
}

}  // namespace gp
