// Unit and interaction tests. Run headless with QT_QPA_PLATFORM=offscreen
// (ctest sets this automatically).
#include "commands/PlotEditor.h"
#include "gui/FigureWindow.h"
#include "model/DataLoader.h"
#include "model/PlotModel.h"
#include "qcustomplot.h"
#include "render/PlotRenderer.h"

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
