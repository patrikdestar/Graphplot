// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
// Unit and interaction tests. Run headless with QT_QPA_PLATFORM=offscreen
// (ctest sets this automatically).
#include "commands/PlotEditor.h"
#include "gui/Appearance.h"
#include "gui/FigureWindow.h"
#include "gui/Timeline.h"
#include "model/DataExport.h"
#include "model/DataLoader.h"
#include "model/PlotModel.h"
#include "qcustomplot.h"
#include "model/BrukerReader.h"
#include "model/Contour.h"
#include "model/PeakPicker.h"
#include "model/Statistics.h"
#include "model/Style.h"
#include "model/TimeSeriesLoader.h"
#include "render/RichText.h"
#include "render/PeakOverlay.h"
#include "render/ContourPlottable.h"
#include "render/DisplayTheme.h"
#include "render/PlotRenderer.h"

#include <QElapsedTimer>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QToolButton>
#include <QAbstractSpinBox>
#include <QHelpEvent>
#include <QStandardPaths>
#include <QImage>
#include <QLineEdit>
#include <QMimeData>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUndoStack>
#include <cmath>

using namespace gp;

namespace {

Dataset ramp(const QString& name, int n = 101, double slope = 1.0)
{
    Dataset d;
    d.name = name;
    for (int i = 0; i < n; ++i) {
        d.x.push_back(i);
        d.y.push_back(slope * i);
    }
    return d;
}

// Gaussian peak (positive) and a weaker negative one on [-1,1]^2.
Matrix peaks(int n)
{
    Matrix m;
    m.name = "peaks";
    m.nx = m.ny = n;
    m.x0 = m.y0 = -1;
    m.x1 = m.y1 = 1;
    m.z.resize(qsizetype(n) * n);
    for (int iy = 0; iy < n; ++iy)
        for (int ix = 0; ix < n; ++ix) {
            const double x = -1 + 2.0 * ix / (n - 1), y = -1 + 2.0 * iy / (n - 1);
            m.z[qsizetype(iy) * n + ix] = std::exp(-((x - 0.3) * (x - 0.3) + y * y) / 0.05) -
                                          0.5 * std::exp(-((x + 0.4) * (x + 0.4) + y * y) / 0.03);
        }
    return m;
}

}  // namespace

class TestGraphplot : public QObject {
    Q_OBJECT

private:
    // A shown window with data x,y in [0,100], view [0,100]x[0,100].
    std::unique_ptr<FigureWindow> makeWindow()
    {
        auto w = std::make_unique<FigureWindow>();
        w->editor()->addDataset(ramp("ramp"));
        w->resize(900, 600);
        w->show();
        if (!QTest::qWaitForWindowExposed(w.get())) qWarning("window not exposed");
        w->renderer()->setViewRange({0, 100, 0, 100});
        w->plot()->replot();
        return w;
    }
    QPoint px(FigureWindow* w, double x, double y)
    {
        return w->renderer()->dataToPixel({x, y}).toPoint();
    }
    void drag(QWidget* widget, QPoint from, QPoint to, Qt::KeyboardModifiers mods = {},
              Qt::MouseButton button = Qt::LeftButton)
    {
        QTest::mousePress(widget, button, mods, from);
        const int steps = 8;
        for (int i = 1; i <= steps; ++i)
            QTest::mouseMove(widget, from + (to - from) * i / steps);
        QTest::mouseRelease(widget, button, mods, to);
    }

private slots:
    // Settings written by tests (theme, tooltips) go to a test location, not
    // to the user's configuration.
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    // ---- model --------------------------------------------------------------
    void paletteColorsAreDistinct()
    {
        PlotModel model;
        PlotEditor ed(&model);
        const ObjectId a = ed.addDataset(ramp("a"));
        const ObjectId b = ed.addDataset(ramp("b"));
        QVERIFY(model.dataset(a)->style.color.isValid());
        QVERIFY(model.dataset(a)->style.color != model.dataset(b)->style.color);
    }

    void mismatchedLengthsAreTruncated()
    {
        PlotModel model;
        Dataset d;
        d.x = {1, 2, 3};
        d.y = {1, 2};
        const ObjectId id = model.insertDataset(d);
        QCOMPARE(model.dataset(id)->x.size(), 2);
    }

    void regionIsNormalised()
    {
        PlotModel model;
        model.setRegion({true, 5, 2});
        QCOMPARE(model.region().xmin, 2.0);
        QCOMPARE(model.region().xmax, 5.0);
    }

    // ---- undo / redo ------------------------------------------------------------
    void undoRedoDataset()
    {
        PlotModel model;
        PlotEditor ed(&model);
        const ObjectId a = ed.addDataset(ramp("a"));
        const ObjectId b = ed.addDataset(ramp("b", 11));
        ed.removeDataset(a);
        QCOMPARE(int(model.datasets().size()), 1);
        ed.undo();  // restores at its original index with its data
        QCOMPARE(int(model.datasets().size()), 2);
        QCOMPARE(model.datasets()[0].id, a);
        QCOMPARE(model.dataset(a)->x.size(), 101);
        ed.undo();  // undo add b
        QVERIFY(!model.dataset(b));
        ed.redo();
        QVERIFY(model.dataset(b));
        QCOMPARE(model.dataset(b)->x.size(), 11);
    }

    void appearanceEditsMerge()
    {
        PlotModel model;
        PlotEditor ed(&model);
        const ObjectId a = ed.addDataset(ramp("a"));
        const int before = ed.undoStack()->count();
        DatasetStyle s = model.dataset(a)->style;
        for (double w : {2.0, 2.5, 3.0}) {
            s.lineWidth = w;
            ed.setDatasetAppearance(a, "a", s, /*mergeable=*/true);
        }
        QCOMPARE(ed.undoStack()->count(), before + 1);
        ed.undo();
        QCOMPARE(model.dataset(a)->style.lineWidth, 1.5);
    }

    void undoRedoMarker()
    {
        PlotModel model;
        PlotEditor ed(&model);
        Marker m;
        m.position = 10;
        const ObjectId id = ed.addMarker(m);
        Marker moved = *model.marker(id);
        moved.position = 20;
        moved.color = Qt::green;
        ed.updateMarker(moved);
        QCOMPARE(model.marker(id)->position, 20.0);
        ed.undo();
        QCOMPARE(model.marker(id)->position, 10.0);
        ed.removeMarker(id);
        QVERIFY(!model.marker(id));
        ed.undo();
        QVERIFY(model.marker(id));  // same id after undo of removal
        ed.undo();
        ed.undo();
        QVERIFY(model.markers().empty());
        ed.redo();
        QCOMPARE(model.marker(id)->position, 10.0);
    }

    void undoRedoAnnotationAndRegion()
    {
        PlotModel model;
        PlotEditor ed(&model);
        Annotation a;
        a.text = "peak";
        const ObjectId id = ed.addAnnotation(a);
        Annotation e = *model.annotation(id);
        e.text = "big peak";
        ed.updateAnnotation(e);
        ed.setRegion({true, 1, 2});
        ed.setRegion({true, 1, 3});
        ed.clearRegion();
        QVERIFY(!model.region().active);
        ed.undo();
        QCOMPARE(model.region().xmax, 3.0);
        ed.undo();
        QCOMPARE(model.region().xmax, 2.0);
        ed.undo();
        QVERIFY(!model.region().active);
        ed.undo();
        QCOMPARE(model.annotation(id)->text, QString("peak"));
    }

    void dataUpdateUndoableOrNot()
    {
        PlotModel model;
        PlotEditor ed(&model);
        const ObjectId id = ed.addDataset(ramp("a", 5));
        ed.setDatasetData(id, {0, 1}, {5, 6});
        QCOMPARE(model.dataset(id)->y.size(), 2);
        ed.undo();
        QCOMPARE(model.dataset(id)->y.size(), 5);
        const int count = ed.undoStack()->count();
        ed.setDatasetData(id, {0}, {1}, /*undoable=*/false);
        QCOMPARE(ed.undoStack()->count(), count);
        QCOMPARE(model.dataset(id)->y.size(), 1);
    }

    // ---- renderer ------------------------------------------------------------------
    void rendererCreatesPlottables()
    {
        auto w = makeWindow();
        QCustomPlot* plot = w->plot();
        QCOMPARE(plot->plottableCount(), 1);
        QVERIFY(qobject_cast<QCPGraph*>(plot->plottable(0)));
        // Unsorted x -> QCPCurve keeps point order.
        Dataset circle;
        for (int i = 0; i <= 64; ++i) {
            circle.x.push_back(std::cos(i * M_PI / 32));
            circle.y.push_back(std::sin(i * M_PI / 32));
        }
        const ObjectId c = w->editor()->addDataset(circle);
        QCOMPARE(plot->plottableCount(), 2);
        QVERIFY(qobject_cast<QCPCurve*>(plot->plottable(1)));
        QCOMPARE(plot->legend->itemCount(), 2);
        w->editor()->removeDataset(c);
        QCOMPARE(plot->plottableCount(), 1);
        w->editor()->undo();
        QCOMPARE(plot->plottableCount(), 2);
    }

    void autoscaleAndReset()
    {
        auto w = makeWindow();
        w->renderer()->autoscale();
        const ViewRange r = w->renderer()->viewRange();
        QCOMPARE(r.xmin, 0.0);
        QCOMPARE(r.xmax, 100.0);
        QVERIFY(r.ymin < 0 && r.ymax > 100);  // 5% margin
        w->renderer()->captureHomeView();
        w->renderer()->setViewRange({10, 20, 10, 20});
        w->renderer()->resetView();
        QCOMPARE(w->renderer()->viewRange(), r);
    }

    // ---- interaction (simulated mouse) ------------------------------------------------------
    void regionSelectionByMouse()
    {
        auto w = makeWindow();
        w->controller()->setMode(InteractionMode::Region);
        drag(w->plot(), px(w.get(), 20, 50), px(w.get(), 40, 50));
        const Region r = w->model()->region();
        QVERIFY(r.active);
        QVERIFY(std::abs(r.xmin - 20) < 0.5);
        QVERIFY(std::abs(r.xmax - 40) < 0.5);
        // Drag the right edge to x=60.
        drag(w->plot(), px(w.get(), r.xmax, 50), px(w.get(), 60, 50));
        QVERIFY(std::abs(w->model()->region().xmax - 60) < 0.5);
        w->editor()->undo();
        QVERIFY(std::abs(w->model()->region().xmax - 40) < 0.5);
        w->editor()->undo();
        QVERIFY(!w->model()->region().active);
    }

    void markerPlaceAndDrag()
    {
        auto w = makeWindow();
        w->controller()->setMode(InteractionMode::AddVMarker);
        QTest::mouseClick(w->plot(), Qt::LeftButton, {}, px(w.get(), 50, 50));
        QCOMPARE(int(w->model()->markers().size()), 1);
        // one-shot: back to the navigation mode
        QCOMPARE(w->controller()->mode(), InteractionMode::Zoom);
        const ObjectId id = w->model()->markers()[0].id;
        const double start = w->model()->marker(id)->position;
        QVERIFY(std::abs(start - 50) < 0.5);
        QCOMPARE(w->controller()->selection(), (ObjectRef{ObjectKind::Marker, id}));

        drag(w->plot(), px(w.get(), start, 30), px(w.get(), 75, 30));
        QVERIFY(std::abs(w->model()->marker(id)->position - 75) < 0.5);
        QCOMPARE(w->editor()->undoStack()->undoText(), QString("Move marker"));
        w->editor()->undo();
        QCOMPARE(w->model()->marker(id)->position, start);

        // Delete key removes the selected marker; undo restores it.
        w->controller()->setSelection({ObjectKind::Marker, id});
        w->plot()->setFocus();
        QTest::keyClick(w->plot(), Qt::Key_Delete);
        QVERIFY(w->model()->markers().empty());
        w->editor()->undo();
        QVERIFY(w->model()->marker(id));
    }

    void horizontalMarkerDrag()
    {
        auto w = makeWindow();
        Marker m;
        m.orientation = Orientation::Horizontal;
        m.position = 40;
        const ObjectId id = w->editor()->addMarker(m);
        w->plot()->replot();
        drag(w->plot(), px(w.get(), 30, 40), px(w.get(), 30, 70));
        QVERIFY(std::abs(w->model()->marker(id)->position - 70) < 0.5);
    }

    void annotationDragKeepsDataCoordinates()
    {
        auto w = makeWindow();
        Annotation a;
        a.x = 30;
        a.y = 30;
        a.text = "Peak";
        const ObjectId id = w->editor()->addAnnotation(a);
        w->plot()->replot();
        // Text is anchored bottom-left at (30,30); grab slightly inside it.
        const QPoint grab = px(w.get(), 30, 30) + QPoint(8, -6);
        drag(w->plot(), grab, grab + QPoint(100, -50));
        const Annotation* moved = w->model()->annotation(id);
        QVERIFY(moved->x > 35 && moved->y > 35);
        // Zooming changes pixels but not the data anchor.
        const Annotation before = *moved;
        w->renderer()->setViewRange({0, 50, 0, 50});
        QCOMPARE(*w->model()->annotation(id), before);
    }

    void wheelZoomAboutCursor()
    {
        auto w = makeWindow();
        const QPoint at = px(w.get(), 25, 50);
        QWheelEvent ev(at, w->plot()->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton,
                       Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(w->plot(), &ev);
        const ViewRange r = w->renderer()->viewRange();
        QVERIFY(r.xmax - r.xmin < 100);
        // The point under the cursor stays fixed.
        QVERIFY(std::abs(w->renderer()->pixelToData(at).x() - 25) < 0.5);
        // Ctrl+wheel only zooms X.
        const ViewRange before = w->renderer()->viewRange();
        QWheelEvent ev2(at, w->plot()->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton,
                        Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(w->plot(), &ev2);
        const ViewRange after = w->renderer()->viewRange();
        QVERIFY(after.xmax - after.xmin < before.xmax - before.xmin);
        QCOMPARE(after.ymin, before.ymin);
        QCOMPARE(after.ymax, before.ymax);
    }

    void rubberBandZoom()
    {
        auto w = makeWindow();
        w->controller()->setMode(InteractionMode::Zoom);
        drag(w->plot(), px(w.get(), 20, 80), px(w.get(), 60, 40));
        const ViewRange r = w->renderer()->viewRange();
        QVERIFY(std::abs(r.xmin - 20) < 1 && std::abs(r.xmax - 60) < 1);
        QVERIFY(std::abs(r.ymin - 40) < 1 && std::abs(r.ymax - 80) < 1);
    }

    void panDrag()
    {
        auto w = makeWindow();
        w->controller()->setMode(InteractionMode::Pan);
        drag(w->plot(), px(w.get(), 50, 50), px(w.get(), 60, 50));
        const ViewRange r = w->renderer()->viewRange();
        QVERIFY(std::abs(r.xmin - (-10)) < 0.5);
        QVERIFY(std::abs(r.xmax - 90) < 0.5);
    }

    // ---- export -----------------------------------------------------------------------------
    void exportFormats()
    {
        auto w = makeWindow();
        Marker m;
        m.position = 50;
        m.label = "M1";
        w->editor()->addMarker(m);
        w->editor()->setRegion({true, 10, 20});
        QTemporaryDir dir;
        QString err;
        for (const char* ext : {"png", "svg", "pdf"}) {
            const QString fn = dir.filePath(QString("fig.%1").arg(ext));
            QVERIFY2(w->exportTo(fn, 800, 500, 2.0, &err), qPrintable(err));
            QFile f(fn);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QByteArray head = f.read(4096);
            QVERIFY(f.size() > 1000);
            if (QByteArray(ext) == "png") {
                QImage img(fn);
                QCOMPARE(img.size(), QSize(1600, 1000));  // 2x scale
            } else if (QByteArray(ext) == "svg") {
                const QByteArray all = head + f.readAll();
                QVERIFY(all.contains("<svg"));
                QVERIFY(all.contains("<path") || all.contains("<polyline"));
                QVERIFY(!all.contains("<image"));  // pure vector, no embedded raster
            } else {
                QVERIFY(head.startsWith("%PDF"));
            }
        }
        QVERIFY(!w->exportTo(dir.filePath("fig.bmp"), 0, 0, 1, &err));
    }

    // ---- 2D data / contours --------------------------------------------------------------
    void contourCircleGeometry()
    {
        // z = sqrt(x^2 + y^2): the level-0.5 contour is a circle of radius 0.5.
        Matrix m;
        m.nx = m.ny = 101;
        m.x0 = m.y0 = -1;
        m.x1 = m.y1 = 1;
        for (int iy = 0; iy < 101; ++iy)
            for (int ix = 0; ix < 101; ++ix) {
                const double x = -1 + 0.02 * ix, y = -1 + 0.02 * iy;
                m.z.push_back(std::sqrt(x * x + y * y));
            }
        ContourGrid g;
        g.setMatrix(m);
        const auto lines = g.compute({0.5});
        QCOMPARE(int(lines.size()), 1);
        QVERIFY(lines[0].segments.size() > 100);
        double total = 0;
        for (const QLineF& l : lines[0].segments) {
            for (const QPointF& p : {l.p1(), l.p2()})
                QVERIFY(std::abs(std::hypot(p.x(), p.y()) - 0.5) < 0.01);
            total += l.length();
        }
        QVERIFY(std::abs(total - 2 * M_PI * 0.5) < 0.02);  // circumference
    }

    void contourLevelsGeometric()
    {
        ContourStyle s;
        s.baseLevel = 1;
        s.factor = 2;
        s.levelCount = 3;
        s.showNegative = true;
        const auto l = contourLevels(s);
        QCOMPARE(l, (std::vector<double>{1, -1, 2, -2, 4, -4}));
    }

    void matrixAddedWithAutoLevels()
    {
        auto w = makeWindow();
        const ObjectId id = w->editor()->addMatrix(peaks(200));
        const Matrix* m = w->model()->matrix(id);
        QVERIFY(m->style.baseLevel > 0);
        QVERIFY(m->style.positiveColor.isValid());
        ContourPlottable* c = w->renderer()->contourPlottable(id);
        QVERIFY(c);
        QVERIFY(c->segmentCount() > 0);
        bool anyNegative = false;
        for (const auto& l : c->lines()) anyNegative |= (l.negative && !l.segments.isEmpty());
        QVERIFY(anyNegative);
        w->editor()->undo();
        QVERIFY(!w->renderer()->contourPlottable(id));
        w->editor()->redo();
        QVERIFY(w->renderer()->contourPlottable(id));
    }

    void altWheelScrollsContourLevels()
    {
        auto w = makeWindow();
        const ObjectId id = w->editor()->addMatrix(peaks(200));
        w->renderer()->autoscale();
        const double base0 = w->model()->matrix(id)->style.baseLevel;
        const qsizetype seg0 = w->renderer()->contourPlottable(id)->segmentCount();
        const ViewRange view0 = w->renderer()->viewRange();
        const int undoCount = w->editor()->undoStack()->count();
        const QPoint at = w->plot()->axisRect()->rect().center();
        for (int i = 0; i < 3; ++i) {  // three notches "up" with Alt held
            QWheelEvent ev(at, w->plot()->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton,
                           Qt::AltModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(w->plot(), &ev);
        }
        const double base1 = w->model()->matrix(id)->style.baseLevel;
        QVERIFY(std::abs(base1 - base0 / std::pow(1.2, 3)) < 1e-9 * base0);
        QCOMPARE(w->renderer()->viewRange(), view0);                       // no zoom
        QVERIFY(w->renderer()->contourPlottable(id)->segmentCount() > seg0);  // more contours
        QCOMPARE(w->editor()->undoStack()->count(), undoCount + 1);        // merged
        w->editor()->undo();
        QCOMPARE(w->model()->matrix(id)->style.baseLevel, base0);

        // "Levels" mode: plain wheel scrolls levels; '/' doubles them.
        w->controller()->setWheelAdjustsLevels(true);
        QWheelEvent ev(at, w->plot()->mapToGlobal(at), QPoint(), QPoint(0, -120), Qt::NoButton,
                       Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(w->plot(), &ev);
        QVERIFY(std::abs(w->model()->matrix(id)->style.baseLevel - base0 * 1.2) < 1e-9 * base0);
        QCOMPARE(w->renderer()->viewRange(), view0);
        w->controller()->scaleContourLevels(2.0);
        QVERIFY(std::abs(w->model()->matrix(id)->style.baseLevel - base0 * 2.4) < 1e-9 * base0);
    }

    void levelScrollTargetsSelectedMatrix()
    {
        auto w = makeWindow();
        const ObjectId a = w->editor()->addMatrix(peaks(50));
        const ObjectId b = w->editor()->addMatrix(peaks(50));
        const double ba = w->model()->matrix(a)->style.baseLevel;
        const double bb = w->model()->matrix(b)->style.baseLevel;
        w->controller()->setSelection({ObjectKind::Matrix, b});
        w->controller()->scaleContourLevels(0.5);
        QCOMPARE(w->model()->matrix(a)->style.baseLevel, ba);
        QCOMPARE(w->model()->matrix(b)->style.baseLevel, bb * 0.5);
        w->controller()->setSelection({});
        w->controller()->scaleContourLevels(2.0);  // all visible
        QCOMPARE(w->model()->matrix(a)->style.baseLevel, ba * 2);
        QCOMPARE(w->model()->matrix(b)->style.baseLevel, bb);
    }

    void contourRecomputeIsFast()
    {
        // 1024 x 1024 grid, 2 x 12 levels: recomputation while scrolling must
        // stay interactive.
        Matrix m = peaks(1024);
        ContourStyle s;
        s.levelCount = 12;  // explicit: 0 would mean "style default"
        s.factor = 1.4;
        s.baseLevel = defaultBaseLevel(m, s.factor, s.levelCount);
        ContourGrid g;
        g.setMatrix(m);
        QElapsedTimer t;
        t.start();
        const auto lines = g.compute(contourLevels(s));
        const qint64 ms = t.elapsed();
        QCOMPARE(int(lines.size()), 24);
        qInfo("1024x1024 grid, %d levels: %lld ms", int(lines.size()), ms);
        QVERIFY(ms < 250);
    }

    void loadMatrix()
    {
        QTemporaryDir dir;
        const QString fn = dir.filePath("m.txt");
        QFile f(fn);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("# extent: 0 10 -1 1\n1 2 3\n4 5 6\n");
        f.close();
        const MatrixLoadResult r = loadMatrixFile(fn);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.matrix.nx, 3);
        QCOMPARE(r.matrix.ny, 2);
        QCOMPARE(r.matrix.x1, 10.0);
        QCOMPARE(r.matrix.z[4], 5.0);
    }

    void brukerRoundTripAndReversedAxes()
    {
        Matrix m = peaks(64);
        m.x0 = 9.0;  // ppm, high -> low
        m.x1 = 1.0;
        m.y0 = 9.0;
        m.y1 = 1.0;
        QTemporaryDir dir;
        const QString pdata = dir.filePath("exp/pdata/1");
        QString err;
        QVERIFY2(writeBruker2D(pdata, m, "1H", "1H", 600.13, 600.13, 16, 8, &err), qPrintable(err));
        const BrukerSpectrum s = readBruker2D(dir.filePath("exp"));
        QVERIFY2(s.error.isEmpty(), qPrintable(s.error));
        QCOMPARE(s.matrix.nx, 64);
        QCOMPARE(s.nucleusF1, QString("1H"));
        QVERIFY(std::abs(s.matrix.x1 - 1.0) < 1e-9);
        double maxErr = 0;
        for (int i = 0; i < m.z.size(); ++i) maxErr = std::max(maxErr, std::abs(m.z[i] - s.matrix.z[i]));
        QVERIFY(maxErr < 1e-6);  // int32 quantisation only

        auto w = makeWindow();
        QVERIFY(w->openBruker(pdata, &err) != kInvalidId);
        QVERIFY(w->model()->axesInfo().xReversed);
        QVERIFY(w->model()->axesInfo().showDiagonal);  // homonuclear
        QVERIFY(w->plot()->xAxis->rangeReversed());
        // Panning on a reversed axis still drags the data with the mouse.
        w->renderer()->setViewRange({2, 8, 2, 8});
        w->plot()->replot();
        const QPoint a = px(w.get(), 5, 5), b = a + QPoint(100, 0);
        const double before = w->renderer()->pixelToData(a).x();
        w->controller()->setMode(InteractionMode::Pan);
        drag(w->plot(), a, b);
        QVERIFY(std::abs(w->renderer()->pixelToData(b).x() - before) < 0.02);
    }

    // ---- peaks ------------------------------------------------------------------------------
    // Gaussian peaks on a 201x201 grid over [0,10]^2 at off-grid positions.
    static Matrix spots()
    {
        Matrix m;
        m.nx = m.ny = 201;
        m.x0 = m.y0 = 0;
        m.x1 = m.y1 = 10;
        m.z.resize(201 * 201);
        struct P { double x, y, a; };
        const P ps[] = {{2.013, 2.013, 1.0}, {5.0, 5.0, 1.0}, {2.017, 5.031, 0.3},
                        {5.031, 2.017, 0.3}, {7.5, 3.0, -0.4}, {8.0, 8.2, 0.05}};
        for (int iy = 0; iy < 201; ++iy)
            for (int ix = 0; ix < 201; ++ix) {
                const double x = ix * 0.05, y = iy * 0.05;
                double z = 0;
                for (const P& p : ps)
                    z += p.a * std::exp(-((x - p.x) * (x - p.x) + (y - p.y) * (y - p.y)) / (2 * 0.15 * 0.15));
                m.z[iy * 201 + ix] = z;
            }
        return m;
    }

    void pickPeaksBasics()
    {
        const Matrix m = spots();
        PeakPickOptions o;
        o.threshold = 0.1;
        auto all = pickPeaks(m, o);
        QCOMPARE(int(all.size()), 4);  // the 0.05 peak is below threshold, negative off
        // Strongest first; sub-grid interpolation recovers off-grid positions.
        bool found = false;
        for (const Peak& p : all)
            if (std::abs(p.x - 2.017) < 0.005 && std::abs(p.y - 5.031) < 0.005) found = true;
        QVERIFY(found);

        o.diagonalExclusion = 0.5;  // only off-diagonal cross peaks
        auto cross = pickPeaks(m, o);
        QCOMPARE(int(cross.size()), 2);
        for (const Peak& p : cross) QVERIFY(std::abs(p.x - p.y) > 0.5);

        o.negative = true;
        o.diagonalExclusion = 0;
        auto withNeg = pickPeaks(m, o);
        QCOMPARE(int(withNeg.size()), 5);
        QVERIFY(std::any_of(withNeg.begin(), withNeg.end(), [](const Peak& p) { return p.intensity < 0; }));

        o.restrictToRect = true;  // only the lower-left quadrant
        o.xmin = 0; o.xmax = 4; o.ymin = 0; o.ymax = 4;
        QCOMPARE(int(pickPeaks(m, o).size()), 1);

        const auto s = snapToPeak(m, 4.8, 5.2, 8);
        QVERIFY(s && std::abs(s->x - 5.0) < 0.01 && std::abs(s->y - 5.0) < 0.01);
    }

    void pickPeaksUndoAndNoDuplicates()
    {
        PlotModel model;
        PlotEditor ed(&model);
        Matrix m = spots();
        const ObjectId id = ed.addMatrix(m);
        ContourStyle st = model.matrix(id)->style;
        st.baseLevel = 0.1;  // threshold defaults to the lowest contour
        ed.setMatrixAppearance(id, "spots", st);
        PeakPickOptions o;
        QCOMPARE(int(ed.pickPeaks(id, o).size()), 4);
        QCOMPARE(int(ed.pickPeaks(id, o).size()), 0);  // second pick: no duplicates
        QCOMPARE(int(model.peaks().size()), 4);
        ed.undo();  // the empty pick produced no command; this undoes the first pick
        QCOMPARE(int(model.peaks().size()), 0);
        ed.redo();
        QCOMPARE(int(model.peaks().size()), 4);
    }

    void peakToolSnapsAndLabelDrags()
    {
        auto w = makeWindow();
        w->editor()->addMatrix(spots());
        w->renderer()->setViewRange({0, 10, 0, 10});
        w->plot()->replot();
        w->controller()->setMode(InteractionMode::AddPeak);
        QTest::mouseClick(w->plot(), Qt::LeftButton, {}, px(w.get(), 5.06, 4.95));
        QCOMPARE(int(w->model()->peaks().size()), 1);
        Peak p = w->model()->peaks()[0];
        QVERIFY(std::abs(p.x - 5.0) < 0.01 && std::abs(p.y - 5.0) < 0.01);  // snapped
        QCOMPARE(w->controller()->mode(), InteractionMode::AddPeak);           // sticky tool
        // Shift+click: exactly at the cursor.
        QTest::mouseClick(w->plot(), Qt::LeftButton, Qt::ShiftModifier, px(w.get(), 6.5, 6.5));
        QVERIFY(std::abs(w->model()->peaks()[1].x - 6.5) < 0.05);

        // Label: set text, then drag the label (not the peak).
        p.label = "H3/C5";
        w->editor()->updatePeak(p);
        w->plot()->replot();
        const QRectF lr = w->renderer()->peakOverlay()->labelRect(*w->model()->peak(p.id));
        drag(w->plot(), lr.center().toPoint(), lr.center().toPoint() + QPoint(40, -30));
        const Peak* moved = w->model()->peak(p.id);
        QCOMPARE(moved->x, p.x);  // the peak itself did not move
        QVERIFY(std::abs(moved->labelOffset.x() - (p.labelOffset.x() + 40)) < 1.5);
        w->editor()->undo();
        QCOMPARE(w->model()->peak(p.id)->labelOffset, p.labelOffset);

        // Escape leaves the sticky peak tool.
        QTest::keyClick(w->plot(), Qt::Key_Escape);
        QTest::keyClick(w->plot(), Qt::Key_Escape);
        QVERIFY(w->controller()->mode() != InteractionMode::AddPeak);
    }

    void peakCsvRoundTrip()
    {
        std::vector<Peak> ps(2);
        ps[0].x = 1.5; ps[0].y = 2.25; ps[0].intensity = 1e6;
        ps[0].label = "H3/C5";
        ps[0].comment = "Multiplet, J = 7.2 Hz, \"tentative\"\nsee HMBC";
        ps[1].x = -0.5; ps[1].y = 3; ps[1].label = "a,b";
        QTemporaryDir dir;
        const QString fn = dir.filePath("p.csv");
        QVERIFY(writePeakCsv(fn, ps, "F2 (ppm)", "F1 (ppm)"));
        QString err;
        const auto back = readPeakCsv(fn, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(int(back.size()), 2);
        QCOMPARE(back[0].comment, ps[0].comment);
        QCOMPARE(back[1].label, QString("a,b"));
        QCOMPARE(back[0].intensity, 1e6);
    }

    // ---- error bars / steps / fills ---------------------------------------------------------
    void errorBarsStepsAndFills()
    {
        auto w = makeWindow();
        Dataset d;
        d.name = "pts";
        d.x = {1, 2, 3};
        d.y = {10, 20, 30};
        d.yErrLow = {1, 2, 3};
        d.yErrHigh = {1, 2, 30};
        d.xErrLow = d.xErrHigh = {0.5, 0.5, 0.5};
        const ObjectId id = w->editor()->addDataset(d);
        int bars = 0;
        for (int i = 0; i < w->plot()->plottableCount(); ++i)
            bars += qobject_cast<QCPErrorBars*>(w->plot()->plottable(i)) != nullptr;
        QCOMPARE(bars, 2);  // x and y error bars
        double x0, x1, y0, y1;
        QVERIFY(w->model()->dataBounds(x0, x1, y0, y1));
        QCOMPARE(y1, 100.0);  // 100 = ramp max; 30 + 30 = 60 < 100
        QCOMPARE(x0, 0.0);
        // Error bars are not legend entries.
        QCOMPARE(w->plot()->legend->itemCount(), 2);
        // Removing the dataset removes its error bars; undo restores them.
        w->editor()->removeDataset(id);
        QCOMPARE(w->plot()->plottableCount(), 1);
        w->editor()->undo();
        QCOMPARE(w->plot()->plottableCount(), 4);

        // Steps + fill
        DatasetStyle st = w->model()->dataset(id)->style;
        st.step = StepMode::Post;
        st.fillColor = QColor(255, 0, 0, 80);
        w->editor()->setDatasetAppearance(id, "pts", st);
        auto* g = qobject_cast<QCPGraph*>(w->plot()->plottable(1));
        QVERIFY(g);
        QCOMPARE(g->lineStyle(), QCPGraph::lsStepLeft);
        QCOMPARE(g->brush().color(), QColor(255, 0, 0, 80));
        // Band: fill towards another dataset.
        st.fillTo = w->model()->datasets()[0].id;
        w->editor()->setDatasetAppearance(id, "pts", st);
        QCOMPARE(g->channelFillGraph(), qobject_cast<QCPGraph*>(w->plot()->plottable(0)));
        // Fills to zero extend the bounds to the baseline.
        st.fillTo = kInvalidId;
        w->editor()->setDatasetAppearance(id, "pts", st);
        st.inLegend = false;
        w->editor()->setDatasetAppearance(id, "pts", st);
        QCOMPARE(w->plot()->legend->itemCount(), 1);
    }

    // ---- multi-panel figures ------------------------------------------------------------------
    void subplotsSharedAxesAndActivePanel()
    {
        auto w = makeWindow();  // panel 0 holds the ramp
        GridSpec g;
        g.rows = 2;
        g.sharex = true;
        g.hspace = 0;
        g.heightRatios = {0.7, 0.3};
        w->setGrid(g);
        QCOMPARE(w->axesCount(), 2);
        QCOMPARE(w->model()->datasets().size(), size_t(1));  // panel 0 is current, content kept
        w->plot()->replot();
        PlotRenderer* top = w->axes(0).renderer;
        PlotRenderer* bottom = w->axes(1).renderer;
        QVERIFY(!top->xAxis()->tickLabels());  // only the bottom panel shows x tick labels
        QVERIFY(bottom->xAxis()->tickLabels());
        // Frames touch (hspace 0) and share left/right edges.
        QCOMPARE(top->axisRect()->rect().bottom() + 1, bottom->axisRect()->rect().top());
        QCOMPARE(top->axisRect()->rect().left(), bottom->axisRect()->rect().left());
        // Shared x: changing one changes the other.
        top->setViewRange({10, 20, 0, 1});
        QCOMPARE(bottom->xAxis()->range().lower, 10.0);
        // Clicking in the lower panel makes it current; edits go there.
        const QPoint inBottom = bottom->axisRect()->rect().center();
        w->controller()->setMode(InteractionMode::AddVMarker);
        QTest::mouseClick(w->plot(), Qt::LeftButton, {}, inBottom);
        QCOMPARE(w->currentAxes(), 1);
        QCOMPARE(int(w->axes(1).model->markers().size()), 1);
        QCOMPARE(int(w->axes(0).model->markers().size()), 0);
        // One undo history for the whole figure.
        w->setCurrentAxes(0);
        w->editor()->undo();
        QCOMPARE(int(w->axes(1).model->markers().size()), 0);
        // Wheel over the top panel zooms that panel (and the linked x of the other).
        const QPoint inTop = top->axisRect()->rect().center();
        QWheelEvent ev(inTop, w->plot()->mapToGlobal(inTop), QPoint(), QPoint(0, 120), Qt::NoButton,
                       Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(w->plot(), &ev);
        QCOMPARE(w->currentAxes(), 0);
        QCOMPARE(bottom->xAxis()->range().size(), top->xAxis()->range().size());
        // Styles apply to all panels.
        QVERIFY(w->applyStyle("science"));
        QCOMPARE(w->axes(1).model->style().xtick.direction, QString("in"));
        QTemporaryDir dir;
        QVERIFY(w->exportTo(dir.filePath("two.pdf")));
    }

    // ---- audit regressions (core) ----------------------------------------------------------
    void auditRegressionsCore()
    {
        QTemporaryDir dir;
        // Style: a file inheriting itself must not recurse forever.
        QFile loop(dir.filePath("loop.gpstyle"));
        QVERIFY(loop.open(QIODevice::WriteOnly));
        loop.write("inherit: loop.gpstyle\nfont.size: 12\n");
        loop.close();
        Style st;
        QStringList errors;
        QVERIFY(loadStyle(loop.fileName(), &st, &errors));
        QVERIFY(errors.join(" ").contains("too deep"));
        // Style: absurd numbers are rejected instead of hanging the app.
        QString err;
        QVERIFY(!st.set("contour.levels", "100000000", &err));
        QVERIFY(!st.set("figure.padding", "inf", &err));
        QVERIFY(!st.set("xtick.count", "1e20", &err));
        QVERIFY(st.set("contour.levels", "40", &err));

        // Undo of a data update restores the error bars.
        PlotModel model;
        PlotEditor ed(&model);
        Dataset d;
        d.x = {1, 2, 3};
        d.y = {1, 2, 3};
        d.yErrLow = d.yErrHigh = {0.1, 0.2, 0.3};
        const ObjectId id = ed.addDataset(d);
        ed.setDatasetData(id, {1, 2}, {5, 6});
        QVERIFY(model.dataset(id)->yErrLow.isEmpty());
        ed.undo();
        QCOMPARE(model.dataset(id)->yErrHigh, (QVector<double>{0.1, 0.2, 0.3}));
        // Undo texts are capitalised normally.
        Marker mk;
        ed.addMarker(mk);
        QCOMPARE(ed.undoStack()->undoText(), QString("Add marker"));
        // Contour levels cannot be scaled to exactly 0 (that would be final).
        const ObjectId mid = ed.addMatrix(peaks(20));
        for (int i = 0; i < 400; ++i) ed.scaleContourLevels({mid}, 1e-10);
        QVERIFY(model.matrix(mid)->style.baseLevel > 0);

        // Column files: header names with spaces/brackets, empty cells = gaps.
        QFile csv(dir.filePath("cols.csv"));
        QVERIFY(csv.open(QIODevice::WriteOnly));
        csv.write("Time (s),Voltage A,Current B\n0,1,\n1,,4\n2,3,5\n");
        csv.close();
        const LoadResult r = loadColumnFile(csv.fileName());
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.datasets.size(), size_t(2));
        QCOMPARE(r.datasets[0].name, QString("Voltage A"));
        QVERIFY(std::isnan(r.datasets[0].y[1]));
        QCOMPARE(r.datasets[1].y[2], 5.0);
        // European style: ';' separated, decimal commas are not split on ','.
        QCOMPARE(detectSeparator("0,5;1,2"), QChar(';'));

        // Bruker writer validates its input.
        Matrix bad = peaks(10);
        QVERIFY(!writeBruker2D(dir.filePath("bad"), bad, "1H", "1H", 600, 600, 3, 3, &err));
        QVERIFY(!writeBruker2D(dir.filePath("bad"), bad, "1H", "1H", 600, 600, 0, 5, &err));

        // NaN coordinates never index outside the grid.
        QVERIFY(std::isnan(sampleMatrix(peaks(10), std::nan(""), 0.0)));
    }

    void auditPeakPickingScales()
    {
        // 1000 x 1000 noisy grid with a low threshold: many candidates.
        Matrix m;
        m.nx = m.ny = 1000;
        m.x1 = m.y1 = 10;
        m.z.resize(1000 * 1000);
        quint32 seed = 1;
        for (double& v : m.z) {
            seed = seed * 1664525u + 1013904223u;
            v = (seed >> 8) / double(1 << 24);
        }
        PeakPickOptions o;
        o.threshold = 0.5;
        o.minDistance = 2;
        QElapsedTimer t;
        t.start();
        const auto found = pickPeaks(m, o);
        qInfo("1000x1000 noise: %zu peaks in %lld ms", found.size(), t.elapsed());
        QVERIFY(found.size() > 10000);
        QVERIFY(t.elapsed() < 3000);
    }

    // ---- styles & rich text ----------------------------------------------------------------
    void styleTextRoundTrip()
    {
        Style s;
        QVERIFY(builtinStyle("science", &s));
        const QString text = s.toText("test");
        Style back;
        QStringList errors;
        QVERIFY(back.parse(text, &errors));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join("; ")));
        for (const auto& k : Style::keys())
            QVERIFY2(back.value(k.key) == s.value(k.key), qPrintable(k.key + ": " + s.value(k.key) + " vs " + back.value(k.key)));
        QCOMPARE(back, s);  // every key survives the round trip
        // Every key is listed with a description.
        for (const auto& k : Style::keys()) {
            QVERIFY(!k.description.isEmpty());
            QVERIFY(!s.value(k.key).isNull());
        }
    }

    void styleParsingAndErrors()
    {
        QTemporaryDir dir;
        QFile base(dir.filePath("base.gpstyle"));
        QVERIFY(base.open(QIODevice::WriteOnly));
        base.write("inherit: science\nfont.size: 13\n");
        base.close();
        QFile f(dir.filePath("mine.gpstyle"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("# my style\n"
                "inherit: base.gpstyle      # relative to this file\n"
                "axes.prop_cycle: #ff0000, blue  ,#00ff00   # colours after ':' are not comments\n"
                "xtick.direction: out\n"
                "no.such.key: 3\n"
                "axes.linewidth: thick\n"
                "label.top.left: \"**ATLAS** *Internal*\"\n");
        f.close();
        Style s;
        QStringList errors;
        QVERIFY(loadStyle(f.fileName(), &s, &errors));
        QCOMPARE(errors.size(), 2);  // unknown key + bad number, both reported with line numbers
        QVERIFY(errors[0].contains("line 5"));
        QCOMPARE(s.fontSize, 13.0);          // from base.gpstyle
        QVERIFY(s.ytick.minorVisible);       // from science (inherited twice)
        QCOMPARE(s.xtick.direction, QString("out"));
        QCOMPARE(s.palette.size(), 3);
        QCOMPARE(s.palette[0], QColor("#ff0000"));
        QCOMPARE(s.labelTopLeft, QString("**ATLAS** *Internal*"));
    }

    void figureStyleIsUndoableAndDrivesRendering()
    {
        auto w = makeWindow();
        QVERIFY(w->applyStyle("science"));
        QCOMPARE(w->model()->style().xtick.direction, QString("in"));
        QVERIFY(w->plot()->xAxis->subTicks());   // minor ticks on
        QVERIFY(!w->model()->axesInfo().gridVisible);  // grid follows the style
        QCOMPARE(w->plot()->xAxis2->ticks(), true);    // mirrored ticks
        w->editor()->undo();
        QCOMPARE(*&w->model()->style(), *&defaultStyle());
        QVERIFY(w->model()->axesInfo().gridVisible);
        QVERIFY(!w->plot()->xAxis->subTicks());
    }

    void userDefaultStyleFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("default.gpstyle");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("inherit: nmr\nfont.size: 15\n");
        f.close();
        qputenv("GRAPHPLOT_STYLE", path.toUtf8());
        reloadDefaultStyle();
        QCOMPARE(defaultStyle().fontSize, 15.0);
        PlotModel m;  // new figures start from the default style
        QCOMPARE(m.style().fontSize, 15.0);
        QCOMPARE(m.style().xtick.direction, QString("out"));
        qunsetenv("GRAPHPLOT_STYLE");
        reloadDefaultStyle();
        QCOMPARE(defaultStyle().fontSize, 10.0);
    }

    void richTextConversion()
    {
        const QString h = richTextToHtml("m$_{\\gamma\\gamma}$ / $10^{-3}$ **CMS** *Prelim*");
        QVERIFY(h.contains("<sub>γγ</sub>"));
        QVERIFY(h.contains("<sup>−3</sup>"));
        QVERIFY(h.contains("<b>CMS</b>"));
        QVERIFY(h.contains("<i>Prelim</i>"));
        QVERIFY(richTextToHtml("$x^2$").contains("<i>x</i><sup>2</sup>"));
        QVERIFY(richTextToHtml("$\\mathrm{GeV}$") == "GeV");
        QVERIFY(richTextToHtml("a < b & 5$").contains("&lt;"));     // escaped, lone $ kept
        QCOMPARE(richTextToHtml("$x$", false), QString("$x$"));       // math disabled
        QVERIFY(richTextToHtml("price \\$5").contains("$5"));
    }

    // ---- time series ------------------------------------------------------------------------
    void timeSeriesCsvFormats()
    {
        QTemporaryDir dir;
        auto write = [&](const QString& name, const QByteArray& text) {
            QFile f(dir.filePath(name));
            if (!f.open(QIODevice::WriteOnly)) qFatal("cannot write");
            f.write(text);
            return f.fileName();
        };
        // Relative seconds, header with units, time not in the first column, unsorted, gap.
        auto a = loadTimeSeriesCsv(write("imu.csv",
            "ax [m/s2],time [s],ay\n0.1,0.02,1\n0.0,0.00,2\n,0.01,3\n"));
        QVERIFY2(a.error.isEmpty(), qPrintable(a.error));
        QVERIFY(!a.absolute);
        QCOMPARE(a.timeColumn, QString("time [s]"));
        QCOMPARE(a.columns, (QStringList{"ax [m/s2]", "ay"}));
        QCOMPARE(a.time, (QVector<double>{0.0, 0.01, 0.02}));
        QVERIFY(std::isnan(a.values[0][1]));  // empty cell -> gap
        QCOMPARE(a.values[1][2], 1.0);
        // Epoch milliseconds, semicolon separated.
        auto b = loadTimeSeriesCsv(write("gps.csv", "timestamp;speed\n1714658607250;12.5\n1714658607350;12.7\n"));
        QVERIFY(b.absolute);
        QVERIFY(std::abs(b.time[0] - 1714658607.25) < 1e-6);
        // Nanoseconds, no header.
        auto c = loadTimeSeriesCsv(write("can.txt", "1714658607000000000 1 2\n1714658607500000000 3 4\n"));
        QVERIFY(c.absolute);
        QVERIFY(std::abs(c.time[1] - 1714658607.5) < 1e-6);
        QCOMPARE(c.columns.size(), 2);
        // ISO date-times.
        auto d = loadTimeSeriesCsv(write("iso.csv", "datetime,v\n2024-05-02 14:03:27.250,1\n2024-05-02T14:03:28,2\n"));
        QVERIFY2(d.error.isEmpty(), qPrintable(d.error));
        QVERIFY(d.absolute);
        QVERIFY(std::abs((d.time[1] - d.time[0]) - 0.75) < 1e-9);
        // Errors
        QVERIFY(!loadTimeSeriesCsv(write("bad.csv", "time\n1\n2\n")).error.isEmpty());
    }

    // ---- timeline ---------------------------------------------------------------------------
private:
    struct TimelineFixture {
        std::unique_ptr<FigureWindow> w;
        Timeline* tl = nullptr;
        int imu = -1, gps = -1;
        ObjectId ax = kInvalidId, speed = kInvalidId;
    };
    TimelineFixture makeTimeline()
    {
        TimelineFixture f;
        f.w = std::make_unique<FigureWindow>();
        f.w->resize(1000, 800);
        f.w->show();
        if (!QTest::qWaitForWindowExposed(f.w.get())) qWarning("not exposed");
        f.tl = f.w->timeline();
        QVector<double> t, a, v;
        for (int i = 0; i <= 1000; ++i) {
            t << i * 0.1;                  // 0..100 s
            a << (i >= 400 && i < 450 ? -3.0 : 0.0);
            v << 20.0 - (i >= 400 ? std::min(10.0, (i - 400) * 0.06) : 0.0);
        }
        f.imu = f.tl->addRow("IMU");
        f.ax = f.tl->addSignal(f.imu, "ax", t, a);
        f.gps = f.tl->addRow("GPS");
        f.speed = f.tl->addSignal(f.gps, "speed", t, v);
        f.w->plot()->replot();
        QCoreApplication::processEvents();
        return f;
    }

private slots:

    void timelineRowsShareTimeAndAutoFit()
    {
        auto f = makeTimeline();
        QCOMPARE(f.tl->rows(), (QList<int>{f.imu, f.gps}));
        QCOMPARE(f.imu, 0);  // the figure's first panel is reused
        QCOMPARE(f.tl->window(), (QPair<double, double>{0.0, 100.0}));
        f.tl->setWindow(30, 50);
        QCoreApplication::processEvents();  // auto-fit runs queued
        PlotRenderer* gpsR = f.w->axes(f.gps).renderer;
        QCOMPARE(gpsR->xAxis()->range().lower, 30.0);  // rows are linked
        // Auto-fit: speed in [30, 50] s spans 14..20 (+ padding), not the full 10..20.
        QVERIFY(gpsR->yAxis()->range().lower > 13 && gpsR->yAxis()->range().lower < 14);
        // Navigator shows the window as its region.
        const Region nav = f.tl->navigatorModel()->region();
        QVERIFY(nav.active && nav.xmin == 30 && nav.xmax == 50);
    }

    void timelineNavigatorDragAndWheel()
    {
        auto f = makeTimeline();
        f.tl->setWindow(20, 40);
        f.w->plot()->replot();
        QCPAxis* nx = f.tl->navigatorRenderer()->xAxis();
        const int y = f.tl->navigatorRenderer()->axisRect()->rect().center().y();
        auto px = [&](double t) { return QPoint(int(std::lround(nx->coordToPixel(t))), y); };
        // Drag the window body by +30 s.
        drag(f.w->plot(), px(30), px(60));
        auto [a, b] = f.tl->window();
        QVERIFY(std::abs(a - 50) < 0.5 && std::abs(b - 70) < 0.5);
        // Drag the right edge out to 90 s.
        drag(f.w->plot(), px(b), px(90));
        std::tie(a, b) = f.tl->window();
        QVERIFY(std::abs(b - 90) < 0.5 && std::abs(a - 50) < 0.5);
        // Wheel over the navigator zooms the window about the mouse.
        QWheelEvent ev(px(70), f.w->plot()->mapToGlobal(px(70)), QPoint(), QPoint(0, 120), Qt::NoButton,
                       Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(f.w->plot(), &ev);
        auto [c, d] = f.tl->window();
        QVERIFY(d - c < b - a);
        // Click outside the window centres it there (keeping its width).
        QTest::mouseClick(f.w->plot(), Qt::LeftButton, {}, px(10));
        auto [e, g] = f.tl->window();
        QVERIFY(std::abs((e + g) / 2 - 10) < 0.6);
        QVERIFY(std::abs((g - e) - (d - c)) < 1e-6);
        // Double-click: whole recording.
        QTest::mouseDClick(f.w->plot(), Qt::LeftButton, {}, px(50));
        QCOMPARE(f.tl->window(), (QPair<double, double>{0.0, 100.0}));
    }

    void timelineCursorReadoutsAndPins()
    {
        auto f = makeTimeline();
        f.tl->setWindow(0, 100);
        f.w->plot()->replot();
        PlotRenderer* imuR = f.w->axes(f.imu).renderer;
        const QPoint p(int(imuR->xAxis()->coordToPixel(42.0)), imuR->axisRect()->rect().center().y());
        QMouseEvent move(QEvent::MouseMove, QPointF(p), f.w->plot()->mapToGlobal(QPointF(p)), Qt::NoButton,
                         Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(f.w->plot(), &move);
        QVERIFY(f.tl->cursorTime().has_value());
        QVERIFY(std::abs(*f.tl->cursorTime() - 42.0) < 0.2);
        const auto values = f.tl->valuesAt(42.0);
        QCOMPARE(values.size(), 2);
        QCOMPARE(values[0].value, -3.0);   // ax during braking
        QVERIFY(std::abs(values[1].value - 18.8) < 1e-9);  // speed interpolated
        // Ctrl+click drops a time marker through all rows.
        QTest::mouseClick(f.w->plot(), Qt::LeftButton, Qt::ControlModifier, p);
        QCOMPARE(int(f.tl->pins().size()), 1);
        const ObjectId pin = f.tl->pins()[0].id;
        // Drag it (in the other row) to 60 s; undo restores 42 s.
        PlotRenderer* gpsR = f.w->axes(f.gps).renderer;
        const double t0 = f.tl->pins()[0].t;
        const QPoint from(int(std::lround(gpsR->xAxis()->coordToPixel(t0))), gpsR->axisRect()->rect().center().y());
        const QPoint to(int(gpsR->xAxis()->coordToPixel(60.0)), from.y());
        drag(f.w->plot(), from, to);
        QVERIFY(std::abs(f.tl->pins()[0].t - 60.0) < 0.3);
        f.w->undoStack()->undo();
        QCOMPARE(f.tl->pins()[0].t, t0);
        f.tl->removePin(pin);
        QVERIFY(f.tl->pins().empty());
        f.w->undoStack()->undo();
        QCOMPARE(int(f.tl->pins().size()), 1);
        // Leaving the plot hides the cursor.
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(f.w->plot(), &leave);
        QVERIFY(!f.tl->cursorTime().has_value());
    }

    void timelineRearrangeRows()
    {
        auto f = makeTimeline();
        f.tl->setRowOrder({f.gps, f.imu});
        QCOMPARE(f.tl->rows(), (QList<int>{f.gps, f.imu}));
        f.w->plot()->replot();
        // gps is now above imu
        QVERIFY(f.w->axes(f.gps).renderer->axisRect()->rect().top() <
                f.w->axes(f.imu).renderer->axisRect()->rect().top());
        // Move "speed" into the IMU row: one undo step.
        const ObjectId moved = f.tl->moveSignal({f.gps, f.speed}, f.imu);
        QVERIFY(moved != kInvalidId);
        QCOMPARE(int(f.w->axes(f.imu).model->datasets().size()), 2);
        QVERIFY(f.w->axes(f.gps).model->datasets().empty());
        QVERIFY(f.tl->removeRowIfEmpty(f.gps));
        QCOMPARE(f.tl->rows(), (QList<int>{f.imu}));
        f.w->undoStack()->undo();  // undo the move (row removal is a layout change)
        QCOMPARE(int(f.w->axes(f.gps).model->datasets().size()), 1);
        // Hide / show and heights
        const int r3 = f.tl->addRow("extra");
        f.tl->setRowVisible(r3, false);
        QVERIFY(!f.w->axes(r3).renderer->axisRect()->visible());
        f.tl->setRowVisible(r3, true);
        f.tl->setRowHeight(r3, 2.0);
        QCOMPARE(f.tl->rowHeight(r3), 2.0);
    }

    void auditRegressionsGui()
    {
        auto f = makeTimeline();
        // Hidden rows must not capture clicks meant for the row below.
        f.tl->setRowVisible(f.imu, false);
        f.w->plot()->replot();
        PlotRenderer* gpsR = f.w->axes(f.gps).renderer;
        QCOMPARE(f.w->controller()->targetAt(gpsR->axisRect()->rect().topLeft() + QPoint(5, 5)), f.gps);
        f.tl->setRowVisible(f.imu, true);
        // Undo of a move after "Remove empty row" brings the row back.
        f.tl->moveSignal({f.gps, f.speed}, f.imu);
        QVERIFY(f.tl->removeRowIfEmpty(f.gps));
        f.w->undoStack()->undo();
        QVERIFY(f.tl->rows().contains(f.gps));
        // A 1-2 px wobble in the navigator is a click (centres), not a tiny window.
        f.w->plot()->replot();
        QCPAxis* nx = f.tl->navigatorRenderer()->xAxis();
        const int y = f.tl->navigatorRenderer()->axisRect()->rect().center().y();
        f.tl->setWindow(0, 20);
        const QPoint at(int(nx->coordToPixel(70)), y);
        drag(f.w->plot(), at, at + QPoint(2, 0));
        const auto [a, b] = f.tl->window();
        QVERIFY(std::abs((b - a) - 20) < 1e-6);
        QVERIFY(std::abs((a + b) / 2 - 70) < 0.6);
    }

    void auditRegressionsDragCancel()
    {
        auto w = makeWindow();
        Marker m;
        m.position = 50;
        const ObjectId id = w->editor()->addMarker(m);
        w->plot()->replot();
        const int undoCount = w->undoStack()->count();
        // Start dragging the marker, then press the right button: drag cancelled.
        const QPoint p0 = px(w.get(), 50, 50), p1 = px(w.get(), 70, 50);
        QTest::mousePress(w->plot(), Qt::LeftButton, {}, p0);
        for (int i = 1; i <= 5; ++i) QTest::mouseMove(w->plot(), p0 + (p1 - p0) * i / 5);
        QTest::mousePress(w->plot(), Qt::RightButton, {}, p1);
        QCOMPARE(w->model()->marker(id)->position, 50.0);  // back at the start
        QCOMPARE(w->undoStack()->count(), undoCount);      // nothing recorded
        // Label edits keep full-precision coordinates (no rounding to 6 decimals).
        Marker tiny = *w->model()->marker(id);
        tiny.position = 5e-7;
        w->editor()->updateMarker(tiny);
        w->controller()->setSelection({ObjectKind::Marker, id});
        QCoreApplication::processEvents();
        // Rename through the properties panel, exactly as a user would.
        auto* label = w->findChild<QLineEdit*>("markerLabel");
        QVERIFY(label);
        label->setText("renamed");
        emit label->editingFinished();
        QCOMPARE(w->model()->marker(id)->label, QString("renamed"));
        QCOMPARE(w->model()->marker(id)->position, 5e-7);
    }

    void timelineClockAxisAndCsv()
    {
        QTemporaryDir dir;
        QFile a(dir.filePath("imu.csv"));
        QVERIFY(a.open(QIODevice::WriteOnly));
        a.write("timestamp,ax\n1714658600.0,0\n1714658600.5,1\n1714658601.0,0\n");
        a.close();
        QFile b(dir.filePath("gps.csv"));
        QVERIFY(b.open(QIODevice::WriteOnly));
        b.write("time_ms;speed;alt\n1714658600200;10;100\n1714658600700;11;101\n");
        b.close();
        FigureWindow w;
        Timeline* tl = w.timeline();
        QString err;
        QVERIFY2(tl->loadCsv({a.fileName(), b.fileName()}, false, &err), qPrintable(err));
        QCOMPARE(tl->rows().size(), 2);
        QVERIFY(tl->hasAbsoluteTime());
        QCOMPARE(tl->timeOrigin(), 1714658600.0);
        // Different time formats are aligned on one clock.
        // (epoch seconds in a double resolve ~0.24 us)
        QVERIFY(std::abs(w.axes(tl->rows()[1]).model->datasets()[0].x.front() - 0.2) < 1e-6);
        tl->setClockTime(true);
        QVERIFY(tl->clockTime());
        QVERIFY(tl->formatTime(0.25).endsWith(".250"));
        // Row per column
        FigureWindow w2;
        QVERIFY(w2.timeline()->loadCsv({b.fileName()}, true, &err));
        QCOMPARE(w2.timeline()->rows().size(), 2);
    }

    // ---- region statistics, typed limits, view history, drag & drop -----------------
    void featureRangeStats()
    {
        PlotModel m;
        Dataset d = ramp("ramp", 11);  // y = x for x = 0..10
        d.y[2] = std::nan("");          // a gap: excluded, interrupts the integral
        m.insertDataset(d);
        const auto st = rangeStats(m, 4, 0);  // reversed bounds are fine
        QCOMPARE(int(st.size()), 1);
        const SignalStats& s = st[0];
        QCOMPARE(s.n, qsizetype(4));  // 0, 1, 3, 4
        QCOMPARE(s.mean, 2.0);
        QCOMPARE(s.min, 0.0);
        QCOMPARE(s.xAtMax, 4.0);
        QCOMPARE(s.integral, 0.5 + 3.5);
        QVERIFY(std::abs(s.std - std::sqrt(10.0 / 3)) < 1e-12);
        QVERIFY(std::abs(s.rms - std::sqrt(26.0 / 4)) < 1e-12);
        QCOMPARE(rangeStats(m, 20, 30)[0].n, qsizetype(0));  // empty range
        // Values on a large offset keep their spread; descending x integrates positive.
        PlotModel m2;
        Dataset e;
        e.x = {4, 3, 2, 1};
        e.y = {1e8 + 0.001, 1e8 + 0.002, 1e8 + 0.003, 1e8 + 0.004};
        m2.insertDataset(e);
        const SignalStats o = rangeStats(m2, 0, 10)[0];
        QVERIFY(std::abs(o.std - 0.0012909944) < 1e-8);
        QVERIFY(o.integral > 0);
    }

    void featureLimitsAndViewHistory()
    {
        auto w = makeWindow();
        auto* xMin = w->findChild<QDoubleSpinBox*>("xMin");
        auto* xMax = w->findChild<QDoubleSpinBox*>("xMax");
        QVERIFY(xMin && xMax);
        QTest::qWait(500);                // the 0..100 view becomes the history start
        QCOMPARE(xMax->value(), 100.0);  // follows the view
        xMax->setValue(50);               // typing sets the view
        QCOMPARE(w->renderer()->viewRange().xmax, 50.0);
        QTest::qWait(500);                // let the history settle
        w->renderer()->setViewRange({10, 20, 0, 5});
        QAction* back = nullptr;
        QAction* fwd = nullptr;
        for (QAction* a : w->findChildren<QAction*>()) {
            if (a->text() == "Previous view") back = a;
            if (a->text() == "Next view") fwd = a;
        }
        QVERIFY(back && fwd);
        back->trigger();  // commits the still-settling view, then steps back
        QCOMPARE(w->renderer()->viewRange().xmax, 50.0);
        back->trigger();
        QCOMPARE(w->renderer()->viewRange().xmax, 100.0);
        fwd->trigger();
        fwd->trigger();
        QCOMPARE(w->renderer()->viewRange(), (ViewRange{10, 20, 0, 5}));
        QCOMPARE(xMin->value(), 10.0);
        // A range that doesn't overlap the current one can be typed min-first.
        xMin->setValue(30);  // view is 10..20: X max follows (width kept)
        QCOMPARE(w->renderer()->viewRange().xmin, 30.0);
        QCOMPARE(w->renderer()->viewRange().xmax, 40.0);
    }

    void featureTimelinePinSteps()
    {
        auto f = makeTimeline();
        f.tl->setWindow(0, 10);  // centre 5
        QCOMPARE(f.tl->stepPin(+1), kInvalidId);  // no pins yet
        const ObjectId p30 = f.tl->addPin(30);
        const ObjectId p60 = f.tl->addPin(60);
        QCOMPARE(f.tl->stepPin(+1), p30);
        QCOMPARE(f.tl->window().first + 5, 30.0);
        QCOMPARE(f.tl->stepPin(+1), p60);  // already centred on p30: go further
        QCOMPARE(f.tl->stepPin(+1), kInvalidId);
        QCOMPARE(f.tl->stepPin(-1), p30);
        f.tl->setCursorTime(32);  // the readout includes the delta (smoke test)
    }

    void featureDropOpensFiles()
    {
        auto w = makeWindow();
        QTemporaryDir dir;
        const QString fn = dir.filePath("data.csv");
        QFile f(fn);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("t,a\n0,1\n1,2\n");
        f.close();
        auto* mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(fn)});
        QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(w.get(), &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(10, 10), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        const int before = int(w->model()->datasets().size());
        QCoreApplication::sendEvent(w.get(), &drop);
        QCOMPARE(drop.dropAction(), Qt::CopyAction);  // never a move
        QTRY_COMPARE(int(w->model()->datasets().size()), before + 1);  // opened after the drop
        delete mime;
    }

    void auditRound2()
    {
        // Overlaid spectra each get their own peaks; one undo step; removing
        // a spectrum removes its peaks and undo restores both.
        auto w = makeWindow();
        PlotEditor* ed = w->editor();
        const ObjectId m1 = ed->addMatrix(peaks(41));
        const ObjectId m2 = ed->addMatrix(peaks(41));
        PeakPickOptions o;
        o.threshold = 0.5;
        const int steps = w->undoStack()->count();
        const auto ids = ed->pickPeaks(QList<ObjectId>{m1, m2}, o);
        QCOMPARE(ids.size(), 2);
        QCOMPARE(w->undoStack()->count(), steps + 1);
        QVERIFY(ed->pickPeaks(QList<ObjectId>{m1, m2}, o).isEmpty());
        QCOMPARE(w->undoStack()->count(), steps + 1);  // nothing new: no empty step
        ed->removeMatrix(m1);
        QCOMPARE(int(w->model()->peaks().size()), 1);
        w->undoStack()->undo();
        QCOMPARE(int(w->model()->peaks().size()), 2);
        // Scrolling levels of two spectra merges into one step.
        const int before = w->undoStack()->index();
        const double level = w->model()->matrix(m2)->style.baseLevel;
        for (int i = 0; i < 5; ++i) ed->scaleContourLevels({m1, m2}, 1.2);
        QCOMPARE(w->undoStack()->index(), before + 1);
        w->undoStack()->undo();
        QCOMPARE(w->model()->matrix(m2)->style.baseLevel, level);

        // Shrinking a grid hides the panels that no longer fit.
        GridSpec g;
        g.rows = g.cols = 2;
        w->setGrid(g);
        g.rows = g.cols = 1;
        w->setGrid(g);
        QVERIFY(!w->axes(3).renderer->axisRect()->visible());
        QVERIFY(w->axes(0).renderer->axisRect()->visible());

        // PDF pages use 100 figure pixels per inch, like PNG and SVG.
        QTemporaryDir dir;
        QString err;
        QVERIFY2(w->exportTo(dir.filePath("f.pdf"), 800, 500, 2.0, &err), qPrintable(err));
        QFile pdf(dir.filePath("f.pdf"));
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        QVERIFY(pdf.readAll().contains("MediaBox [0 0 576"));
    }

    // ---- appearance -------------------------------------------------------------------
    void themesDarkOnScreenLightInExports()
    {
        auto w = makeWindow();
        Appearance* app = Appearance::instance();
        QVERIFY(!app->setTheme("no-such-theme", false));
        QVERIFY(app->setTheme("dark", false));
        QVERIFY(QApplication::palette().color(QPalette::Window).lightnessF() < 0.3);
        // On screen: dark plot background; the figure's style is unchanged.
        QVERIFY(w->plot()->axisRect()->backgroundBrush().color().lightnessF() < 0.3);
        QCOMPARE(w->model()->style().axesFace, QColor(Qt::white));
        // Exports use the figure's own (light) colours by default...
        QTemporaryDir dir;
        QVERIFY(w->exportTo(dir.filePath("t.png"), 200, 150, 1.0));
        QImage img(dir.filePath("t.png"));
        QVERIFY(img.pixelColor(img.width() / 2, img.height() / 2).lightnessF() > 0.7);
        // ...and the screen is dark again afterwards.
        QVERIFY(w->plot()->axisRect()->backgroundBrush().color().lightnessF() < 0.3);
        // Plots can keep their style colours in a dark theme.
        app->setThemePlots(false, false);
        QCOMPARE(w->plot()->axisRect()->backgroundBrush().color(), QColor(Qt::white));
        app->setThemePlots(true, false);
        // Every theme applies without errors; back to the default.
        for (const ThemeDef& t : DisplayTheme::themes()) QVERIFY(app->setTheme(t.id, false));
        QVERIFY(app->setTheme("system", false));
        QCOMPARE(w->plot()->axisRect()->backgroundBrush().color(), QColor(Qt::white));
    }

    void tooltipsCanBeSwitchedOff()
    {
        auto w = makeWindow();
        Appearance::instance()->load();  // installs the tooltip filter
        QWidget* target = w->plot();
        Appearance::instance()->setTooltipsEnabled(false, false);
        QHelpEvent off(QEvent::ToolTip, QPoint(5, 5), target->mapToGlobal(QPoint(5, 5)));
        QVERIFY(QApplication::sendEvent(target, &off));  // swallowed by the filter
        Appearance::instance()->setTooltipsEnabled(true, false);
        QVERIFY(Appearance::instance()->tooltipsEnabled());
    }

    // Every button, field and menu entry explains itself on hover.
    void everyControlHasAnExplanation()
    {
        FigureWindow w;
        w.timeline();  // also the Timeline menu and dock
        w.show();
        QStringList missing;
        auto explained = [](const QString& tip, const QString& text) {
            QString t = text;
            t.remove('&');
            return !tip.isEmpty() && tip != t && tip != t.chopped(t.endsWith(QChar(0x2026)) ? 1 : 0);
        };
        for (QWidget* c : w.findChildren<QWidget*>()) {
            const bool control = qobject_cast<QAbstractButton*>(c) || qobject_cast<QAbstractSpinBox*>(c) ||
                                 qobject_cast<QComboBox*>(c) || qobject_cast<QLineEdit*>(c);
            if (!control || qobject_cast<QToolButton*>(c)) continue;  // tool buttons: via their actions
            if (qobject_cast<QLineEdit*>(c) && qobject_cast<QAbstractSpinBox*>(c->parentWidget())) continue;
            if (qobject_cast<QLineEdit*>(c) && qobject_cast<QComboBox*>(c->parentWidget())) continue;
            if (c->objectName().startsWith("qt_") || c->inherits("QTableCornerButton"))
                continue;  // Qt-internal (dock title buttons, table corner, ...)
            const auto* b = qobject_cast<QAbstractButton*>(c);
            if (!explained(c->toolTip(), b ? b->text() : QString()))
                missing << QString("%1 '%2' (%3)").arg(c->metaObject()->className(), b ? b->text() : c->objectName(),
                                                       c->parentWidget() ? c->parentWidget()->metaObject()->className() : "");
        }
        for (QAction* a : w.findChildren<QAction*>()) {
            if (a->isSeparator() || a->menu() || a->text().isEmpty()) continue;
            if (!explained(a->toolTip(), a->text())) missing << "action '" + a->text() + "'";
        }
        missing.removeDuplicates();
        for (const QString& m : missing) qWarning("no hover explanation: %s", qPrintable(m));
        QVERIFY2(missing.isEmpty(), qPrintable("no hover explanation:\n  " + missing.join("\n  ")));
    }

    // ---- figure files and data export -----------------------------------------------
    void figureFileRoundTrip()
    {
        auto w = makeWindow();  // panel 0: the ramp
        PlotEditor* ed = w->editor();
        Dataset d = ramp("with errors", 5);
        d.yErrLow = d.yErrHigh = {0.1, 0.2, 0.3, 0.4, 0.5};
        d.y[2] = std::nan("");  // NaN survives the trip
        d.style.color = QColor(10, 20, 30, 200);
        ed->addDataset(d);
        const ObjectId m = ed->addMatrix(peaks(21));
        Peak p;
        p.matrixId = m;
        p.x = 0.3;
        p.label = "H3/C5";
        p.comment = "dd, J = 7.2 Hz\nsecond line, \"quoted\"";
        ed->addPeak(p);
        Marker mk;
        mk.position = 42;
        mk.label = "edge";
        ed->addMarker(mk);
        Annotation an;
        an.x = 1, an.y = 2, an.text = "$\\alpha$ note";
        ed->addAnnotation(an);
        ed->setRegion(Region{true, 10, 20});
        AxesInfo info = w->model()->axesInfo();
        info.xLabel = "ppm";
        info.xReversed = true;
        w->model()->setAxesInfo(info);
        Style st = w->model()->style();
        st.fontSize = 13;
        w->model()->setStyle(st);
        GridSpec g;
        g.rows = 2;
        g.sharex = true;
        w->setGrid(g);
        w->axes(1).editor->addDataset(ramp("lower", 3));
        w->renderer()->setViewRange({5, 50, -1, 60});

        QTemporaryDir dir;
        const QString fn = dir.filePath("f.gpfig");
        QString err;
        QVERIFY2(w->saveFigure(fn, &err), qPrintable(err));

        FigureWindow back;
        QVERIFY2(back.loadFigure(fn, &err), qPrintable(err));
        QCOMPARE(back.axesCount(), 2);
        const PlotModel* a = back.axes(0).model;
        QCOMPARE(a->datasets().size(), size_t(2));
        const Dataset& e = a->datasets()[1];
        QCOMPARE(e.name, QString("with errors"));
        QCOMPARE(e.yErrHigh, d.yErrHigh);
        QVERIFY(std::isnan(e.y[2]));
        QCOMPARE(e.style.color, d.style.color);
        QCOMPARE(a->matrices().size(), size_t(1));
        QCOMPARE(a->matrices()[0].z, peaks(21).z);
        QCOMPARE(a->peaks()[0].comment, p.comment);
        QCOMPARE(a->peaks()[0].matrixId, a->matrices()[0].id);
        QCOMPARE(a->markers()[0].label, QString("edge"));
        QCOMPARE(a->annotations()[0].text, an.text);
        QCOMPARE(a->region().xmax, 20.0);
        QCOMPARE(a->axesInfo().xLabel, QString("ppm"));
        QVERIFY(a->axesInfo().xReversed);
        QCOMPARE(a->style().fontSize, 13.0);
        QCOMPARE(back.axes(1).model->datasets()[0].name, QString("lower"));
        QCOMPARE(back.axes(0).renderer->viewRange().xmin, 5.0);
        QVERIFY(!back.undoStack()->canUndo());  // a fresh history
        // Not into a window that holds data; not from a foreign file.
        QVERIFY(!w->loadFigure(fn, &err));
        QFile junk(dir.filePath("junk.gpfig"));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("{\"hello\": 1}");
        junk.close();
        FigureWindow empty;
        QVERIFY(!empty.loadFigure(junk.fileName(), &err));
    }

    void timelineFigureRoundTrip()
    {
        auto f = makeTimeline();
        f.tl->addPin(30, "brake");
        f.tl->setRowHeight(f.gps, 2.5);
        f.tl->setWindow(20, 40);
        f.tl->setNavigatorSignal({f.gps, f.speed});
        QTemporaryDir dir;
        const QString fn = dir.filePath("t.gpfig");
        QString err;
        QVERIFY2(f.w->saveFigure(fn, &err), qPrintable(err));
        FigureWindow back;
        QVERIFY2(back.loadFigure(fn, &err), qPrintable(err));
        QVERIFY(back.hasTimeline());
        Timeline* tl = back.timeline();
        QCOMPARE(tl->rows().size(), 2);
        QCOMPARE(tl->rowName(tl->rows()[1]), QString("GPS"));
        QCOMPARE(tl->rowHeight(tl->rows()[1]), 2.5);
        QCOMPARE(int(tl->pins().size()), 1);
        QCOMPARE(tl->pins()[0].label, QString("brake"));
        QCOMPARE(tl->window(), (QPair<double, double>(20, 40)));
        QCOMPARE(tl->signalName(tl->navigatorSignal()), QString("GPS / speed"));
    }

    void dataExportCsv()
    {
        PlotModel m;
        Dataset a = ramp("a", 4), b = ramp("b", 4, 2.0);
        m.insertDataset(a);
        m.insertDataset(b);
        QTemporaryDir dir;
        QString err;
        QVERIFY(writeDataCsv(m, dir.filePath("wide.csv"), std::nullopt, &err));
        QFile wide(dir.filePath("wide.csv"));
        QVERIFY(wide.open(QIODevice::ReadOnly));
        QCOMPARE(QString(wide.readAll()), QString("x,a,b\n0,0,0\n1,1,2\n2,2,4\n3,3,6\n"));
        Dataset c = ramp("c", 2);
        c.x = {0.5, 1.5};
        m.insertDataset(c);  // different x: long format
        QVERIFY(writeDataCsv(m, dir.filePath("long.csv"), Region{true, 1, 2}, &err));
        QFile lng(dir.filePath("long.csv"));
        QVERIFY(lng.open(QIODevice::ReadOnly));
        const QStringList lines = QString(lng.readAll()).trimmed().split('\n');
        QCOMPARE(lines.front(), QString("dataset,x,y"));
        QCOMPARE(lines.size(), 1 + 2 + 2 + 1);  // a: x=1,2; b: x=1,2; c: x=1.5
        QVERIFY(statisticsTsv(m, 0, 3).startsWith("dataset\tn\tmean"));
    }

    void auditRound3()
    {
        // Single '*' only pairs with a lone '*'; "a * b" is not emphasis.
        QCOMPARE(richTextToHtml("2*3 = 6, see **note**"), QString("2*3 = 6, see <b>note</b>"));
        QCOMPARE(richTextToHtml("a * b and c * d"), QString("a * b and c * d"));
        QCOMPARE(richTextToHtml("*it* and **bold**"), QString("<i>it</i> and <b>bold</b>"));
        // Style text values with '#' and quotes survive save + load.
        Style st;
        QVERIFY(builtinStyle("default", &st));
        st.labelInside = "#1 #2";
        st.labelTopLeft = "12\" pipe";
        Style back;
        QStringList errors;
        QVERIFY2(back.parse(st.toText(), &errors), qPrintable(errors.join("; ")));
        QCOMPARE(back.labelInside, st.labelInside);
        QCOMPARE(back.labelTopLeft, st.labelTopLeft);

        // Edits in different panels never merge into one undo step.
        auto w = makeWindow();
        GridSpec g;
        g.cols = 2;
        w->setGrid(g);
        w->axes(1).editor->addDataset(ramp("other"));  // same id sequence as panel 0
        const ObjectId a = w->axes(0).editor->addMatrix(peaks(21));
        const ObjectId b = w->axes(1).editor->addMatrix(peaks(21));
        QCOMPARE(a, b);  // ids are per panel: the case that used to merge
        const double la = w->axes(0).model->matrix(a)->style.baseLevel;
        w->axes(0).editor->scaleContourLevels({a}, 2);
        w->axes(1).editor->scaleContourLevels({b}, 2);
        w->undoStack()->undo();
        w->undoStack()->undo();
        QCOMPARE(w->axes(0).model->matrix(a)->style.baseLevel, la);
        QCOMPARE(w->axes(1).model->matrix(b)->style.baseLevel, la);

        // The properties page never changes values the user didn't touch.
        Matrix m = *w->axes(0).model->matrix(a);
        m.style.levelCount = 150;
        m.style.factor = 1.0005;
        w->axes(0).editor->setMatrixAppearance(a, m.name, m.style, false);
        w->setCurrentAxes(0);
        w->controller()->setSelection({ObjectKind::Matrix, a});
        QCoreApplication::processEvents();
        auto* neg = w->findChild<QCheckBox*>("matrixNegative");
        QVERIFY(neg);
        neg->click();
        QCOMPARE(w->axes(0).model->matrix(a)->style.levelCount, 150);
        QCOMPARE(w->axes(0).model->matrix(a)->style.factor, 1.0005);
    }

    void timelineNavigatorFollowsMovedSignal()
    {
        auto f = makeTimeline();
        f.tl->setNavigatorSignal({f.gps, f.speed});
        f.tl->moveSignal({f.gps, f.speed}, f.imu);
        QCOMPARE(f.tl->navigatorSignal().row, f.imu);
        QCOMPARE(f.tl->signalName(f.tl->navigatorSignal()), QString("IMU / speed"));
    }

    // ---- data loading -----------------------------------------------------------------------
    void loaderEdgeCases()
    {
        QTemporaryDir dir;
        auto write = [&](const QString& name, const QByteArray& text) {
            QFile f(dir.filePath(name));
            f.open(QIODevice::WriteOnly);
            f.write(text);
            return f.fileName();
        };
        // A relative microsecond counter is not a 1970 epoch.
        TimeSeriesTable a = loadTimeSeriesCsv(write("daq.csv", "time_us,v\n0,1\n600000000,2\n"));
        QVERIFY2(a.error.isEmpty(), qPrintable(a.error));
        QVERIFY(!a.absolute);
        QCOMPARE(a.time.last(), 600.0);
        // Space-separated date + time stays one timestamp.
        TimeSeriesTable b = loadTimeSeriesCsv(
            write("log.txt", "date time value\n2024-01-01 12:00:00 1.5\n2024-01-01 12:00:01 2.5\n"));
        QVERIFY2(b.error.isEmpty(), qPrintable(b.error));
        QVERIFY(b.absolute);
        QCOMPARE(b.time[1] - b.time[0], 1.0);
        QCOMPARE(b.columns, QStringList{"value"});
        // Quoted names containing the separator; a trailing separator adds nothing.
        LoadResult c = loadColumnFile(write("excel.csv", "\"x\",\"Temp, K\",\n1,2,\n2,3,\n"));
        QVERIFY2(c.error.isEmpty(), qPrintable(c.error));
        QCOMPARE(int(c.datasets.size()), 1);
        QCOMPARE(c.datasets[0].name, QString("Temp, K"));
        // Space-separated header with units.
        LoadResult d = loadColumnFile(write("units.txt", "time (s)   value\n0 1\n1 2\n"));
        QCOMPARE(d.datasets[0].name, QString("value"));
        // Matrix: empty cells are NaN, columns don't shift.
        MatrixLoadResult m = loadMatrixFile(write("grid.csv", "1,,3\n4,,6\n"));
        QVERIFY2(m.error.isEmpty(), qPrintable(m.error));
        QCOMPARE(m.matrix.nx, 3);
        QVERIFY(std::isnan(m.matrix.z[1]));
        QCOMPARE(m.matrix.z[5], 6.0);
    }

    void loadColumns()
    {
        QTemporaryDir dir;
        const QString fn = dir.filePath("data.csv");
        QFile f(fn);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("# comment\ntime,a,b\n0,1,2\n1,2,4\n2,3,6\n");
        f.close();
        const LoadResult r = loadColumnFile(fn);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(int(r.datasets.size()), 2);
        QCOMPARE(r.datasets[1].name, QString("b"));
        QCOMPARE(r.datasets[1].y, (QVector<double>{2, 4, 6}));
    }
};

QTEST_MAIN(TestGraphplot)
#include "test_graphplot.moc"
