// FigureWindow: one figure = one top-level window.
//
// It wires the layers together:
//   PlotModel (data)  <-  PlotEditor (undoable edits)
//        |                    ^             ^
//        v                    |             |
//   PlotRenderer -> QCustomPlot  <- InteractionController     PropertiesPanel
//
// and provides toolbar, menus and status bar. The Python bindings drive a
// FigureWindow through editor(), renderer() and controller().
#pragma once

#include "interaction/InteractionController.h"

#include <QMainWindow>

class QCustomPlot;
class QLabel;
class QActionGroup;
class QComboBox;
class QDockWidget;

namespace gp {

class PlotModel;
class PlotEditor;
class PlotRenderer;
class PropertiesPanel;

class FigureWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit FigureWindow(QWidget* parent = nullptr);
    ~FigureWindow() override;

    PlotModel* model() const { return m_model; }
    PlotEditor* editor() const { return m_editor; }
    PlotRenderer* renderer() const { return m_renderer; }
    InteractionController* controller() const { return m_controller; }
    QCustomPlot* plot() const { return m_plot; }

    bool openDataFile(const QString& fileName, QString* error = nullptr);
    bool openMatrixFile(const QString& fileName, QString* error = nullptr);
    // Opens a Bruker processed 2D spectrum (pdata dir, 2rr file or experiment
    // dir) with ppm axes, NMR axis orientation and nucleus axis labels.
    // Returns the new 2D dataset id (kInvalidId on error).
    ObjectId openBruker(const QString& path, QString* error = nullptr);
    bool exportTo(const QString& fileName, int width = 0, int height = 0, double scale = 2.0,
                  QString* error = nullptr);

signals:
    void closed();

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildActions();
    void showContextMenu(ObjectRef ref, QPoint globalPos, QPointF dataPos);
    void onOpen();
    void onOpenMatrix();
    void onOpenBruker();
    void updateLevelStatus();
    void onExport();

    PlotModel* m_model;
    PlotEditor* m_editor;
    QCustomPlot* m_plot;
    PlotRenderer* m_renderer;
    InteractionController* m_controller;
    PropertiesPanel* m_panel;
    QDockWidget* m_dock;
    QActionGroup* m_modeGroup = nullptr;
    QComboBox* m_zoomAxes = nullptr;
    QLabel* m_cursorLabel = nullptr;
    QLabel* m_regionLabel = nullptr;
    QLabel* m_levelLabel = nullptr;
    QAction* m_levelsAction = nullptr;
    QString m_lastDir;
};

}  // namespace gp
