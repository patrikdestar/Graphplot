// Draws all peaks (cross + label, optional leader line) in a single pass.
// One QCustomPlot item per peak would be too slow for hundreds of peaks;
// this layerable lives on the buffered "objects" layer, so dragging a peak
// or its label only repaints that layer.
#pragma once

#include "model/PlotModel.h"
#include "RichText.h"
#include "qcustomplot.h"

#include <QHash>
#include <memory>

namespace gp {

struct PeakAppearance {
    QColor color = QColor(0x0b, 0x0b, 0x0b);
    QColor labelColor = QColor(0x0b, 0x0b, 0x0b);
    QColor highlight = QColor(0x2a, 0x78, 0xd6);
    double crossSize = 7.0;   // half-length of the cross arms, pixels
    double lineWidth = 1.2;
    double fontSize = 9.0;    // points
    bool showLabels = true;
    QFont fontFamily;         // label font (family/size from the style)
    bool math = true;         // interpret $math$ in labels
};

class PeakOverlay : public QCPLayerable {
    Q_OBJECT
public:
    PeakOverlay(QCustomPlot* plot, QCPAxis* xAxis, QCPAxis* yAxis, PlotModel* model);

    void setAppearance(const PeakAppearance& a)
    {
        m_app = a;
        m_cache.clear();
    }
    const PeakAppearance& appearance() const { return m_app; }
    void setHighlight(ObjectId id) { m_highlight = id; }
    void setExportMode(bool on) { m_export = on; }

    struct Hit {
        ObjectId id = kInvalidId;
        bool onLabel = false;
    };
    Hit hitTest(const QPointF& px, double tol) const;
    QRectF labelRect(const Peak& p) const;  // in pixels

protected:
    void applyDefaultAntialiasingHint(QCPPainter* painter) const override;
    void draw(QCPPainter* painter) override;
    QRect clipRect() const override;

private:
    QPointF toPixel(const Peak& p) const;
    QFont labelFont() const;
    const RichTextBlock& block(const QString& label) const;

    QCPAxis* m_x;
    QCPAxis* m_y;
    PlotModel* m_model;
    PeakAppearance m_app;
    ObjectId m_highlight = kInvalidId;
    bool m_export = false;
    mutable QHash<QString, std::shared_ptr<RichTextBlock>> m_cache;  // laid-out labels
};

}  // namespace gp
