// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "FigureWindow.h"

#include "Appearance.h"
#include "Explanations.h"
#include "model/DataExport.h"

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
#include <QSettings>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QMimeData>
#include <QTimer>
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
    limitPlainKeyShortcuts();
    applyExplanations(this);  // hover explanations for every control
    setAcceptDrops(true);
    m_plot->setFocus();

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

    // Status bar: cursor coordinates, and the distance from the last click
    // (measure a peak width or a delay: click one point, hover the other).
    connect(m_controller, &InteractionController::clicked, this, [this](QPointF p, int panel) {
        m_clickData = p;
        m_clickPanel = panel;
    });
    connect(m_controller, &InteractionController::activeTargetChanged, this, [this] { m_clickData.reset(); });
    connect(m_controller, &InteractionController::cursorMoved, this,
            [this](QPointF p, bool inside, int panel) {
                QString text;
                if (inside) {
                    text = QString("x = %1    y = %2").arg(fmt(p.x()), fmt(p.y()));
                    // Distances only within the panel of the click (other
                    // panels have other coordinates).
                    if (m_clickData && panel == m_clickPanel && *m_clickData != p)
                        text += QString("    %1x = %2    %1y = %3")
                                    .arg(QChar(0x0394), fmt(p.x() - m_clickData->x()), fmt(p.y() - m_clickData->y()));
                }
                m_cursorLabel->setText(text);
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
    PlotRenderer* renderer = ax.renderer;
    connect(renderer, &PlotRenderer::viewChanged, this, [this, renderer] { recordView(renderer); });
    PlotModel* model = ax.model;
    // Shared axes share their direction too (invert_xaxis on one panel flips
    // all panels sharing that x). Partners follow through their models, so
    // undo of the original change brings them back as well.
    connect(model, &PlotModel::axesInfoChanged, this, [this, renderer, model] {
        if (!m_syncDirections) return;
        const AxesInfo& info = model->axesInfo();
        for (PlotRenderer* p : renderer->sharedX())
            if (p->model()->axesInfo().xReversed != info.xReversed) {
                AxesInfo o = p->model()->axesInfo();
                o.xReversed = info.xReversed;
                p->model()->setAxesInfo(o);
            }
        for (PlotRenderer* p : renderer->sharedY())
            if (p->model()->axesInfo().yReversed != info.yReversed) {
                AxesInfo o = p->model()->axesInfo();
                o.yReversed = info.yReversed;
                p->model()->setAxesInfo(o);
            }
    });
    // Show the peak list automatically once there are peaks.
    connect(model, &PlotModel::peakAdded, this, [this] {
        if (m_peakDock->isHidden()) m_peakDock->show();
    });
    for (auto sig : {&PlotModel::matrixAdded, &PlotModel::matrixRemoved,
                     &PlotModel::matrixAppearanceChanged})
        connect(model, sig, this, &FigureWindow::updateLevelStatus);
    connect(model, &PlotModel::regionChanged, this, [this, model] {
        if (model == m_model) updateRegionLabel();
    });
}

// ---- view history -------------------------------------------------------------

void FigureWindow::recordView(PlotRenderer* r)
{
    ViewHistory& h = m_history[r];
    if (!h.settle) {
        // Debounce: a wheel scroll or a pan emits many view changes; only the
        // view where the user paused (~0.4 s) becomes a history entry.
        h.settle = new QTimer(this);
        h.settle->setSingleShot(true);
        h.settle->setInterval(400);
        connect(h.settle, &QTimer::timeout, this, [this, r] { commitView(r); });
    }
    h.settle->start();
}

void FigureWindow::commitView(PlotRenderer* r)
{
    ViewHistory& h = m_history[r];
    if (h.settle) h.settle->stop();
    const ViewRange now = r->viewRange();
    // The history starts with the first view that shows data (not the empty
    // default range of a fresh panel).
    if (!h.started) {
        h.started = !r->model()->datasets().empty() || !r->model()->matrices().empty();
        h.last = now;
        return;
    }
    if (now == h.last) return;  // nothing new (e.g. the result of Back / Forward)
    h.back << h.last;
    if (h.back.size() > 100) h.back.removeFirst();
    h.forward.clear();
    h.last = now;
}

void FigureWindow::stepView(int direction)
{
    ViewHistory& h = m_history[m_renderer];
    QList<ViewRange>& from = direction < 0 ? h.back : h.forward;
    QList<ViewRange>& to = direction < 0 ? h.forward : h.back;
    if (h.settle && h.settle->isActive()) commitView(m_renderer);  // still settling
    if (from.isEmpty()) return;
    to << h.last;
    h.last = from.takeLast();
    m_renderer->setViewRange(h.last);
}

void FigureWindow::setExportMode(bool on)
{
    for (const Axes& ax : m_axes) ax.renderer->setExportMode(on);
    emit exportModeChanged(on);  // the timeline's own items (navigator, marker labels)
}

void FigureWindow::copyToClipboard()
{
    // Same size and look as File > Export (nominal figure size, export mode).
    const QSize size = m_figureSize.isValid() ? m_figureSize : m_plot->size();
    setExportMode(true);
    const QPixmap pm = m_plot->toPixmap(size.width(), size.height(), 2.0);
    setExportMode(false);
    m_plot->replot();
    QGuiApplication::clipboard()->setImage(pm.toImage());
    statusBar()->showMessage("Figure copied to the clipboard", 3000);
}

// ---- drag & drop ----------------------------------------------------------------

void FigureWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void FigureWindow::dropEvent(QDropEvent* e)
{
    QStringList paths;
    for (const QUrl& url : e->mimeData()->urls())
        if (url.isLocalFile()) paths << url.toLocalFile();
    if (paths.isEmpty()) return;  // nothing we can open: refuse the drop
    // Always a copy: accepting a "move" would let the source delete the file.
    e->setDropAction(Qt::CopyAction);
    e->accept();
    // Open after the drop returns, so the drag source isn't blocked while
    // files load or an error box is shown.
    QTimer::singleShot(0, this, [this, paths] { openPaths(paths); });
}

void FigureWindow::openPaths(const QStringList& paths)
{
    QStringList errors, logs;
    for (const QString& path : paths) {
        const QFileInfo fi(path);
        QString err;
        if (fi.suffix().compare("gpstyle", Qt::CaseInsensitive) == 0) {
            QStringList errs;
            applyStyle(path, &errs);
            err = errs.join("; ");
        } else if (fi.suffix().compare("gpfig", Qt::CaseInsensitive) == 0) {
            openFigureFile(path);  // reports its own errors
        } else if (fi.isDir() || fi.fileName() == "2rr") {
            if (openBruker(path, &err) != kInvalidId) err.clear(), addRecent("bruker", {path});
        } else if (m_timeline) {
            logs << path;  // loaded together below (one time origin, one undo step)
        } else if (openDataFile(path, &err)) {
            err.clear();
            addRecent("data", {path});
        }
        if (!err.isEmpty()) errors << fi.fileName() + ": " + err;
    }
    QString err;
    if (!logs.isEmpty() && !m_timeline->loadCsv(logs, false, &err)) errors << err;
    else if (!logs.isEmpty()) addRecent("timeseries", logs);
    if (!errors.isEmpty()) QMessageBox::warning(this, "Open", errors.join("\n"));
}

void FigureWindow::updateRegionLabel()
{
    const Region& r = m_model->region();
    m_regionLabel->setText(r.active ? QString("Region: %1 … %2").arg(fmt(r.xmin), fmt(r.xmax))
                                    : QString());
}

void FigureWindow::limitPlainKeyShortcuts()
{
    // Single-key shortcuts (Z, P, A, M, *, Home, ...) must not fire while the
    // user types in a table cell or tree item of a side panel: make them
    // active only while the plot (or the menu) has focus.
    for (QAction* a : findChildren<QAction*>()) {
        const QKeySequence ks = a->shortcut();
        if (ks.isEmpty()) continue;
        const Qt::KeyboardModifiers mods = ks[0].keyboardModifiers() & ~Qt::KeypadModifier;
        if (mods != Qt::NoModifier && mods != Qt::ShiftModifier) continue;
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        m_plot->addAction(a);
    }
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
    updateRegionLabel();
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
    // A figure named at creation (figure("Run 42")) keeps that name as the
    // figure-wide title once it has several panels, not as panel 0's title.
    if (rows * cols > 1 && !m_axes.empty()) {
        AxesInfo info = m_axes.front().model->axesInfo();
        if (!info.title.isEmpty() && info.title == windowTitle() && info.suptitle.isEmpty()) {
            info.suptitle = info.title;
            info.title.clear();
            m_axes.front().model->setAxesInfo(info);
        }
    }
    ensureGrid();
    // Grid cells hold the ordinary panels in creation order; twin panels
    // (twinx) are drawn over their host and follow it.
    auto cells = [this] {
        QList<int> out;
        for (size_t i = 0; i < m_axes.size(); ++i)
            if (m_axes[i].twinOf < 0) out << int(i);
        return out;
    };
    // Create the missing panels (existing ones keep their content).
    while (cells().size() < rows * cols) createPanel();
    const QList<int> cell = cells();
    // Place panels row-major.
    for (int i : cell)
        if (m_grid->elements(false).contains(m_axes[size_t(i)].renderer->axisRect()))
            m_grid->take(m_axes[size_t(i)].renderer->axisRect());
    m_grid->simplify();
    for (int k = 0; k < cell.size(); ++k) {
        PlotRenderer* r = m_axes[size_t(cell[k])].renderer;
        const bool inGrid = k < rows * cols;
        // Panels beyond a smaller grid keep their content (undo, Python
        // handles) but must not be drawn at their old position.
        r->setPanelVisible(inGrid);
        // Several panels: each shows its own title; the header the suptitle.
        r->setOwnTitle(rows * cols > 1);
        if (inGrid) m_grid->addElement(k / cols, k % cols, r->axisRect());
    }
    auto cellRect = [&](int k) { return k < cell.size() ? m_axes[size_t(cell[k])].renderer->axisRect() : nullptr; };
    auto cellRenderer = [&](int k) { return k < cell.size() ? m_axes[size_t(cell[k])].renderer : nullptr; };
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
            if (auto* rect = cellRect(r * cols + c)) rect->setMarginGroup(QCP::msLeft | QCP::msRight, g);
    }
    for (int r = 0; r < rows; ++r) {
        auto* g = new QCPMarginGroup(m_plot);
        m_marginGroups << g;
        for (int c = 0; c < cols; ++c)
            if (auto* rect = cellRect(r * cols + c)) rect->setMarginGroup(QCP::msTop | QCP::msBottom, g);
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
    // The panel each panel's axis is linked to (its group's first panel), or -1.
    auto leader = [&](GridSpec::Share mode, int row, int col) {
        switch (mode) {
        case GridSpec::Share::All: return 0;
        case GridSpec::Share::Column: return col;
        case GridSpec::Share::Row: return row * cols;
        case GridSpec::Share::None: break;
        }
        return -1;
    };
    const int n = std::min(rows * cols, int(cell.size()));
    for (int i = 0; i < n; ++i) {
        PlotRenderer* r = cellRenderer(i);
        const int row = i / cols, col = i % cols;
        const bool hideX = (spec.sharex == GridSpec::Share::All || spec.sharex == GridSpec::Share::Column) &&
                           row < rows - 1;
        const bool hideY = (spec.sharey == GridSpec::Share::All || spec.sharey == GridSpec::Share::Row) && col > 0;
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
        const int lx = leader(spec.sharex, row, col), ly = leader(spec.sharey, row, col);
        if (lx >= 0 && lx != i) link(cellRenderer(lx)->xAxis(), r->xAxis());
        if (ly >= 0 && ly != i) link(cellRenderer(ly)->yAxis(), r->yAxis());
    }
    // Autoscaling a linked axis fits all panels that share it (the union).
    for (int i = 0; i < n; ++i) {
        const int row = i / cols, col = i % cols;
        QList<PlotRenderer*> xs, ys;
        for (int j = 0; j < n; ++j) {
            if (j == i) continue;
            const int r2 = j / cols, c2 = j % cols;
            if (leader(spec.sharex, row, col) >= 0 && leader(spec.sharex, row, col) == leader(spec.sharex, r2, c2))
                xs << cellRenderer(j);
            if (leader(spec.sharey, row, col) >= 0 && leader(spec.sharey, row, col) == leader(spec.sharey, r2, c2))
                ys << cellRenderer(j);
        }
        if (PlotRenderer* twin = cellRenderer(i)->twin()) xs << twin;  // its twin shares x too
        cellRenderer(i)->setSharedWith(xs, ys);
    }
    // Panels sharing an axis start with the same direction as their group's
    // first panel (e.g. a reversed ppm axis).
    for (int i = 0; i < n; ++i) {
        PlotRenderer* r = cellRenderer(i);
        auto align = [](const QList<PlotRenderer*>& partners, PlotModel* m, bool x) {
            for (PlotRenderer* p : partners) {
                const AxesInfo& lead = p->model()->axesInfo();
                AxesInfo o = m->axesInfo();
                bool& mine = x ? o.xReversed : o.yReversed;
                const bool theirs = x ? lead.xReversed : lead.yReversed;
                if (mine != theirs) {
                    mine = theirs;
                    m->setAxesInfo(o);
                }
                return;  // the first partner leads
            }
        };
        if (i > 0) {
            align(r->sharedX(), r->model(), true);
            align(r->sharedY(), r->model(), false);
        }
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
    QAction* prevPin = menu->addAction("Previous time marker", this, [this] { m_timeline->stepPin(-1); });
    prevPin->setShortcut(QKeySequence("["));
    QAction* nextPin = menu->addAction("Next time marker", this, [this] { m_timeline->stepPin(+1); });
    nextPin->setShortcut(QKeySequence("]"));
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
    limitPlainKeyShortcuts();
    applyExplanations(this);  // hover explanations for every control
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
    openTimeSeries(files);
}

void FigureWindow::openTimeSeries(const QStringList& files)
{
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
    else
        addRecent("timeseries", files);
}

// ---- figure files, data export, recent files -------------------------------------

void FigureWindow::openFigureFile(const QString& path)
{
    FigureWindow* target = this;
    if (!isEmpty()) {
        target = new FigureWindow;
        target->setAttribute(Qt::WA_DeleteOnClose);
    }
    QString err;
    if (!target->loadFigure(path, &err)) {
        QMessageBox::warning(this, "Open figure", err);
        if (target != this) target->close();
        return;
    }
    target->show();
    target->raise();
    addRecent("figure", {path});
}

void FigureWindow::onOpenFigure()
{
    const QString fn = QFileDialog::getOpenFileName(this, "Open figure", m_lastDir,
                                                    "Graphplot figures (*.gpfig);;All files (*)");
    if (fn.isEmpty()) return;
    m_lastDir = QFileInfo(fn).absolutePath();
    openFigureFile(fn);
}

void FigureWindow::onSaveFigure(bool saveAs)
{
    QString fn = m_figureFile;
    if (saveAs || fn.isEmpty()) {
        const QString suggestion =
            fn.isEmpty() ? QDir(m_lastDir.isEmpty() ? QDir::homePath() : m_lastDir).filePath("figure.gpfig") : fn;
        fn = QFileDialog::getSaveFileName(this, "Save figure", suggestion, "Graphplot figures (*.gpfig)");
        if (fn.isEmpty()) return;
        if (QFileInfo(fn).suffix().isEmpty()) fn += ".gpfig";
        m_lastDir = QFileInfo(fn).absolutePath();
    }
    QString err;
    if (!saveFigure(fn, &err)) {
        QMessageBox::warning(this, "Save figure", err);
        return;
    }
    m_stack->setClean();
    addRecent("figure", {fn});
    statusBar()->showMessage("Saved " + QFileInfo(fn).fileName(), 3000);
}

void FigureWindow::onExportData(bool regionOnly)
{
    const Region r = m_model->region();
    if (regionOnly && !r.active) {
        QMessageBox::information(this, "Export data", "Select an X region first (Region tool, R).");
        return;
    }
    QString fn = QFileDialog::getSaveFileName(this, regionOnly ? "Export region data" : "Export data",
                                              QDir(m_lastDir).filePath("data.csv"), "CSV files (*.csv)");
    if (fn.isEmpty()) return;
    if (QFileInfo(fn).suffix().isEmpty()) fn += ".csv";
    m_lastDir = QFileInfo(fn).absolutePath();
    QString err;
    if (!writeDataCsv(*m_model, fn, regionOnly ? std::optional<Region>(r) : std::nullopt, &err))
        QMessageBox::warning(this, "Export data", err);
    else
        statusBar()->showMessage("Data written to " + QFileInfo(fn).fileName(), 3000);
}

void FigureWindow::addRecent(const QString& kind, const QStringList& paths)
{
    QStringList abs;
    for (const QString& p : paths) abs << QFileInfo(p).absoluteFilePath();
    const QString entry = kind + "|" + abs.join(';');
    QSettings settings("Graphplot", "Graphplot");
    QStringList list = settings.value("recentFiles").toStringList();
    list.removeAll(entry);
    list.prepend(entry);
    while (list.size() > 12) list.removeLast();
    settings.setValue("recentFiles", list);
}

void FigureWindow::fillRecentMenu(QMenu* menu)
{
    menu->clear();
    QSettings settings("Graphplot", "Graphplot");
    const QStringList list = settings.value("recentFiles").toStringList();
    static const QHash<QString, QString> kinds = {{"data", "data"},          {"matrix", "2D matrix"},
                                                  {"timeseries", "time series"}, {"bruker", "Bruker"},
                                                  {"figure", "figure"}};
    for (const QString& entry : list) {
        const QString kind = entry.section('|', 0, 0);
        const QStringList paths = entry.section('|', 1).split(';', Qt::SkipEmptyParts);
        if (!kinds.contains(kind) || paths.isEmpty()) continue;
        QStringList names;
        for (const QString& p : paths) names << QFileInfo(p).fileName();
        QAction* a = menu->addAction(QString("%1  (%2)").arg(names.join(", "), kinds.value(kind)));
        a->setToolTip(paths.join("\n"));
        a->setEnabled(QFileInfo::exists(paths.front()));
        connect(a, &QAction::triggered, this, [this, kind, paths] {
            QString err;
            bool ok = true;
            if (kind == "figure") openFigureFile(paths.front());
            else if (kind == "timeseries") openTimeSeries(paths);
            else if (kind == "bruker") ok = openBruker(paths.front(), &err) != kInvalidId;
            else if (kind == "matrix") ok = openMatrixFile(paths.front(), &err);
            else ok = openDataFile(paths.front(), &err);
            if (!ok) QMessageBox::warning(this, "Open", err);
            else if (kind != "figure" && kind != "timeseries") addRecent(kind, paths);
        });
    }
    if (menu->isEmpty()) menu->addAction("(no recent files)")->setEnabled(false);
    menu->addSeparator();
    connect(menu->addAction("Clear the list"), &QAction::triggered, this, [] {
        QSettings("Graphplot", "Graphplot").remove("recentFiles");
    });
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

int FigureWindow::createTwinPanel(int host)
{
    if (host < 0 || host >= axesCount() || m_axes[size_t(host)].twinOf >= 0) return -1;
    PlotRenderer* hostRenderer = m_axes[size_t(host)].renderer;
    if (hostRenderer->twin()) return -1;  // one twin per panel
    // The twin's axis rect covers exactly the host's plot area (the renderer
    // keeps it there after every layout pass); no margins of its own.
    auto* rect = new QCPAxisRect(m_plot, true);
    rect->setAutoMargins(QCP::msNone);
    rect->setMargins(QMargins());
    rect->setMinimumMargins(QMargins());
    auto* model = new PlotModel(this);
    model->setStyle(m_axes[size_t(host)].model->style());
    AxesInfo info = model->axesInfo();
    info.gridVisible = false;  // the host draws the grid
    model->setAxesInfo(info);
    auto* editor = new PlotEditor(model, m_stack, this);
    auto* renderer = new PlotRenderer(model, m_plot, rect, hostRenderer, this);
    Axes ax{model, editor, renderer};
    ax.twinOf = host;
    m_axes.push_back(ax);
    m_controller->addTarget(renderer, editor);
    wireAxes(m_axes.back());
    emit panelCreated(int(m_axes.size()) - 1);
    m_plot->replot();
    return int(m_axes.size()) - 1;
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
    QAction* openFig = file->addAction("Open &figure…", this, &FigureWindow::onOpenFigure);
    openFig->setShortcut(QKeySequence("Ctrl+Shift+O"));
    QMenu* recent = file->addMenu("Open &recent");
    connect(recent, &QMenu::aboutToShow, this, [this, recent] { fillRecentMenu(recent); });
    file->addSeparator();
    QAction* save = file->addAction("&Save figure", this, [this] { onSaveFigure(false); });
    save->setShortcut(QKeySequence::Save);
    QAction* saveAs = file->addAction("Save figure &as…", this, [this] { onSaveFigure(true); });
    saveAs->setShortcut(QKeySequence("Ctrl+Shift+S"));
    QAction* exportAct = file->addAction("&Export…", this,
                                         &FigureWindow::onExport);
    icons::set(exportAct, icons::Kind::Export);
    exportAct->setShortcut(QKeySequence("Ctrl+E"));
    file->addAction("Export &data (CSV)…", this, [this] { onExportData(false); });
    file->addAction("Export region data (CSV)…", this, [this] { onExportData(true); });
    QAction* copyAct = file->addAction("&Copy figure to clipboard", this, &FigureWindow::copyToClipboard);
    copyAct->setShortcut(QKeySequence("Ctrl+Shift+C"));
    file->addSeparator();
    QAction* closeAct = file->addAction("&Close", this, &QWidget::close);
    closeAct->setShortcut(QKeySequence::Close);

    // ---- Edit
    QMenu* edit = menuBar()->addMenu("&Edit");
    QAction* undo = stack->createUndoAction(this, "&Undo");
    undo->setShortcut(QKeySequence::Undo);
    icons::set(undo, icons::Kind::Undo);
    QAction* redo = stack->createRedoAction(this, "&Redo");
    redo->setShortcuts({QKeySequence::Redo, QKeySequence("Ctrl+Y")});
    icons::set(redo, icons::Kind::Redo);
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
        QAction* a = new QAction(d.text, this);
        icons::set(a, d.icon);
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
    QAction* autoscale = new QAction("Autoscale", this);
    icons::set(autoscale, icons::Kind::Autoscale);
    autoscale->setShortcut(QKeySequence("A"));
    autoscale->setToolTip("Fit all visible data (A). Double-click the plot does the same.");
    connect(autoscale, &QAction::triggered, this, [this] { m_renderer->autoscale(); });
    QAction* autoX = new QAction("Autoscale X", this);
    connect(autoX, &QAction::triggered, this, [this] { m_renderer->autoscale(true, false); });
    QAction* autoY = new QAction("Autoscale Y", this);
    autoY->setShortcut(QKeySequence("Shift+A"));
    connect(autoY, &QAction::triggered, this, [this] { m_renderer->autoscale(false, true); });
    QAction* reset = new QAction("Reset view", this);
    icons::set(reset, icons::Kind::ResetView);
    reset->setShortcut(QKeySequence("Home"));
    reset->setToolTip("Return to the original axis limits (Home).");
    connect(reset, &QAction::triggered, this, [this] { m_renderer->resetView(); });
    QAction* setHome = new QAction("Set current view as home", this);
    connect(setHome, &QAction::triggered, this, [this] { m_renderer->captureHomeView(); });
    m_levelsAction = new QAction("Levels", this);
    icons::set(m_levelsAction, icons::Kind::Levels);
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
    auto syncLabels = [this, labelsAct] {
        const QSignalBlocker b(labelsAct);
        labelsAct->setChecked(m_model->axesInfo().showPeakLabels);
    };
    connect(this, &FigureWindow::currentAxesChanged, labelsAct, syncLabels);
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
    QAction* back = view->addAction("Previous view", this, [this] { stepView(-1); });
    back->setShortcut(QKeySequence("Alt+Left"));
    QAction* fwd = view->addAction("Next view", this, [this] { stepView(+1); });
    fwd->setShortcut(QKeySequence("Alt+Right"));
    view->addAction(setHome);
    view->addSeparator();
    view->addAction(m_dock->toggleViewAction());
    view->addSeparator();
    view->addMenu(Appearance::instance()->createMenu(this));  // themes, hover explanations
    connect(Appearance::instance(), &Appearance::changed, this, [this] { icons::refresh(this); });
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
    setFigureStyle(s, QString("Style: %1").arg(QFileInfo(nameOrPath).completeBaseName()));
    return true;
}

void FigureWindow::setFigureStyle(const Style& style, const QString& undoText)
{
    m_stack->beginMacro(undoText);
    for (const Axes& ax : m_axes) ax.editor->setStyle(style, undoText);  // the style is figure-wide
    m_stack->endMacro();
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
        StyleEditor dlg(m_model->style(), [this](const Style& s) { setFigureStyle(s, "Edit style"); }, this);
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
    auto axisLabel = [](const QString& nucIn, const char* dim) {
        // "1H" -> "¹H", "13C" -> "¹³C" (mass number as a superscript, as printed in papers).
        static const QString sup = QStringLiteral("⁰¹²³⁴⁵⁶⁷⁸⁹");
        QString nuc;
        int i = 0;
        for (; i < nucIn.size() && nucIn[i].isDigit(); ++i) nuc += sup[nucIn[i].digitValue()];
        nuc += nucIn.mid(i);
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
    else addRecent("bruker", {dir});
}

void FigureWindow::onOpenMatrix()
{
    const QString fn = QFileDialog::getOpenFileName(
        this, "Open 2D matrix", m_lastDir, "Data files (*.csv *.txt *.dat *.tsv);;All files (*)");
    if (fn.isEmpty()) return;
    m_lastDir = QFileInfo(fn).absolutePath();
    QString err;
    if (!openMatrixFile(fn, &err)) QMessageBox::warning(this, "Open 2D matrix", err);
    else addRecent("matrix", {fn});
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
    else addRecent("data", {fn});
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
    setExportMode(true);
    const bool ok = exportFigure(m_axes.front().renderer, fileName, o, error);
    setExportMode(false);
    return ok;
}

void FigureWindow::onExport()
{
    QString selectedFilter;
    QString fn = QFileDialog::getSaveFileName(
        this, "Export figure", m_lastDir,
        "PNG image (*.png);;JPEG image (*.jpg);;SVG vector graphics (*.svg);;PDF document (*.pdf)", &selectedFilter);
    if (fn.isEmpty()) return;
    if (QFileInfo(fn).suffix().isEmpty()) {
        if (selectedFilter.contains("svg")) fn += ".svg";
        else if (selectedFilter.contains("pdf")) fn += ".pdf";
        else if (selectedFilter.contains("jpg")) fn += ".jpg";
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
