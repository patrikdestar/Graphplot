// Figure export to PNG (raster), SVG and PDF (vector).
#pragma once

#include <QString>

namespace gp {

class PlotRenderer;

struct ExportOptions {
    int width = 0;      // logical pixels; 0 = current widget size
    int height = 0;
    double scale = 2.0; // PNG only: pixel density multiplier (2 = "retina")
};

// Format is chosen from the file extension (.png, .svg, .pdf).
bool exportFigure(PlotRenderer* renderer, const QString& fileName, const ExportOptions& opts = {},
                  QString* error = nullptr);

}  // namespace gp
