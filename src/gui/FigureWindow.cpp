#include "FigureWindow.h"

#include "Icons.h"
#include "PeakTable.h"
#include "PropertiesPanel.h"
#include "StyleEditor.h"
#include "Timeline.h"
#include "TimelinePanel.h"
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
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QSignalBlocker>
#include <QUndoStack>

namespace gp {

namespace {
QString fmt(double v) { return QString::number(v, 'g', 6); }
}

FigureWindow::FigureWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle("Graphplot");
    m_stack = new QUndoStack(this);
    m_stack->setUndoLimit(500);
    m_plot = new QCustomPlot(this);
    m_model = new PlotModel(this);
    m_editor = new PlotEditor(m_model, m_stack, this);
    m_renderer = new PlotRenderer(m_model, m_plot, this);
    m_axes.push_back({m_model, m_editor, m_renderer});
    m_controller = new InteractionController(m_renderer, m_editor, this);
    setCentralWidget(m_plot);
    connect(m_controller, &InteractionController::activeTargetChanged, this,
            &FigureWindow::setCurrentAxes);

    m_panel = new PropertiesPanel(m_editor, m_controller);
    m_dock = new QDockWidget("Properties", this);
    m_dock->setObjectName("propertiesDock");
    m_dock->setWidget(m_panel);
    m_dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable |
                        QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_dock);

    m_peakTable = new PeakTable(m_editor, m_renderer, m_controller);
    m_peakDock = new QDockWidget("Peaks", this);
    m_peakDock->setObjectName("peaksDock");
    m_peakDock->setWidget(m_peakTable);
    addDockWidget(Qt::RightDockWidgetArea, m_peakDock);
    tabifyDockWidget(m_dock, m_peakDock);
    m_dock->raise();
    buildActions();

    m_cursorLabel = new QLabel;
    m_cursorLabel->setMinimumWidth(240);
    m_regionLabel = new QLabel;
    m_levelLabel = new QLabel;
    statusBar()->addWidget(m_cursorLabel);
    statusBar()->addPermanentWidget(m_levelLabel);
    statusBar()->addPermanentWidget(m_regionLabel);
    wireAxes(m_axes.front());
    connect(m_controller, &InteractionController::selectionChanged, this,
            &FigureWindow::updateLevelStatus);

    connect(m_controller, &InteractionController::cursorMoved, this,
            [this](QPointF p, bool inside) {
                m_cursorLabel->setText(inside ? QString("x = %1    y = %2").arg(fmt(p.x()), fmt(p.y()))
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

void FigureWindow::wireAxes(const Axes& ax)
{
    PlotModel* model = ax.model;
    // Show the peak list automatically once there are peaks.
    connect(model, &PlotModel::peakAdded, this, [this] {
        if (m_peakDock->isHidden()) m_peakDock->show();
    });
    for (auto sig : {&PlotModel::matrixAdded, &PlotModel::matrixRemoved,
                     &PlotModel::matrixAppearanceChanged})
        connect(model, sig, this, &FigureWindow::updateLevelStatus);
    connect(model, &PlotModel::regionChanged, this, [this, model] {
        if (model != m_model) return;
        const Region& r = model->region();
        m_regionLabel->setText(r.active ? QString("Region: %1 … %2").arg(fmt(r.xmin), fmt(r.xmax))
                                        : QString());
    });
}

void FigureWindow::setCurrentAxes(int index)
{
    if (index < 0 || index >= int(m_axes.size())) return;
    const Axes& ax = m_axes[size_t(index)];
    if (ax.model == m_model) return;
    m_model = ax.model;
    m_editor = ax.editor;
    m_renderer = ax.renderer;
    m_controller->setActiveTarget(index);
    // The side panels show the active panel's objects: rebuild them.
    auto* panel = new PropertiesPanel(m_editor, m_controller);
    m_dock->setWidget(panel);
    m_panel->deleteLater();
    m_panel = panel;
    auto* table = new PeakTable(m_editor, m_renderer, m_controller);
    m_peakDock->setWidget(table);
    m_peakTable->deleteLater();
    m_peakTable = table;
    updateLevelStatus();
    emit currentAxesChanged(index);
}

int FigureWindow::currentAxes() const
{
    for (size_t i = 0; i < m_axes.size(); ++i)
        if (m_axes[i].model == m_model) return int(i);
    return 0;
}

void FigureWindow::setGrid(const GridSpec& spec)
{
    const int rows = std::max(1, spec.rows), cols = std::max(1, spec.cols);
    ensureGrid();
    // Create the missing panels (existing ones keep their content).
    while (int(m_axes.size()) < rows * cols) createPanel();
    // Place panels row-major.
    for (const Axes& ax : m_axes)
        if (m_grid->elements(false).contains(ax.renderer->axisRect())) m_grid->take(ax.renderer->axisRect());
    m_grid->simplify();
    for (size_t i = 0; i < m_axes.size() && int(i) < rows * cols; ++i)
        m_grid->addElement(int(i) / cols, int(i) % cols, m_axes[i].renderer->axisRect());
    if (spec.heightRatios.size() == rows) m_grid->setRowStretchFactors(spec.heightRatios);
    if (spec.widthRatios.size() == cols) m_grid->setColumnStretchFactors(spec.widthRatios);
    m_grid->setRowSpacing(spec.hspace >= 0 ? spec.hspace : 8);
    m_grid->setColumnSpacing(spec.wspace >= 0 ? spec.wspace : 8);

    // Align the frames: same left/right margins per column, top/bottom per row.
    for (QCPMarginGroup* g : m_marginGroups) delete g;
    m_marginGroups.clear();
    for (int c = 0; c < cols; ++c) {
        auto* g = new QCPMarginGroup(m_plot);
        m_marginGroups << g;
        for (int r = 0; r < rows; ++r)
            if (auto* rect = axesRect(r * cols + c)) rect->setMarginGroup(QCP::msLeft | QCP::msRight, g);
    }
    for (int r = 0; r < rows; ++r) {
        auto* g = new QCPMarginGroup(m_plot);
        m_marginGroups << g;
        for (int c = 0; c < cols; ++c)
            if (auto* rect = axesRect(r * cols + c)) rect->setMarginGroup(QCP::msTop | QCP::msBottom, g);
    }

    // Shared axes: link ranges; inner panels drop duplicate tick labels.
    for (const QMetaObject::Connection& c : m_linkConnections) disconnect(c);
    m_linkConnections.clear();
    auto link = [this](QCPAxis* a, QCPAxis* b) {
        m_linkConnections << connect(a, qOverload<const QCPRange&>(&QCPAxis::rangeChanged), b,
                                     qOverload<const QCPRange&>(&QCPAxis::setRange));
        m_linkConnections << connect(b, qOverload<const QCPRange&>(&QCPAxis::rangeChanged), a,
                                     qOverload<const QCPRange&>(&QCPAxis::setRange));
    };
    for (int i = 0; i < rows * cols && i < int(m_axes.size()); ++i) {
        PlotRenderer* r = m_axes[size_t(i)].renderer;
        const int row = i / cols, col = i % cols;
        const bool hideX = spec.sharex && row < rows - 1;
        const bool hideY = spec.sharey && col > 0;
        r->xAxis()->setTickLabels(!hideX);
        r->yAxis()->setTickLabels(!hideY);
        QCPAxisRect* rect = r->axisRect();
        // Zero spacing (ratio panels): frames touch, no margin in between.
        QCP::MarginSides autoSides = QCP::msAll;
        QMargins fixed = rect->margins();
        if (spec.hspace == 0 && row > 0) autoSides &= ~QCP::msTop, fixed.setTop(0);
        if (spec.hspace == 0 && row < rows - 1) autoSides &= ~QCP::msBottom, fixed.setBottom(0);
        if (spec.wspace == 0 && col > 0) autoSides &= ~QCP::msLeft, fixed.setLeft(0);
        if (spec.wspace == 0 && col < cols - 1) autoSides &= ~QCP::msRight, fixed.setRight(0);
        rect->setAutoMargins(autoSides);
        rect->setMargins(fixed);
        if (spec.sharex && col == 0 && row > 0) {
            for (int c2 = 0; c2 < cols; ++c2)
                if (auto* other = axesRenderer(c2)) link(other->xAxis(), axesRenderer(row * cols + c2)->xAxis());
        }
        if (spec.sharey && col > 0) link(axesRenderer(row * cols)->yAxis(), r->yAxis());
    }
    m_gridSpec = spec;
    m_plot->replot();
}

Timeline* FigureWindow::timeline()
{
    if (m_timeline) return m_timeline;
    m_timeline = new Timeline(this);
    auto* panel = new TimelinePanel(m_timeline);
    auto* dock = new QDockWidget("Timeline", this);
    dock->setObjectName("timelineDock");
    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    tabifyDockWidget(m_dock, dock);
    dock->show();
    dock->raise();

    QMenu* menu = menuBar()->addMenu("T&imeline");
    QAction* pin = menu->addAction("Add time marker at cursor", this, [this] {
        const auto [a, b] = m_timeline->window();
        m_timeline->addPin(m_timeline->cursorTime().value_or(0.5 * (a + b)));
    });
    pin->setShortcut(QKeySequence("M"));
    addAction(pin);
    QAction* all = menu->addAction("Show whole recording", m_timeline, &Timeline::showAll);
    all->setShortcut(QKeySequence("Shift+Home"));
    QAction* fit = menu->addAction("Auto-fit Y to window");
    fit->setCheckable(true);
    fit->setChecked(m_timeline->autoFitY());
    connect(fit, &QAction::toggled, m_timeline, &Timeline::setAutoFitY);
    QAction* clock = menu->addAction("Clock time");
    clock->setCheckable(true);
    connect(clock, &QAction::toggled, m_timeline, &Timeline::setClockTime);
    clock->setEnabled(m_timeline->hasAbsoluteTime());
    connect(m_timeline, &Timeline::optionsChanged, this, [this, clock, fit] {
        const QSignalBlocker b1(clock), b2(fit);
        clock->setEnabled(m_timeline->hasAbsoluteTime());
        clock->setChecked(m_timeline->clockTime());
        fit->setChecked(m_timeline->autoFitY());
    });
    connect(m_timeline, &Timeline::cursorChanged, this, [this](std::optional<double> t) {
        if (!t) return;
        const auto [a, b] = m_timeline->window();
        m_cursorLabel->setText("t = " + m_timeline->formatTime(*t, (b - a) / 1000.0));
    });
    return m_timeline;
}

void FigureWindow::onOpenTimeSeries()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, "Open time series (one or more CSV logs)", m_lastDir,
        "Data files (*.csv *.txt *.tsv *.dat);;All files (*)");
    if (files.isEmpty()) return;
    m_lastDir = QFileInfo(files.front()).absolutePath();
    // A figure that already holds other plots opens the logs in a new window.
    FigureWindow* target = this;
    const bool empty = m_axes.size() == 1 && m_model->datasets().empty() && m_model->matrices().empty();
    if (!m_timeline && !empty) {
        target = new FigureWindow;
        target->setAttribute(Qt::WA_DeleteOnClose);
        target->show();
    }
    QString err;
    if (!target->timeline()->loadCsv(files, false, &err))
        QMessageBox::warning(target, "Open time series", err);
}

void FigureWindow::ensureGrid()
{
    if (m_grid) return;
    // Replace the single default axis rect by a grid holding all panels.
    QCPLayoutGrid* outer = m_plot->plotLayout();
    QCPAxisRect* first = m_axes.front().renderer->axisRect();
    int row = 0, col = 0;
    for (int r = 0; r < outer->rowCount(); ++r)
        for (int c = 0; c < outer->columnCount(); ++c)
            if (outer->element(r, c) == first) row = r, col = c;
    outer->take(first);
    m_grid = new QCPLayoutGrid;
    outer->addElement(row, col, m_grid);
    m_grid->addElement(0, 0, first);
}

int FigureWindow::createPanel()
{
    auto* rect = new QCPAxisRect(m_plot, true);
    auto* model = new PlotModel(this);
    model->setStyle(m_axes.front().model->style());
    AxesInfo info = model->axesInfo();
    info.gridVisible = m_axes.front().model->axesInfo().gridVisible;
    model->setAxesInfo(info);
    auto* editor = new PlotEditor(model, m_stack, this);
    auto* renderer = new PlotRenderer(model, m_plot, rect, false, this);
    m_axes.push_back({model, editor, renderer});
    m_controller->addTarget(renderer, editor);
    wireAxes(m_axes.back());
    emit panelCreated(int(m_axes.size()) - 1);
    return int(m_axes.size()) - 1;
}

PlotRenderer* FigureWindow::axesRenderer(int i) const
{
    return i >= 0 && i < int(m_axes.size()) ? m_axes[size_t(i)].renderer : nullptr;
}

QCPAxisRect* FigureWindow::axesRect(int i) const
{
    PlotRenderer* r = axesRenderer(i);
    return r ? r->axisRect() : nullptr;
}

void FigureWindow::buildActions()
{
    QUndoStack* stack = m_stack;

    // ---- File
    QMenu* file = menuBar()->addMenu("&File");
    QAction* open = file->addAction("&Open data…", this, &FigureWindow::onOpen);
    open->setShortcut(QKeySequence::Open);
    file->addAction("Open &2D matrix…", this, &FigureWindow::onOpenMatrix);
    QAction* openTs = file->addAction("Open &time series (CSV)…", this, &FigureWindow::onOpenTimeSeries);
    openTs->setShortcut(QKeySequence("Ctrl+T"));
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
    // Keep the toolbar buttons short; the menu shows "Undo <action>".
    undo->setIconText("Undo");
    redo->setIconText("Redo");
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
        {InteractionMode::AddPeak, icons::Kind::Peak, "Peak", "K",
         "Click to add peaks: snaps to the nearest maximum (Shift: exactly at the cursor).\n"
         "Stays active until Esc. Drag a peak or its label to move it."},
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
    QAction* pickAct = new QAction("Pick peaks", this);
    pickAct->setShortcut(QKeySequence("Ctrl+Shift+K"));
    pickAct->setToolTip("Automatic peak picking with the options of the Peaks panel");
    connect(pickAct, &QAction::triggered, this, [this] {
        m_peakDock->show();
        m_peakDock->raise();
        m_peakTable->pickNow();
    });
    QAction* labelsAct = new QAction("Show peak labels", this);
    labelsAct->setCheckable(true);
    labelsAct->setChecked(true);
    connect(labelsAct, &QAction::toggled, this, [this](bool on) {
        AxesInfo a = m_model->axesInfo();
        a.showPeakLabels = on;
        m_editor->setAxesInfo(a);
    });
    connect(m_model, &PlotModel::axesInfoChanged, labelsAct, [this, labelsAct] {
        const QSignalBlocker b(labelsAct);
        labelsAct->setChecked(m_model->axesInfo().showPeakLabels);
    });
    tools->addSeparator();
    tools->addAction(pickAct);
    view->addAction(labelsAct);
    view->addAction(m_peakDock->toggleViewAction());
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
    buildStyleMenu();
    QMenu* help = menuBar()->addMenu("&Help");
    help->addAction("&About Graphplot", this, [this] {
        QMessageBox::about(
            this, "About Graphplot",
            "<h3>Graphplot</h3>"
            "<p>Interactive scientific plotting with Python control.</p>"
            "<p>Author: <b>Justinas Minkevicius</b><br>"
            "<a href='mailto:oompagoo@gmail.com'>oompagoo@gmail.com</a></p>"
            "<p>Copyright &copy; 2026 Justinas Minkevicius. All rights reserved.<br>"
            "Any use without the author's written permission is prohibited.</p>"
            "<p>Includes QCustomPlot (GPLv3) by Emanuel Eichhammer and uses Qt.</p>");
    });
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
    connect(m_controller, &InteractionController::zoomAxesChanged, this, [this](ZoomAxes a) {
        const QSignalBlocker b(m_zoomAxes);
        m_zoomAxes->setCurrentIndex(m_zoomAxes->findData(int(a)));
    });
    tb->addWidget(m_zoomAxes);
    tb->addSeparator();
    tb->addAction(undo);
    tb->addAction(redo);
    tb->addAction(exportAct);
}

bool FigureWindow::applyStyle(const QString& nameOrPath, QStringList* errors)
{
    Style s;
    if (!loadStyle(nameOrPath, &s, errors)) return false;
    const QString text = QString("Style: %1").arg(QFileInfo(nameOrPath).completeBaseName());
    m_stack->beginMacro(text);
    for (const Axes& ax : m_axes) ax.editor->setStyle(s, text);  // the style is figure-wide
    m_stack->endMacro();
    return true;
}

void FigureWindow::buildStyleMenu()
{
    QMenu* menu = menuBar()->addMenu("&Style");
    for (const QString& name : builtinStyleNames()) {
        QString text = name;
        text[0] = text[0].toUpper();
        menu->addAction(text, this, [this, name] { applyStyle(name); });
    }
    menu->addSeparator();
    menu->addAction("&Edit style…", this, [this] {
        StyleEditor dlg(m_editor, this);
        dlg.exec();
    });
    menu->addAction("&Load style file…", this, [this] {
        const QString fn = QFileDialog::getOpenFileName(this, "Load style", m_lastDir,
                                                        "Graphplot styles (*.gpstyle);;All files (*)");
        if (fn.isEmpty()) return;
        QStringList errors;
        applyStyle(fn, &errors);
        if (!errors.isEmpty())
            QMessageBox::warning(this, "Load style", "Some entries were ignored:\n" + errors.join("\n"));
    });
    menu->addAction("&Save style as…", this, [this] {
        const QString fn = QFileDialog::getSaveFileName(this, "Save style", m_lastDir + "/my.gpstyle",
                                                        "Graphplot styles (*.gpstyle)");
        QString err;
        if (!fn.isEmpty() && !saveStyle(fn, m_model->style(), &err))
            QMessageBox::warning(this, "Save style", err);
    });
    menu->addSeparator();
    menu->addAction("Use as my &default style", this, [this] {
        const QString path = userDefaultStylePath();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QString err;
        if (!saveStyle(path, m_model->style(), &err)) {
            QMessageBox::warning(this, "Default style", err);
            return;
        }
        setDefaultStyle(m_model->style());
        statusBar()->showMessage("New figures will use this style (saved to " + path + ")", 6000);
    });
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

void FigureWindow::showPeakTable()
{
    m_peakDock->show();
    m_peakDock->raise();
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

void FigureWindow::setFigureSize(const QSize& size)
{
    m_figureSize = size;
    // Toolbar, status bar and the side panel come on top of the plot area.
    const int panel = m_dock && m_dock->isVisible() ? m_dock->sizeHint().width() : 330;
    resize(size.width() + panel, size.height() + 110);
}

bool FigureWindow::exportTo(const QString& fileName, int width, int height, double scale,
                            QString* error)
{
    // No explicit size: the nominal figure size if set, else the widget size
    // (which is only meaningful once the window has been laid out).
    if (width <= 0 && height <= 0 && m_figureSize.isValid()) {
        width = m_figureSize.width();
        height = m_figureSize.height();
    }
    if ((width <= 0 || height <= 0) && (m_plot->width() < 100 || m_plot->height() < 100)) {
        width = width > 0 ? width : 800;
        height = height > 0 ? height : 600;
    }
    ExportOptions o;
    o.width = width;
    o.height = height;
    o.scale = scale;
    for (const Axes& ax : m_axes) ax.renderer->setExportMode(true);
    const bool ok = exportFigure(m_axes.front().renderer, fileName, o, error);
    for (const Axes& ax : m_axes) ax.renderer->setExportMode(false);
    return ok;
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
