// Standalone Graphplot application.
// Copyright (c) 2026 Justinas Minkevicius <oompagoo@gmail.com>. All rights reserved.
// Proprietary - see LICENSE.
//   graphplot [data files...]
// Without arguments a small demo data set is shown.
#include "commands/PlotEditor.h"
#include "gui/FigureWindow.h"
#include "gui/Timeline.h"
#include "model/PlotModel.h"
#include "model/Style.h"
#include "render/PlotRenderer.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QMessageBox>
#include <QTimer>
#include <QUndoStack>
#include <cmath>

using namespace gp;

static void addDemoData(FigureWindow& w)
{
    constexpr int n = 4000;
    QVector<double> x(n), y1(n), y2(n);
    for (int i = 0; i < n; ++i) {
        x[i] = 100.0 * i / (n - 1);
        y1[i] = std::exp(-std::pow((x[i] - 30) / 4.0, 2)) + 0.6 * std::exp(-std::pow((x[i] - 62) / 2.5, 2)) +
                0.03 * std::sin(7.3 * x[i]);
        y2[i] = 0.5 * std::exp(-std::pow((x[i] - 45) / 9.0, 2)) - 0.2 + 0.002 * x[i];
    }
    Dataset a;
    a.name = "Spectrum A";
    a.x = x;
    a.y = y1;
    Dataset b;
    b.name = "Spectrum B";
    b.x = x;
    b.y = y2;
    w.editor()->addDataset(a);
    w.editor()->addDataset(b);
    AxesInfo info = w.model()->axesInfo();
    info.xLabel = "Chemical shift (ppm)";
    info.yLabel = "Intensity (a.u.)";
    w.model()->setAxesInfo(info);
    Marker m;
    m.position = 30.0;
    m.label = "Peak 1";
    w.editor()->addMarker(m);
    Marker h;
    h.orientation = Orientation::Horizontal;
    h.position = 0.5;
    h.label = "Half height";
    h.color = paletteColor(6);
    h.lineStyle = LineStyle::Dot;
    w.editor()->addMarker(h);
    Annotation note;
    note.x = 63.5;
    note.y = 0.66;
    note.text = "Shoulder";
    w.editor()->addAnnotation(note);
    w.editor()->setRegion({true, 55.0, 70.0});
    w.editor()->undoStack()->clear();  // demo setup is not user history
    w.renderer()->autoscale();
    w.renderer()->captureHomeView();
}

// Synthetic 2D spectrum: cross peaks of varying intensity, one negative
// peak and a little noise - useful to try contour level scrolling.
static void addDemo2D(FigureWindow& w)
{
    constexpr int n = 512;
    Matrix m;
    m.name = "2D spectrum";
    m.nx = m.ny = n;
    m.x0 = 0.0;
    m.x1 = 10.0;
    m.y0 = 0.0;
    m.y1 = 10.0;
    m.z.resize(n * n);
    struct Peak { double x, y, a, w; };
    const Peak peaks[] = {{2.0, 2.0, 1.0, 0.12}, {5.5, 5.5, 0.8, 0.12}, {8.0, 8.0, 0.6, 0.1},
                          {2.0, 5.5, 0.15, 0.1}, {5.5, 2.0, 0.15, 0.1}, {5.5, 8.0, 0.05, 0.1},
                          {8.0, 5.5, 0.05, 0.1}, {3.5, 7.0, -0.2, 0.15}};
    quint32 seed = 12345;
    for (int iy = 0; iy < n; ++iy)
        for (int ix = 0; ix < n; ++ix) {
            const double x = 10.0 * ix / (n - 1), y = 10.0 * iy / (n - 1);
            double z = 0;
            for (const Peak& p : peaks)
                z += p.a * std::exp(-((x - p.x) * (x - p.x) + (y - p.y) * (y - p.y)) / (2 * p.w * p.w));
            seed = seed * 1664525u + 1013904223u;
            z += 0.002 * ((seed >> 8) / double(1 << 24) - 0.5);
            m.z[iy * n + ix] = z;
        }
    w.editor()->addMatrix(m);
    AxesInfo info = w.model()->axesInfo();
    info.xLabel = "F2 (ppm)";
    info.yLabel = "F1 (ppm)";
    w.model()->setAxesInfo(info);
    w.editor()->undoStack()->clear();
    w.renderer()->autoscale();
    w.renderer()->captureHomeView();
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Graphplot");
    QApplication::setOrganizationName("Graphplot");
    QApplication::setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Interactive scientific plotting");
    parser.addHelpOption();
    QCommandLineOption shot("screenshot", "Render the window to <file> and exit.", "file");
    parser.addOption(shot);
    QCommandLineOption timelineOpt("timeline", "Open the CSV files as a timeline (stacked sensor rows).");
    parser.addOption(timelineOpt);
    QCommandLineOption perColumn("row-per-column", "Timeline: one row per column instead of per file.");
    parser.addOption(perColumn);
    QCommandLineOption demo2d("demo2d", "Show a synthetic 2D spectrum (contour demo).");
    parser.addOption(demo2d);
    parser.addPositionalArgument("files", "Column data files (CSV/TSV/whitespace), or Bruker 2D "
                                          "spectra (experiment folder, pdata/N folder or 2rr file).");
    parser.process(app);
    QStringList styleErrors;
    reloadDefaultStyle(&styleErrors);  // user default style file, if any
    for (const QString& e : styleErrors) qWarning("style: %s", qPrintable(e));

    FigureWindow w;
    const QStringList files = parser.positionalArguments();
    if (parser.isSet(timelineOpt)) {
        QString err;
        if (!w.timeline()->loadCsv(files, parser.isSet(perColumn), &err))
            QMessageBox::warning(&w, "Open time series", err);
        w.undoStack()->clear();
    } else if (parser.isSet(demo2d)) {
        addDemo2D(w);
    } else if (files.isEmpty()) {
        addDemoData(w);
    } else {
        for (const QString& f : files) {
            QString err;
            const QFileInfo fi(f);
            // Bruker: an experiment / pdata folder, or a 2rr file.
            const bool bruker = fi.isDir() || fi.fileName() == "2rr";
            const bool ok = bruker ? w.openBruker(f, &err) != kInvalidId : w.openDataFile(f, &err);
            if (!ok) QMessageBox::warning(&w, "Open data", err);
        }
        w.editor()->undoStack()->clear();
        w.renderer()->captureHomeView();
    }
    w.show();
    if (parser.isSet(shot)) {
        // Used for documentation and headless smoke tests.
        QTimer::singleShot(300, &w, [&] {
            const bool ok = w.grab().save(parser.value(shot));
            app.exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}
