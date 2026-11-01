// QCustomPlot plottable that draws contour lines of a 2D grid.
//
// QCustomPlot has a colour-map plottable but no contour plot, so this class
// renders the marching-squares segments (model/Contour) directly with
// QCPPainter. Contours are recomputed only when the grid or the level set
// changes; panning/zooming just re-projects the cached segments.
#pragma once

#include "model/Contour.h"
#include "qcustomplot.h"

namespace gp {

class ContourPlottable : public QCPAbstractPlottable {
    Q_OBJECT
public:
    ContourPlottable(QCPAxis* keyAxis, QCPAxis* valueAxis);

    void setMatrix(const Matrix& m);          // grid + style
    void setContourStyle(const ContourStyle& s);
    const std::vector<ContourLines>& lines() const { return m_lines; }
    qsizetype segmentCount() const;

    double selectTest(const QPointF& pos, bool onlySelectable,
                      QVariant* details = nullptr) const override;
    QCPRange getKeyRange(bool& foundRange,
                         QCP::SignDomain inSignDomain = QCP::sdBoth) const override;
    QCPRange getValueRange(bool& foundRange, QCP::SignDomain inSignDomain = QCP::sdBoth,
                           const QCPRange& inKeyRange = QCPRange()) const override;

protected:
    void draw(QCPPainter* painter) override;
    void drawLegendIcon(QCPPainter* painter, const QRectF& rect) const override;

private:
    void recompute();

    ContourGrid m_grid;
    ContourStyle m_style;
    std::vector<ContourLines> m_lines;
    double m_x0 = 0, m_x1 = 1, m_y0 = 0, m_y1 = 1;
    bool m_hasData = false;
};

}  // namespace gp
