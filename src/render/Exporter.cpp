// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "Exporter.h"

#include "PlotRenderer.h"
#include "qcustomplot.h"

#include <QFileInfo>
#include <QPageSize>
#include <QPdfWriter>
#include <QSvgGenerator>

namespace gp {

namespace {

// One physical scale for every format: 100 figure pixels per inch (the
// pyplot convention), so an 800 px wide figure is 8 inches as PNG
// (metadata), SVG and PDF alike.
constexpr int kDpi = 100;

void prepareVectorPainter(QCPPainter& painter)
{
    // Vectorized mode: no pixmap caching, no cosmetic-pen tricks that only
    // make sense on screen, so every line stays a true vector path.
    painter.setMode(QCPPainter::pmVectorized);
    painter.setMode(QCPPainter::pmNoCaching);
    painter.setMode(QCPPainter::pmNonCosmetic);
}

bool exportPdf(QCustomPlot* plot, const QString& fileName, int w, int h)
{
    QPdfWriter pdf(fileName);
    pdf.setCreator("Graphplot");
    pdf.setTitle(plot->window() ? plot->window()->windowTitle() : QString());
    pdf.setResolution(kDpi);  // one device pixel = one figure pixel
    pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
    pdf.setPageSize(QPageSize(QSizeF(w * 72.0 / kDpi, h * 72.0 / kDpi), QPageSize::Point, QString(),
                              QPageSize::ExactMatch));
    QCPPainter painter;
    if (!painter.begin(&pdf)) return false;
    prepareVectorPainter(painter);
    plot->toPainter(&painter, w, h);
    painter.end();
    return true;
}

bool exportSvg(QCustomPlot* plot, const QString& fileName, int w, int h)
{
    QSvgGenerator gen;
    gen.setFileName(fileName);
    gen.setSize(QSize(w, h));
    gen.setViewBox(QRect(0, 0, w, h));
    gen.setResolution(kDpi);
    gen.setTitle(plot->window() ? plot->window()->windowTitle() : QString());
    gen.setDescription("Created with Graphplot");
    QCPPainter painter;
    if (!painter.begin(&gen)) return false;
    prepareVectorPainter(painter);
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
                           int(kDpi * (opts.scale > 0 ? opts.scale : 1.0)), QCP::ruDotsPerInch);
    } else if (ext == "jpg" || ext == "jpeg") {
        ok = plot->saveJpg(fileName, w, h, opts.scale > 0 ? opts.scale : 1.0, 95,
                           int(kDpi * (opts.scale > 0 ? opts.scale : 1.0)), QCP::ruDotsPerInch);
    } else if (ext == "pdf") {
        ok = exportPdf(plot, fileName, w, h);
    } else if (ext == "svg") {
        ok = exportSvg(plot, fileName, w, h);
    } else {
        if (error) *error = QString("Unsupported export format '.%1' (use .png, .jpg, .svg or .pdf)").arg(ext);
        renderer->setExportMode(false);
        return false;
    }
    renderer->setExportMode(false);
    renderer->replot();
    if (!ok && error) *error = QString("Could not write '%1'").arg(fileName);
    return ok;
}

}  // namespace gp
