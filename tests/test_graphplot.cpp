// Unit and interaction tests. Run headless with QT_QPA_PLATFORM=offscreen
// (ctest sets this automatically).
#include "commands/PlotEditor.h"
#include "gui/FigureWindow.h"
#include "model/DataLoader.h"
#include "model/PlotModel.h"
#include "qcustomplot.h"
#include "model/BrukerReader.h"
#include "model/Contour.h"
#include "model/PeakPicker.h"
#include "model/Style.h"
#include "render/RichText.h"
#include "render/PeakOverlay.h"
#include "render/ContourPlottable.h"
#include "render/PlotRenderer.h"

#include <QElapsedTimer>

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
        s.baseLevel = defaultBaseLevel(m, s.factor, s.levelCount);
        ContourGrid g;
        g.setMatrix(m);
        QElapsedTimer t;
        t.start();
        const auto lines = g.compute(contourLevels(s));
        const qint64 ms = t.elapsed();
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

    // ---- data loading -----------------------------------------------------------------------
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
