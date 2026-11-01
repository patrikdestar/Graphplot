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

#include <QList>
#include <QMainWindow>
#include <vector>

class QCustomPlot;
class QLabel;
class QActionGroup;
class QComboBox;
class QDockWidget;
class QUndoStack;
class QCPLayoutGrid;
class QCPMarginGroup;
class QCPAxisRect;

namespace gp {

class PlotModel;
class PlotEditor;
class PlotRenderer;
class PropertiesPanel;
class PeakTable;
class Timeline;

// Layout of a multi-panel figure (matplotlib's subplots).
struct GridSpec {
    int rows = 1;
    int cols = 1;
    QList<double> heightRatios;  // one per row (empty = equal)
    QList<double> widthRatios;   // one per column
    int hspace = -1;             // px between rows (-1 = default; 0 = frames touch)
    int wspace = -1;
    bool sharex = false;         // linked x ranges; only the bottom row shows x tick labels
    bool sharey = false;
};

class FigureWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit FigureWindow(QWidget* parent = nullptr);
    ~FigureWindow() override;

    // The *current* panel (the one last clicked, or selected via setCurrentAxes).
    PlotModel* model() const { return m_model; }
    PlotEditor* editor() const { return m_editor; }
    PlotRenderer* renderer() const { return m_renderer; }
    InteractionController* controller() const { return m_controller; }
    QCustomPlot* plot() const { return m_plot; }

    // ---- panels (axes) ---------------------------------------------------
    struct Axes {
        PlotModel* model;
        PlotEditor* editor;
        PlotRenderer* renderer;
    };
    int axesCount() const { return int(m_axes.size()); }
    const Axes& axes(int i) const { return m_axes[size_t(i)]; }
    int currentAxes() const;
    void setCurrentAxes(int index);
    // Arranges the figure as rows x cols panels. Existing panels keep their
    // content (panel 0 stays top-left); missing ones are created.
    void setGrid(const GridSpec& spec);
    const GridSpec& gridSpec() const { return m_gridSpec; }
    QUndoStack* undoStack() const { return m_stack; }
    // Creates an empty panel (not placed in the layout); returns its index.
    int createPanel();
    // The layout grid holding the panels (created on first use).
    void ensureGrid();
    QCPLayoutGrid* panelGrid() const { return m_grid; }
    QList<QCPMarginGroup*>& marginGroups() { return m_marginGroups; }

    bool openDataFile(const QString& fileName, QString* error = nullptr);
    bool openMatrixFile(const QString& fileName, QString* error = nullptr);
    // Opens a Bruker processed 2D spectrum (pdata dir, 2rr file or experiment
    // dir) with ppm axes, NMR axis orientation and nucleus axis labels.
    // Returns the new 2D dataset id (kInvalidId on error).
    ObjectId openBruker(const QString& path, QString* error = nullptr);
    PeakTable* peakTable() const { return m_peakTable; }
    // Nominal figure (plot area) size: used by exports that give no size,
    // and the window is sized so the plot area roughly matches it.
    void setFigureSize(const QSize& size);
    QSize figureSize() const { return m_figureSize; }
    void showPeakTable();
    bool exportTo(const QString& fileName, int width = 0, int height = 0, double scale = 2.0,
                  QString* error = nullptr);

signals:
    void closed();
    void currentAxesChanged(int index);
    void panelCreated(int index);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildActions();
    void wireAxes(const Axes& ax);
    PlotRenderer* axesRenderer(int i) const;
    QCPAxisRect* axesRect(int i) const;
    void showContextMenu(ObjectRef ref, QPoint globalPos, QPointF dataPos);
    void onOpen();
    void onOpenMatrix();
    void onOpenBruker();
    void onOpenTimeSeries();
    void updateLevelStatus();
    void onExport();
    void buildStyleMenu();
public:
    // Switches the figure to the timeline layout (stacked rows sharing a time
    // axis, navigator, cursor line). Created on first use.
    Timeline* timeline();
    bool hasTimeline() const { return m_timeline != nullptr; }

    // Applies a built-in style name or a style file to this figure (undoable).
    bool applyStyle(const QString& nameOrPath, QStringList* errors = nullptr);

private:

    QUndoStack* m_stack = nullptr;
    std::vector<Axes> m_axes;
    QCPLayoutGrid* m_grid = nullptr;
    QList<QCPMarginGroup*> m_marginGroups;
    QList<QMetaObject::Connection> m_linkConnections;
    GridSpec m_gridSpec;
    PlotModel* m_model;
    PlotEditor* m_editor;
    QCustomPlot* m_plot;
    PlotRenderer* m_renderer;
    InteractionController* m_controller;
    PropertiesPanel* m_panel;
    QDockWidget* m_dock;
    PeakTable* m_peakTable = nullptr;
    QDockWidget* m_peakDock = nullptr;
    QActionGroup* m_modeGroup = nullptr;
    QComboBox* m_zoomAxes = nullptr;
    QLabel* m_cursorLabel = nullptr;
    QLabel* m_regionLabel = nullptr;
    QLabel* m_levelLabel = nullptr;
    QAction* m_levelsAction = nullptr;
    QString m_lastDir;
    QSize m_figureSize;
    Timeline* m_timeline = nullptr;
};

}  // namespace gp
