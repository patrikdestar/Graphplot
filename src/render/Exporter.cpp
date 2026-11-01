#include "Exporter.h"

#include "PlotRenderer.h"
#include "qcustomplot.h"

#include <QFileInfo>
#include <QSvgGenerator>

namespace gp {

namespace {

bool exportSvg(QCustomPlot* plot, const QString& fileName, int w, int h)
{
    QSvgGenerator gen;
    gen.setFileName(fileName);
    gen.setSize(QSize(w, h));
    gen.setViewBox(QRect(0, 0, w, h));
    gen.setResolution(96);
    gen.setTitle(plot->window() ? plot->window()->windowTitle() : QString());
    gen.setDescription("Created with Graphplot");
    QCPPainter painter;
    if (!painter.begin(&gen)) return false;
    // Vectorized mode: no pixmap caching, no cosmetic-pen tricks that only
    // make sense on screen, so every line stays a true vector path.
    painter.setMode(QCPPainter::pmVectorized);
    painter.setMode(QCPPainter::pmNoCaching);
    plot->toPainter(&painter, w, h);
    painter.end();
    return true;
}

}  // namespace

bool exportFigure(PlotRenderer* renderer, const QString& fileName, const ExportOptions& opts,
                  QString* error)
{
    QCustomPlot* plot = renderer->plot();
    const QString ext = QFileInfo(fileName).suffix().toLower();
    const int w = opts.width > 0 ? opts.width : plot->width();
    const int h = opts.height > 0 ? opts.height : plot->height();

    // Selection highlights are an on-screen aid and must not end up in files.
    renderer->setExportMode(true);
    bool ok = false;
    if (ext == "png") {
        ok = plot->savePng(fileName, w, h, opts.scale > 0 ? opts.scale : 1.0, -1,
                           int(96 * (opts.scale > 0 ? opts.scale : 1.0)), QCP::ruDotsPerInch);
    } else if (ext == "pdf") {
        ok = plot->savePdf(fileName, w, h, QCP::epNoCosmetic, "Graphplot");
    } else if (ext == "svg") {
        ok = exportSvg(plot, fileName, w, h);
    } else {
        if (error) *error = QString("Unsupported export format '.%1' (use .png, .svg or .pdf)").arg(ext);
        renderer->setExportMode(false);
        return false;
    }
    renderer->setExportMode(false);
    renderer->replot();
    if (!ok && error) *error = QString("Could not write '%1'").arg(fileName);
    return ok;
}

}  // namespace gp
