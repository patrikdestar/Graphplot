// Standalone Graphplot application.
//   graphplot [data files...]
// Without arguments a small demo data set is shown.
#include "commands/PlotEditor.h"
#include "gui/FigureWindow.h"
#include "model/PlotModel.h"
#include "render/PlotRenderer.h"

#include <QApplication>
#include <QCommandLineParser>
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

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Graphplot");
    QApplication::setOrganizationName("Graphplot");

    QCommandLineParser parser;
    parser.setApplicationDescription("Interactive scientific plotting");
    parser.addHelpOption();
    QCommandLineOption shot("screenshot", "Render the window to <file> and exit.", "file");
    parser.addOption(shot);
    parser.addPositionalArgument("files", "Column data files to open (CSV/TSV/whitespace).");
    parser.process(app);

    FigureWindow w;
    const QStringList files = parser.positionalArguments();
    if (files.isEmpty()) {
        addDemoData(w);
    } else {
        for (const QString& f : files) {
            QString err;
            if (!w.openDataFile(f, &err)) QMessageBox::warning(&w, "Open data", err);
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
