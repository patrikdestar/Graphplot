#include "FigureWindow.h"

#include "Icons.h"
#include "PropertiesPanel.h"
#include "commands/PlotEditor.h"
#include "model/BrukerReader.h"
#include "model/DataLoader.h"
#include "model/PlotModel.h"
#include "qcustomplot.h"
#include "render/Exporter.h"
#include "render/PlotRenderer.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QUndoStack>

namespace gp {

namespace {
QString fmt(double v) { return QString::number(v, 'g', 6); }
}

FigureWindow::FigureWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle("Graphplot");
    m_model = new PlotModel(this);
    m_editor = new PlotEditor(m_model, this);
    m_plot = new QCustomPlot(this);
    m_renderer = new PlotRenderer(m_model, m_plot, this);
    m_controller = new InteractionController(m_renderer, m_editor, this);
    setCentralWidget(m_plot);

    m_panel = new PropertiesPanel(m_editor, m_controller);
    m_dock = new QDockWidget("Properties", this);
    m_dock->setObjectName("propertiesDock");
    m_dock->setWidget(m_panel);
    m_dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable |
                        QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_dock);

    buildActions();

    m_cursorLabel = new QLabel;
    m_cursorLabel->setMinimumWidth(240);
    m_regionLabel = new QLabel;
    m_levelLabel = new QLabel;
    statusBar()->addWidget(m_cursorLabel);
    statusBar()->addPermanentWidget(m_levelLabel);
    statusBar()->addPermanentWidget(m_regionLabel);
    for (auto sig : {&PlotModel::matrixAdded, &PlotModel::matrixRemoved,
                     &PlotModel::matrixAppearanceChanged})
        connect(m_model, sig, this, &FigureWindow::updateLevelStatus);
    connect(m_controller, &InteractionController::selectionChanged, this,
            &FigureWindow::updateLevelStatus);

    connect(m_controller, &InteractionController::cursorMoved, this,
            [this](QPointF p, bool inside) {
                m_cursorLabel->setText(inside ? QString("x = %1    y = %2").arg(fmt(p.x()), fmt(p.y()))
                                              : QString());
            });
    connect(m_model, &PlotModel::regionChanged, this, [this] {
        const Region& r = m_model->region();
        m_regionLabel->setText(r.active ? QString("Region: %1 … %2").arg(fmt(r.xmin), fmt(r.xmax))
                                        : QString());
    });
    connect(m_controller, &InteractionController::editRequested, this, [this](ObjectRef ref) {
        m_dock->show();
        m_panel->beginEdit(ref);
    });
    connect(m_controller, &InteractionController::contextMenuRequested, this,
            &FigureWindow::showContextMenu);

    resize(1100, 680);
}

FigureWindow::~FigureWindow() = default;

void FigureWindow::buildActions()
{
    QUndoStack* stack = m_editor->undoStack();

    // ---- File
    QMenu* file = menuBar()->addMenu("&File");
    QAction* open = file->addAction("&Open data…", this, &FigureWindow::onOpen);
    open->setShortcut(QKeySequence::Open);
    file->addAction("Open &2D matrix…", this, &FigureWindow::onOpenMatrix);
    QAction* bruker = file->addAction("Open &Bruker 2D spectrum…", this, &FigureWindow::onOpenBruker);
    bruker->setShortcut(QKeySequence("Ctrl+B"));
    QAction* exportAct = file->addAction(icons::make(icons::Kind::Export), "&Export…", this,
                                         &FigureWindow::onExport);
    exportAct->setShortcut(QKeySequence("Ctrl+E"));
    file->addSeparator();
    QAction* closeAct = file->addAction("&Close", this, &QWidget::close);
    closeAct->setShortcut(QKeySequence::Close);

    // ---- Edit
    QMenu* edit = menuBar()->addMenu("&Edit");
    QAction* undo = stack->createUndoAction(this, "&Undo");
    undo->setShortcut(QKeySequence::Undo);
    undo->setIcon(icons::make(icons::Kind::Undo));
    QAction* redo = stack->createRedoAction(this, "&Redo");
    redo->setShortcuts({QKeySequence::Redo, QKeySequence("Ctrl+Y")});
    redo->setIcon(icons::make(icons::Kind::Redo));
    edit->addAction(undo);
    edit->addAction(redo);
    edit->addSeparator();
    QAction* del = edit->addAction("&Delete selected", m_controller,
                                   &InteractionController::deleteSelection);
    del->setShortcut(QKeySequence::Delete);
    del->setShortcutContext(Qt::WidgetWithChildrenShortcut);  // don't steal Delete from editors
    m_plot->addAction(del);
    edit->addAction("&Clear region selection", m_editor, &PlotEditor::clearRegion);

    // ---- Tools (interaction modes)
    m_modeGroup = new QActionGroup(this);
    m_modeGroup->setExclusive(true);
    struct ModeDef {
        InteractionMode mode;
        icons::Kind icon;
        const char* text;
        const char* key;
        const char* tip;
    };
    const ModeDef modes[] = {
        {InteractionMode::Zoom, icons::Kind::Zoom, "Zoom", "Z",
         "Drag a rectangle to zoom. Wheel zooms anywhere (Ctrl: X only, Shift: Y only)."},
        {InteractionMode::Pan, icons::Kind::Pan, "Pan", "P",
         "Drag to pan. The middle mouse button pans in every mode."},
        {InteractionMode::Region, icons::Kind::Region, "Region", "R",
         "Drag along X to select a region. Drag its edges to adjust, Shift+drag inside to move."},
        {InteractionMode::AddVMarker, icons::Kind::VMarker, "V-marker", "V",
         "Click to place a vertical marker."},
        {InteractionMode::AddHMarker, icons::Kind::HMarker, "H-marker", "H",
         "Click to place a horizontal marker."},
        {InteractionMode::AddAnnotation, icons::Kind::Annotation, "Text", "T",
         "Click to place a text annotation."},
    };
    QMenu* tools = menuBar()->addMenu("&Tools");
    QToolBar* tb = addToolBar("Main");
    tb->setObjectName("mainToolbar");
    tb->setMovable(false);
    tb->setIconSize(QSize(22, 22));
    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    for (const ModeDef& d : modes) {
        QAction* a = new QAction(icons::make(d.icon), d.text, this);
        a->setCheckable(true);
        a->setShortcut(QKeySequence(d.key));
        a->setToolTip(QString("%1 (%2)\n%3").arg(d.text, d.key, d.tip));
        a->setData(int(d.mode));
        a->setChecked(d.mode == m_controller->mode());
        m_modeGroup->addAction(a);
        tools->addAction(a);
        tb->addAction(a);
        if (d.mode == InteractionMode::Region) tb->addSeparator();
        connect(a, &QAction::triggered, this,
                [this, mode = d.mode] { m_controller->setMode(mode); });
    }
    connect(m_controller, &InteractionController::modeChanged, this, [this](InteractionMode m) {
        for (QAction* a : m_modeGroup->actions())
            if (a->data().toInt() == int(m)) a->setChecked(true);
    });
    tb->addSeparator();

    // ---- View
    QMenu* view = menuBar()->addMenu("&View");
    QAction* autoscale = new QAction(icons::make(icons::Kind::Autoscale), "Autoscale", this);
    autoscale->setShortcut(QKeySequence("A"));
    autoscale->setToolTip("Fit all visible data (A). Double-click the plot does the same.");
    connect(autoscale, &QAction::triggered, this, [this] { m_renderer->autoscale(); });
    QAction* autoX = new QAction("Autoscale X", this);
    connect(autoX, &QAction::triggered, this, [this] { m_renderer->autoscale(true, false); });
    QAction* autoY = new QAction("Autoscale Y", this);
    autoY->setShortcut(QKeySequence("Shift+A"));
    connect(autoY, &QAction::triggered, this, [this] { m_renderer->autoscale(false, true); });
    QAction* reset = new QAction(icons::make(icons::Kind::ResetView), "Reset view", this);
    reset->setShortcut(QKeySequence("Home"));
    reset->setToolTip("Return to the original axis limits (Home).");
    connect(reset, &QAction::triggered, this, [this] { m_renderer->resetView(); });
    QAction* setHome = new QAction("Set current view as home", this);
    connect(setHome, &QAction::triggered, this, [this] { m_renderer->captureHomeView(); });
    m_levelsAction = new QAction(icons::make(icons::Kind::Levels), "Levels", this);
    m_levelsAction->setCheckable(true);
    m_levelsAction->setShortcut(QKeySequence("L"));
    m_levelsAction->setToolTip(
        "Mouse wheel scrolls contour levels of 2D data instead of zooming (L).\n"
        "Alt+wheel always scrolls levels. Keys: '*' lowers levels x2, '/' raises them x2.\n"
        "Applies to the selected 2D dataset, or to all when none is selected.");
    connect(m_levelsAction, &QAction::toggled, m_controller,
            &InteractionController::setWheelAdjustsLevels);
    connect(m_controller, &InteractionController::wheelAdjustsLevelsChanged, m_levelsAction,
            &QAction::setChecked);
    QAction* lowerLevels = new QAction("Lower contour levels ÷2", this);
    lowerLevels->setShortcut(QKeySequence("*"));
    connect(lowerLevels, &QAction::triggered, this, [this] { m_controller->scaleContourLevels(0.5); });
    QAction* raiseLevels = new QAction("Raise contour levels ×2", this);
    raiseLevels->setShortcut(QKeySequence("/"));
    connect(raiseLevels, &QAction::triggered, this, [this] { m_controller->scaleContourLevels(2.0); });
    view->addAction(m_levelsAction);
    view->addAction(lowerLevels);
    view->addAction(raiseLevels);
    view->addSeparator();
    view->addAction(autoscale);
    view->addAction(autoX);
    view->addAction(autoY);
    view->addAction(reset);
    view->addAction(setHome);
    view->addSeparator();
    view->addAction(m_dock->toggleViewAction());
    tb->addAction(autoscale);
    tb->addAction(reset);
    tb->addAction(m_levelsAction);

    // Zoom axes selector
    tb->addSeparator();
    m_zoomAxes = new QComboBox;
    m_zoomAxes->addItem("Zoom X + Y", int(ZoomAxes::Both));
    m_zoomAxes->addItem("Zoom X only", int(ZoomAxes::XOnly));
    m_zoomAxes->addItem("Zoom Y only", int(ZoomAxes::YOnly));
    m_zoomAxes->setToolTip("Axes affected by wheel and rectangle zoom");
    m_zoomAxes->setFocusPolicy(Qt::NoFocus);
    connect(m_zoomAxes, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_controller->setZoomAxes(ZoomAxes(m_zoomAxes->itemData(i).toInt()));
    });
    tb->addWidget(m_zoomAxes);
    tb->addSeparator();
    tb->addAction(undo);
    tb->addAction(redo);
    tb->addAction(exportAct);
}

void FigureWindow::showContextMenu(ObjectRef ref, QPoint globalPos, QPointF dataPos)
{
    QMenu menu(this);
    if (ref.isValid()) {
        if (ref.kind != ObjectKind::Region) {
            menu.addAction("Edit…", this, [this, ref] {
                m_dock->show();
                m_panel->beginEdit(ref);
            });
        }
        menu.addAction(ref.kind == ObjectKind::Region ? "Clear selection" : "Delete", this,
                       [this, ref] { m_editor->removeObject(ref); });
        menu.addSeparator();
    }
    menu.addAction("Vertical marker here", this, [this, dataPos] {
        Marker m;
        m.position = dataPos.x();
        m.color = paletteColor(7 + int(m_model->markers().size()));
        m.label = QString("M%1").arg(m_model->markers().size() + 1);
        m_controller->setSelection({ObjectKind::Marker, m_editor->addMarker(m)});
    });
    menu.addAction("Horizontal marker here", this, [this, dataPos] {
        Marker m;
        m.orientation = Orientation::Horizontal;
        m.position = dataPos.y();
        m.color = paletteColor(7 + int(m_model->markers().size()));
        m.label = QString("M%1").arg(m_model->markers().size() + 1);
        m_controller->setSelection({ObjectKind::Marker, m_editor->addMarker(m)});
    });
    menu.addAction("Annotation here", this, [this, dataPos] {
        Annotation a;
        a.x = dataPos.x();
        a.y = dataPos.y();
        a.text = "Annotation";
        const ObjectRef r{ObjectKind::Annotation, m_editor->addAnnotation(a)};
        m_controller->setSelection(r);
        m_dock->show();
        m_panel->beginEdit(r);
    });
    menu.addSeparator();
    menu.addAction("Autoscale", this, [this] { m_renderer->autoscale(); });
    menu.addAction("Reset view", this, [this] { m_renderer->resetView(); });
    menu.exec(globalPos);
}

bool FigureWindow::openDataFile(const QString& fileName, QString* error)
{
    LoadResult r = loadColumnFile(fileName);
    if (!r.error.isEmpty()) {
        if (error) *error = r.error;
        return false;
    }
    m_editor->undoStack()->beginMacro(QString("Open %1").arg(QFileInfo(fileName).fileName()));
    for (Dataset& d : r.datasets) {
        d.style.color = QColor();  // palette colour
        m_editor->addDataset(std::move(d));
    }
    m_editor->undoStack()->endMacro();
    m_renderer->autoscale();
    return true;
}

bool FigureWindow::openMatrixFile(const QString& fileName, QString* error)
{
    MatrixLoadResult r = loadMatrixFile(fileName);
    if (!r.error.isEmpty()) {
        if (error) *error = r.error;
        return false;
    }
    m_editor->addMatrix(std::move(r.matrix));
    m_renderer->autoscale();
    return true;
}

ObjectId FigureWindow::openBruker(const QString& path, QString* error)
{
    BrukerSpectrum s = readBruker2D(path);
    if (!s.error.isEmpty()) {
        if (error) *error = s.error;
        return kInvalidId;
    }
    m_editor->undoStack()->beginMacro("Open Bruker spectrum");
    const ObjectId id = m_editor->addMatrix(std::move(s.matrix));
    AxesInfo a = m_model->axesInfo();
    auto axisLabel = [](const QString& nuc, const char* dim) {
        return nuc.isEmpty() ? QString("%1 (ppm)").arg(dim) : QString("%1 %2 (ppm)").arg(dim, nuc);
    };
    a.xLabel = axisLabel(s.nucleusF2, "F2");
    a.yLabel = axisLabel(s.nucleusF1, "F1");
    a.xReversed = true;  // NMR convention: ppm decreases to the right/up
    a.yReversed = true;
    a.showDiagonal = !s.nucleusF1.isEmpty() && s.nucleusF1 == s.nucleusF2;
    if (a.title.isEmpty()) a.title = s.title;
    m_editor->setAxesInfo(a);
    m_editor->undoStack()->endMacro();
    m_renderer->autoscale();
    m_renderer->captureHomeView();
    return id;
}

void FigureWindow::onOpenBruker()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Open Bruker 2D spectrum (select the pdata/N folder or the experiment folder)",
        m_lastDir);
    if (dir.isEmpty()) return;
    m_lastDir = QFileInfo(dir).absolutePath();
    QString err;
    if (openBruker(dir, &err) == kInvalidId) QMessageBox::warning(this, "Open Bruker spectrum", err);
}

void FigureWindow::onOpenMatrix()
{
    const QString fn = QFileDialog::getOpenFileName(
        this, "Open 2D matrix", m_lastDir, "Data files (*.csv *.txt *.dat *.tsv);;All files (*)");
    if (fn.isEmpty()) return;
    m_lastDir = QFileInfo(fn).absolutePath();
    QString err;
    if (!openMatrixFile(fn, &err)) QMessageBox::warning(this, "Open 2D matrix", err);
}

void FigureWindow::updateLevelStatus()
{
    const QList<ObjectId> ids = m_renderer->levelTargets();
    if (ids.isEmpty()) {
        m_levelLabel->clear();
        return;
    }
    const Matrix* m = m_model->matrix(ids.first());
    m_levelLabel->setText(QString("Contour base: %1%2")
                              .arg(QString::number(m->style.baseLevel, 'g', 4),
                                   ids.size() > 1 ? QString(" (+%1 more)").arg(ids.size() - 1)
                                                  : QString()));
}

void FigureWindow::onOpen()
{
    const QString fn = QFileDialog::getOpenFileName(
        this, "Open data", m_lastDir, "Data files (*.csv *.txt *.dat *.tsv);;All files (*)");
    if (fn.isEmpty()) return;
    m_lastDir = QFileInfo(fn).absolutePath();
    QString err;
    if (!openDataFile(fn, &err)) QMessageBox::warning(this, "Open data", err);
}

bool FigureWindow::exportTo(const QString& fileName, int width, int height, double scale,
                            QString* error)
{
    ExportOptions o;
    o.width = width;
    o.height = height;
    o.scale = scale;
    return exportFigure(m_renderer, fileName, o, error);
}

void FigureWindow::onExport()
{
    QString selectedFilter;
    QString fn = QFileDialog::getSaveFileName(
        this, "Export figure", m_lastDir,
        "PNG image (*.png);;SVG vector graphics (*.svg);;PDF document (*.pdf)", &selectedFilter);
    if (fn.isEmpty()) return;
    if (QFileInfo(fn).suffix().isEmpty()) {
        if (selectedFilter.contains("svg")) fn += ".svg";
        else if (selectedFilter.contains("pdf")) fn += ".pdf";
        else fn += ".png";
    }
    m_lastDir = QFileInfo(fn).absolutePath();
    QString err;
    if (!exportTo(fn, 0, 0, 2.0, &err)) QMessageBox::warning(this, "Export", err);
    else statusBar()->showMessage(QString("Exported %1").arg(fn), 4000);
}

void FigureWindow::closeEvent(QCloseEvent* e)
{
    emit closed();
    QMainWindow::closeEvent(e);
}

}  // namespace gp
