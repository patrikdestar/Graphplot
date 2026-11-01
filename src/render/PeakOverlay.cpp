// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "PeakOverlay.h"

#include <cmath>

namespace gp {

PeakOverlay::PeakOverlay(QCustomPlot* plot, QCPAxis* xAxis, QCPAxis* yAxis, PlotModel* model)
    : QCPLayerable(plot), m_x(xAxis), m_y(yAxis), m_model(model)
{
}

void PeakOverlay::applyDefaultAntialiasingHint(QCPPainter* painter) const
{
    painter->setAntialiasing(true);
}

QRect PeakOverlay::clipRect() const { return m_x->axisRect()->rect(); }

QPointF PeakOverlay::toPixel(const Peak& p) const
{
    return {m_x->coordToPixel(p.x), m_y->coordToPixel(p.y)};
}

QFont PeakOverlay::labelFont() const
{
    QFont f = m_app.fontFamily.family().isEmpty() ? mParentPlot->font() : m_app.fontFamily;
    f.setPointSizeF(m_app.fontSize);
    return f;
}

const RichTextBlock& PeakOverlay::block(const QString& label) const
{
    auto it = m_cache.find(label);
    if (it == m_cache.end()) {
        if (m_cache.size() > 2000) m_cache.clear();
        auto b = std::make_shared<RichTextBlock>();
        b->set(label, labelFont(), m_app.math);
        it = m_cache.insert(label, b);
    }
    return **it;
}

QRectF PeakOverlay::labelRect(const Peak& p) const
{
    if (p.label.isEmpty()) return {};
    const QSizeF text = block(p.label).size();
    // The offset is the label's bottom-left corner relative to the peak.
    const QPointF anchor = toPixel(p) + p.labelOffset;
    return QRectF(anchor.x(), anchor.y() - text.height(), text.width(), text.height());
}

PeakOverlay::Hit PeakOverlay::hitTest(const QPointF& px, double tol) const
{
    Hit best;
    double bestDist = tol;
    const auto& peaks = m_model->peaks();
    for (auto it = peaks.rbegin(); it != peaks.rend(); ++it) {
        const QPointF c = toPixel(*it);
        const double d = std::hypot(px.x() - c.x(), px.y() - c.y());
        if (d <= std::max(bestDist, m_app.crossSize)) {
            if (d < bestDist || best.id == kInvalidId) {
                bestDist = d;
                best = {it->id, false};
            }
        }
    }
    if (best.id != kInvalidId) return best;
    if (m_app.showLabels) {
        for (auto it = peaks.rbegin(); it != peaks.rend(); ++it)
            if (labelRect(*it).adjusted(-2, -2, 2, 2).contains(px)) return {it->id, true};
    }
    return {};
}

void PeakOverlay::draw(QCPPainter* painter)
{
    const auto& peaks = m_model->peaks();
    if (peaks.empty()) return;
    const QRect clip = clipRect();
    for (const Peak& p : peaks) {
        const QPointF c = toPixel(p);
        if (!clip.adjusted(-50, -50, 50, 50).contains(c.toPoint())) continue;
        const bool hl = !m_export && p.id == m_highlight;
        const QColor col = hl ? m_app.highlight : m_app.color;
        QPen pen(col, m_app.lineWidth + (hl ? 0.8 : 0.0));
        pen.setCapStyle(Qt::FlatCap);
        painter->setPen(pen);
        const double s = m_app.crossSize;
        painter->drawLine(QPointF(c.x() - s, c.y()), QPointF(c.x() + s, c.y()));
        painter->drawLine(QPointF(c.x(), c.y() - s), QPointF(c.x(), c.y() + s));
        if (!m_app.showLabels || p.label.isEmpty()) continue;
        const QRectF r = labelRect(p);
        // Leader line when the label was moved away from the peak.
        if (std::hypot(p.labelOffset.x(), p.labelOffset.y()) > 3 * s) {
            QPen lp(col, 0.8);
            painter->setPen(lp);
            const QPointF target(std::clamp(c.x(), r.left(), r.right()),
                                 std::clamp(c.y(), r.top(), r.bottom()));
            painter->drawLine(c, target);
        }
        // White halo keeps the label readable on top of contours.
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 255, 255, 200));
        painter->drawRect(r.adjusted(-1, 0, 1, 0));
        painter->setBrush(Qt::NoBrush);
        block(p.label).draw(painter, r.topLeft(), hl ? m_app.highlight : m_app.labelColor);
    }
}

}  // namespace gp
