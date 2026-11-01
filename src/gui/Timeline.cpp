// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "Timeline.h"

#include "render/DisplayTheme.h"

#include "FigureWindow.h"
#include "commands/PlotEditor.h"
#include "model/PlotModel.h"
#include "model/TimeSeriesLoader.h"
#include "qcustomplot.h"
#include "render/PlotRenderer.h"
#include "render/RichElements.h"

#include <QDateTime>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace gp {

namespace {

constexpr int kPinTolerancePx = 5;
constexpr int kDragThresholdPx = 4;  // same as the interaction controller
constexpr double kNavHeight = 70;

// Undo command for adding / removing / editing a pin.
class PinCommand : public QUndoCommand {
public:
    PinCommand(Timeline* tl, ObjectId id, std::optional<TimePin> before, std::optional<TimePin> after,
               const QString& text)
        : QUndoCommand(text), m_tl(tl), m_id(id), m_before(std::move(before)), m_after(std::move(after))
    {
    }
    void redo() override { m_tl->applyPin(m_id, m_after); }
    void undo() override { m_tl->applyPin(m_id, m_before); }

private:
    QPointer<Timeline> m_tl;
    ObjectId m_id;
    std::optional<TimePin> m_before, m_after;
};

// Date/time tick labels for an axis whose coordinates are seconds relative
// to `origin` (Unix epoch seconds): ticks are placed at "nice" clock times.
class ClockTicker : public QCPAxisTickerDateTime {
public:
    double origin = 0;
    void generate(const QCPRange& range, const QLocale& locale, QChar formatChar, int precision,
                  QVector<double>& ticks, QVector<double>* subTicks, QVector<QString>* tickLabels) override
    {
        const double span = range.size();
        setDateTimeFormat(span > 2 * 86400 ? "dd.MM. hh:mm" : span > 120 ? "hh:mm:ss" : span > 2 ? "hh:mm:ss.zzz"
                                                                                       : "mm:ss.zzz");
        QCPAxisTickerDateTime::generate(range + origin, locale, formatChar, precision, ticks, subTicks,
                                        tickLabels);
        // Ticks on whole seconds need no ".000".
        const bool wholeSeconds = std::all_of(ticks.begin(), ticks.end(), [](double t) {
            return std::abs(t - std::round(t)) < 1e-6;
        });
        if (wholeSeconds && span <= 120 && span > 2) {
            setDateTimeFormat("hh:mm:ss");
            QCPAxisTickerDateTime::generate(range + origin, locale, formatChar, precision, ticks, subTicks,
                                            tickLabels);
        }
        for (double& t : ticks) t -= origin;
        if (subTicks)
            for (double& t : *subTicks) t -= origin;
    }
};

// Linear interpolation in a dataset sorted by x; NaN outside or across gaps.
double interpolate(const Dataset& d, double t)
{
    const auto& x = d.x;
    if (x.isEmpty() || !(t >= x.front()) || !(t <= x.back())) return std::nan("");
    auto it = std::lower_bound(x.begin(), x.end(), t);
    const qsizetype i = it - x.begin();
    if (i < x.size() && x[i] == t) return d.y[i];
    if (i == 0) return d.y[0];
    const double x0 = x[i - 1], x1 = x[i], y0 = d.y[i - 1], y1 = d.y[i];
    if (!std::isfinite(y0) || !std::isfinite(y1)) return std::nan("");
    return y0 + (y1 - y0) * (t - x0) / (x1 - x0);
}

QString fmtValue(double v)
{
    if (!std::isfinite(v)) return QStringLiteral("–");
    return QString::number(v, 'g', 5).replace('-', QChar(0x2212));
}

// Seconds with as many decimals as the resolution needs (typographic minus).
QString formatSeconds(double s, double resolution)
{
    int decimals = 3;
    if (resolution > 0) decimals = std::clamp(int(std::ceil(-std::log10(resolution))) + 1, 0, 9);
    return QString::number(s, 'f', decimals).replace('-', QChar(0x2212)) + " s";
}

}  // namespace

// ============================================================================

Timeline::Timeline(FigureWindow* window) : QObject(window), m_win(window), m_plot(window->plot())
{
    m_win->ensureGrid();
    if (!m_plot->layer("cursor")) {
        m_plot->addLayer("cursor", m_plot->layer("legend"), QCustomPlot::limAbove);
        m_plot->layer("cursor")->setMode(QCPLayer::lmBuffered);
    }
    m_cursorLayer = m_plot->layer("cursor");

    // Navigator panel: its own model/renderer, not an editable figure panel.
    auto* navRect = new QCPAxisRect(m_plot, true);
    m_navModel = new PlotModel(this);
    m_navModel->setStyle(m_win->axes(0).model->style());
    m_navRenderer = new PlotRenderer(m_navModel, m_plot, navRect, false, this);
    m_navRenderer->setRegionLabelVisible(false);
    m_navRenderer->setRegionInExports(true);  // the band shows which time span the rows show
    navRect->setMinimumSize(0, int(kNavHeight));
    navRect->setMaximumSize(QWIDGETSIZE_MAX, int(kNavHeight));
    navRect->axis(QCPAxis::atLeft)->setTickLabels(false);
    navRect->axis(QCPAxis::atLeft)->setTicks(false);
    AxesInfo navInfo = m_navModel->axesInfo();
    navInfo.gridVisible = false;
    navInfo.legendVisible = false;
    m_navModel->setAxesInfo(navInfo);
    connect(m_win->axes(0).model, &PlotModel::styleChanged, this, [this] {
        m_navModel->setStyle(m_win->axes(0).model->style());
        AxesInfo a = m_navModel->axesInfo();
        a.gridVisible = false;
        m_navModel->setAxesInfo(a);
    });

    m_navCursor = new QCPItemStraightLine(m_plot);
    bindToRect(m_navCursor, m_navRenderer);
    m_navCursor->setLayer(m_cursorLayer);
    m_navCursor->setPen(QPen(QColor(0x0b, 0x0b, 0x0b, 160), 1));
    m_navCursor->point1->setTypeY(QCPItemPosition::ptAxisRectRatio);
    m_navCursor->point2->setTypeY(QCPItemPosition::ptAxisRectRatio);
    m_navCursor->setVisible(false);
    m_navTimeLabel = new RichTextItem(m_plot);
    bindToRect(m_navTimeLabel, m_navRenderer);
    m_navTimeLabel->setLayer(m_cursorLayer);
    m_navTimeLabel->position->setTypeY(QCPItemPosition::ptAxisRectRatio);
    m_navTimeLabel->setPositionAlignment(Qt::AlignTop | Qt::AlignHCenter);
    m_navTimeLabel->setPadding(QMargins(4, 1, 4, 1));
    m_navTimeLabel->setBrush(QColor(255, 255, 255, 230));
    m_navTimeLabel->setPen(QPen(QColor(0x52, 0x51, 0x4e)));
    m_navTimeLabel->setVisible(false);
    applyThemeColors();

    // Colours of the cursor, readouts and marker labels follow the display theme.
    connect(DisplayTheme::instance(), &DisplayTheme::changed, this, [this] {
        applyThemeColors();
        rebuildPinVisuals();
        updateCursorVisuals();
    });
    // Exports: the navigator and marker labels use the figure's colours too.
    connect(m_plot, &QCustomPlot::afterLayout, this, &Timeline::positionReadouts);
    connect(m_win, &FigureWindow::exportModeChanged, this, [this](bool on) {
        if (on && m_cursor) updateCursorVisuals();  // current readings (e.g. after moving a signal)
        // A script may export right after plotting, before the event loop ran
        // the pending Y auto-fit: do it now so the file shows fitted rows.
        if (on && m_fitScheduled) autoFitNow();
        m_navRenderer->setExportMode(on);
        applyThemeColors();
        rebuildPinVisuals();
    });

    // Short rows get fewer y ticks (about one per 35 px) so labels never overlap.
    connect(m_plot, &QCustomPlot::afterLayout, this, [this] {
        for (int row : m_order) {
            PlotRenderer* r = rowRenderer(row);
            r->setMaxTicksY(std::max(2, r->axisRect()->rect().height() / 35));
        }
    });

    // Time-only zooming in rows: Y follows the data (auto-fit) or Shift+wheel.
    m_win->controller()->setZoomAxes(ZoomAxes::XOnly);
    m_plot->installEventFilter(this);  // runs before the interaction controller
    m_plot->setMouseTracking(true);

    // Panel 0 is the first row.
    attachRow(0);
    m_order << 0;
    setRowName(0, m_win->axes(0).model->axesInfo().yLabel);
    connect(m_win->axes(0).renderer->xAxis(), qOverload<const QCPRange&>(&QCPAxis::rangeChanged), this,
            &Timeline::onPrimaryRangeChanged);
    relayout();
}

Timeline::~Timeline() = default;

void Timeline::bindToRect(QCPAbstractItem* item, PlotRenderer* r)
{
    for (QCPItemPosition* pos : item->positions()) {
        pos->setAxes(r->xAxis(), r->yAxis());
        pos->setAxisRect(r->axisRect());
    }
    item->setClipAxisRect(r->axisRect());
}

PlotRenderer* Timeline::rowRenderer(int row) const
{
    return row >= 0 && row < m_win->axesCount() ? m_win->axes(row).renderer : nullptr;
}

// ---- rows ---------------------------------------------------------------------------

void Timeline::attachRow(int row)
{
    PlotRenderer* r = rowRenderer(row);
    r->setAutoHome(false);  // a row's first signal must not move the time window
    RowVisual v;
    v.cursor = new QCPItemStraightLine(m_plot);
    bindToRect(v.cursor, r);
    v.cursor->setLayer(m_cursorLayer);
    QPen pen(QColor(0x0b, 0x0b, 0x0b, 170), 1);
    pen.setCosmetic(true);
    v.cursor->setPen(pen);
    v.cursor->point1->setTypeY(QCPItemPosition::ptAxisRectRatio);
    v.cursor->point2->setTypeY(QCPItemPosition::ptAxisRectRatio);
    v.cursor->setVisible(false);
    v.readout = new RichTextItem(m_plot);
    bindToRect(v.readout, r);
    v.readout->setLayer(m_cursorLayer);
    v.readout->position->setTypeY(QCPItemPosition::ptAxisRectRatio);
    v.readout->setPadding(QMargins(5, 3, 5, 3));
    v.readout->setClipToAxisRect(false);  // a short row must not cut the values in half
    v.readout->setBrush(QColor(255, 255, 255, 225));
    v.readout->setPen(QPen(QColor(0xc8, 0xc7, 0xc2)));
    v.readout->setVisible(false);
    v.linearTicker = r->xAxis()->ticker();
    m_rows.insert(row, v);
    applyThemeColors();

    PlotModel* m = m_win->axes(row).model;
    AxesInfo info = m->axesInfo();
    info.legendVisible = true;
    if (info.legendLoc.isEmpty()) info.legendLoc = "best";  // keep clear of the signals
    m->setAxesInfo(info);
    r->setCompact(true);  // short rows: no oversized legend, no piled-up tick labels
    // Rows share the time axis: link every row to row 0 (both directions).
    if (row != 0) {
        QCPAxis* a = rowRenderer(0)->xAxis();
        QCPAxis* b = r->xAxis();
        connect(a, qOverload<const QCPRange&>(&QCPAxis::rangeChanged), b,
                qOverload<const QCPRange&>(&QCPAxis::setRange));
        connect(b, qOverload<const QCPRange&>(&QCPAxis::rangeChanged), a,
                qOverload<const QCPRange&>(&QCPAxis::setRange));
        b->setRange(a->range());
    }
    // Keep navigator, full range and readouts in sync with the data.
    // A removed (empty) row that receives data again - typically through
    // undo - comes back at the bottom instead of holding invisible data.
    connect(m, &PlotModel::datasetAdded, this, [this, row] {
        if (m_order.contains(row)) return;
        m_order << row;
        m_rows[row].visible = true;
        relayout();
        emit rowsChanged();
    });
    for (auto sig : {&PlotModel::datasetAdded, &PlotModel::datasetRemoved, &PlotModel::datasetDataChanged,
                     &PlotModel::datasetAppearanceChanged})
        connect(m, sig, this, [this] {
            updateNavigatorData();
            emit signalsChanged();
        });
    applyTimeAxis();
}

int Timeline::addRow(const QString& name)
{
    int row;
    // The first row reuses panel 0, which every figure starts with.
    if (!m_firstRowUsed && m_win->axes(0).model->datasets().empty()) {
        row = 0;
        m_firstRowUsed = true;
    } else {
        // Reuse a removed, still empty panel before allocating a new one.
        row = -1;
        for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it)
            if (!m_order.contains(it.key()) && m_win->axes(it.key()).model->datasets().empty()) {
                row = it.key();
                break;
            }
        if (row < 0) {
            row = m_win->createPanel();
            attachRow(row);
        }
        m_rows[row].visible = true;
        m_order << row;
    }
    setRowName(row, name);
    relayout();
    emit rowsChanged();
    return row;
}

ObjectId Timeline::addSignal(int row, const QString& name, QVector<double> t, QVector<double> y,
                             const QColor& color)
{
    if (!m_rows.contains(row)) return kInvalidId;
    Dataset d;
    d.name = name;
    d.x = std::move(t);
    d.y = std::move(y);
    d.style.color = color;
    const bool firstData = fullRange().first >= fullRange().second;
    const ObjectId id = m_win->axes(row).editor->addDataset(std::move(d));
    if (!m_navSignal.isValid()) setNavigatorSignal({row, id});
    if (firstData) showAll();
    scheduleAutoFit();
    return id;
}

void Timeline::setRowOrder(const QList<int>& order)
{
    QList<int> clean;
    for (int r : order)
        if (m_rows.contains(r) && !clean.contains(r)) clean << r;
    for (int r : m_order)  // keep rows that were not mentioned, at the end
        if (!clean.contains(r)) clean << r;
    if (clean == m_order) return;
    m_order = clean;
    relayout();
    emit rowsChanged();
}

bool Timeline::rowVisible(int row) const { return m_rows.value(row).visible; }

void Timeline::setRowVisible(int row, bool visible)
{
    if (!m_rows.contains(row) || m_rows[row].visible == visible) return;
    m_rows[row].visible = visible;
    relayout();
    scheduleAutoFit();  // the row may have missed window changes while hidden
    emit rowsChanged();
}

bool Timeline::removeRowIfEmpty(int row)
{
    if (!m_rows.contains(row) || !m_order.contains(row) || m_order.size() <= 1) return false;
    if (!m_win->axes(row).model->datasets().empty()) return false;
    m_order.removeAll(row);
    m_rows[row].visible = false;
    rowRenderer(row)->setPanelVisible(false);
    relayout();
    emit rowsChanged();
    return true;
}

double Timeline::rowHeight(int row) const { return m_rows.value(row).height; }

void Timeline::setRowHeight(int row, double stretch)
{
    if (!m_rows.contains(row) || !(stretch > 0)) return;
    m_rows[row].height = stretch;
    relayout();
    emit rowsChanged();
}

QString Timeline::rowName(int row) const
{
    return row >= 0 && row < m_win->axesCount() ? m_win->axes(row).model->axesInfo().yLabel : QString();
}

void Timeline::setRowName(int row, const QString& name)
{
    if (row < 0 || row >= m_win->axesCount()) return;
    PlotModel* m = m_win->axes(row).model;
    AxesInfo info = m->axesInfo();
    if (info.yLabel == name) return;
    info.yLabel = name;
    m->setAxesInfo(info);
    emit rowsChanged();
}

ObjectId Timeline::moveSignal(const SignalRef& from, int toRow)
{
    if (!from.isValid() || !m_rows.contains(toRow) || from.row == toRow) return kInvalidId;
    const Dataset* d = m_win->axes(from.row).model->dataset(from.id);
    if (!d) return kInvalidId;
    Dataset copy = *d;
    copy.id = kInvalidId;
    QUndoStack* stack = m_win->undoStack();
    // Decided before the move: removing the signal re-points the navigator.
    const bool wasNavigator = m_navSignal == from;
    stack->beginMacro(QString("Move %1 to %2").arg(copy.name, rowName(toRow)));
    const ObjectId nid = m_win->axes(toRow).editor->addDataset(copy);
    m_win->axes(from.row).editor->removeDataset(from.id);
    stack->endMacro();
    if (wasNavigator) setNavigatorSignal({toRow, nid});
    scheduleAutoFit();
    if (m_cursor) updateCursorVisuals();  // the readout moves with the signal
    return nid;
}

int Timeline::firstVisibleRow() const
{
    for (int row : m_order)
        if (m_rows.value(row).visible) return row;
    return m_order.isEmpty() ? 0 : m_order.front();
}

void Timeline::applyThemeColors()
{
    // The cursor line and boxes use the plot's text and background colours
    // as drawn (the figure style, or the dark theme on screen).
    const Style st = m_navRenderer->displayStyle();
    QColor line = st.labelColor;
    line.setAlpha(170);
    QColor box = st.axesFace;
    box.setAlpha(228);
    QPen cursorPen(line, 1);
    cursorPen.setCosmetic(true);
    if (m_navCursor) {  // not yet created while the first row is attached
        m_navCursor->setPen(cursorPen);
        m_navTimeLabel->setBrush(box);
        m_navTimeLabel->setPen(QPen(st.axesEdge));
        m_navTimeLabel->setColor(st.labelColor);
    }
    for (RowVisual& v : m_rows) {
        v.cursor->setPen(cursorPen);
        v.readout->setBrush(box);
        v.readout->setPen(QPen(st.gridColor));
        v.readout->setColor(st.labelColor);
    }
}

void Timeline::relayout()
{
    QCPLayoutGrid* grid = m_win->panelGrid();
    for (int i = grid->elementCount() - 1; i >= 0; --i)
        if (grid->elementAt(i)) grid->takeAt(i);
    grid->simplify();
    QList<double> stretch;
    int r = 0;
    int lastVisible = -1;
    for (int row : m_order) {
        PlotRenderer* rr = rowRenderer(row);
        const bool vis = m_rows[row].visible;
        rr->setPanelVisible(vis);
        if (!vis) continue;
        grid->addElement(r++, 0, rr->axisRect());
        stretch << m_rows[row].height;
        lastVisible = row;
    }
    grid->addElement(r, 0, m_navRenderer->axisRect());
    stretch << 0.001;  // fixed height (see min/max size)
    grid->setRowStretchFactors(stretch);
    grid->setRowSpacing(6);

    // Only the lowest row shows time tick labels (the navigator has its own).
    for (int row : m_order) rowRenderer(row)->xAxis()->setTickLabels(row == lastVisible);

    // Aligned frames.
    for (QCPMarginGroup* g : m_win->marginGroups()) delete g;
    m_win->marginGroups().clear();
    auto* g = new QCPMarginGroup(m_plot);
    m_win->marginGroups() << g;
    for (int row : m_order) rowRenderer(row)->axisRect()->setMarginGroup(QCP::msLeft | QCP::msRight, g);
    m_navRenderer->axisRect()->setMarginGroup(QCP::msLeft | QCP::msRight, g);

    rebuildPinVisuals();
    // setPanelVisible(true) shows every item of a panel, including the cursor
    // line and readout: put them back in their correct state.
    updateCursorVisuals();
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

int Timeline::rowAt(const QPointF& px) const
{
    for (int row : m_order) {
        if (!m_rows.value(row).visible) continue;
        if (rowRenderer(row)->axisRect()->rect().contains(px.toPoint())) return row;
    }
    return -1;
}

bool Timeline::inNavigator(const QPointF& px) const
{
    return m_navRenderer->axisRect()->outerRect().contains(px.toPoint());
}

// ---- CSV ------------------------------------------------------------------------------

bool Timeline::loadCsv(const QStringList& files, bool rowPerColumn, QString* error)
{
    std::vector<TimeSeriesTable> tables;
    for (const QString& f : files) {
        TimeSeriesTable t = loadTimeSeriesCsv(f);
        if (!t.error.isEmpty()) {
            if (error) *error = QFileInfo(f).fileName() + ": " + t.error;
            return false;
        }
        tables.push_back(std::move(t));
    }
    // Absolute (epoch) logs share one origin: the earliest start of the
    // first batch. Later files reuse it (their times may become negative).
    if (!m_absolute) {
        double origin = std::numeric_limits<double>::infinity();
        for (const auto& t : tables)
            if (t.absolute) origin = std::min(origin, t.time.front());
        if (std::isfinite(origin)) setTimeOrigin(origin);
    }
    m_win->undoStack()->beginMacro(QString("Load %1 file(s)").arg(files.size()));
    for (auto& t : tables) {
        QVector<double> time = t.time;
        if (t.absolute)
            for (double& v : time) v -= m_origin;
        if (rowPerColumn) {
            for (int c = 0; c < t.columns.size(); ++c) {
                const int row = addRow(t.columns[c]);
                // One signal per row: give each its own palette colour (each
                // row would otherwise start at the first one).
                addSignal(row, t.columns[c], time, t.values[size_t(c)],
                          m_win->model()->style().paletteColor(m_signalCount++));
            }
        } else {
            const int row = addRow(t.name);
            for (int c = 0; c < t.columns.size(); ++c) addSignal(row, t.columns[c], time, t.values[size_t(c)]);
        }
    }
    m_win->undoStack()->endMacro();
    showAll();
    return true;
}

// ---- window / navigator ----------------------------------------------------------------

QPair<double, double> Timeline::window() const
{
    const QCPRange r = rowRenderer(0)->xAxis()->range();
    return {r.lower, r.upper};
}

void Timeline::setWindow(double t0, double t1)
{
    if (t1 < t0) std::swap(t0, t1);
    if (!(t1 > t0)) t1 = t0 + 1e-6;
    rowRenderer(0)->xAxis()->setRange(t0, t1);  // other rows are linked
    for (int row : m_order) rowRenderer(row)->replot();
}

QPair<double, double> Timeline::fullRange() const
{
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (int row : m_order)
        for (const Dataset& d : m_win->axes(row).model->datasets())
            if (!d.x.isEmpty()) {
                lo = std::min(lo, d.x.front());
                hi = std::max(hi, d.x.back());
            }
    if (!(hi >= lo)) return {0, 0};
    return {lo, hi};
}

void Timeline::showAll()
{
    const auto [lo, hi] = fullRange();
    if (hi > lo) setWindow(lo, hi);
}

void Timeline::onPrimaryRangeChanged()
{
    const auto [t0, t1] = window();
    if (!m_syncing) {
        m_syncing = true;
        m_navModel->setRegion({true, t0, t1});
        m_syncing = false;
    }
    scheduleAutoFit();
    if (m_cursor) updateCursorVisuals();
    emit windowChanged(t0, t1);
}

void Timeline::setWindowFromNavigator(double a, double b)
{
    if (b < a) std::swap(a, b);
    const auto [lo, hi] = fullRange();
    const double minWidth = std::max(1e-6, (hi - lo) * 1e-6);
    if (b - a < minWidth) b = a + minWidth;
    setWindow(a, b);
}

void Timeline::setNavigatorSignal(const SignalRef& s)
{
    m_navSignal = s;
    updateNavigatorData();
    emit signalsChanged();
}

QList<SignalRef> Timeline::signalList() const
{
    QList<SignalRef> out;
    for (int row : m_order)
        for (const Dataset& d : m_win->axes(row).model->datasets()) out << SignalRef{row, d.id};
    return out;
}

QString Timeline::signalName(const SignalRef& s) const
{
    if (!s.isValid() || s.row >= m_win->axesCount()) return {};
    const Dataset* d = m_win->axes(s.row).model->dataset(s.id);
    return d ? QString("%1 / %2").arg(rowName(s.row), d->name) : QString();
}

void Timeline::updateNavigatorData()
{
    const Dataset* src = (m_navSignal.isValid() && m_navSignal.row < m_win->axesCount())
                             ? m_win->axes(m_navSignal.row).model->dataset(m_navSignal.id)
                             : nullptr;
    if (!src) {
        // Fall back to the first available signal.
        const QList<SignalRef> all = signalList();
        if (!all.isEmpty() && all.front() != m_navSignal) {
            m_navSignal = all.front();
            src = m_win->axes(m_navSignal.row).model->dataset(m_navSignal.id);
        }
    }
    if (m_navDataset != kInvalidId) m_navModel->takeDataset(m_navDataset);
    m_navDataset = kInvalidId;
    if (src) {
        Dataset d;
        d.name = src->name;
        d.x = src->x;  // implicitly shared, no copy
        d.y = src->y;
        d.style = src->style;
        d.style.inLegend = false;
        d.style.lineWidth = 1.0;
        d.style.scatter = ScatterShape::None;
        d.style.lineStyle = LineStyle::Solid;
        d.style.fillColor = QColor();  // no fill: the zero baseline may be far away
        m_navDataset = m_navModel->insertDataset(d);
    }
    // Navigator always shows the full time range; its Y fits the signal.
    const auto [lo, hi] = fullRange();
    if (hi > lo) {
        m_navRenderer->autoscale(false, true);
        m_navRenderer->xAxis()->setRange(lo, hi);
        m_navRenderer->replot();
    }
}

// ---- auto-fit Y ------------------------------------------------------------------------------

void Timeline::setAutoFitY(bool on)
{
    if (m_autoFit == on) return;
    m_autoFit = on;
    if (on) scheduleAutoFit();
    emit optionsChanged();
}

void Timeline::scheduleAutoFit()
{
    if (!m_autoFit || m_fitScheduled) return;
    m_fitScheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_fitScheduled = false;
        autoFitNow();
    });
}

void Timeline::autoFitNow()
{
    const auto [t0, t1] = window();
    for (int row : m_order) {
        if (!m_rows[row].visible) continue;
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        for (const Dataset& d : m_win->axes(row).model->datasets()) {
            if (!d.style.visible || d.x.isEmpty()) continue;
            auto b = std::lower_bound(d.x.begin(), d.x.end(), t0);
            auto e = std::upper_bound(d.x.begin(), d.x.end(), t1);
            // Include the neighbours so lines entering the window are covered.
            qsizetype i0 = std::max<qsizetype>(0, (b - d.x.begin()) - 1);
            qsizetype i1 = std::min<qsizetype>(d.x.size(), (e - d.x.begin()) + 1);
            for (qsizetype i = i0; i < i1; ++i) {
                const double v = d.y[i];
                if (!std::isfinite(v)) continue;
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        }
        if (!(hi >= lo)) continue;
        double pad = (hi - lo) * 0.08;
        if (pad == 0) pad = std::max(std::abs(hi) * 0.1, 1e-9);
        rowRenderer(row)->yAxis()->setRange(lo - pad, hi + pad);
        rowRenderer(row)->replot();
    }
}

// ---- time axis ---------------------------------------------------------------------------------

void Timeline::setTimeOrigin(double epochSeconds)
{
    m_origin = epochSeconds;
    m_absolute = true;
    applyTimeAxis();
    emit optionsChanged();
}

void Timeline::setClockTime(bool on)
{
    if (m_clock == (on && m_absolute)) return;
    m_clock = on && m_absolute;
    applyTimeAxis();
    emit optionsChanged();
}

void Timeline::applyTimeAxis()
{
    QList<QCPAxis*> axes;
    for (auto it = m_rows.begin(); it != m_rows.end(); ++it) {
        PlotRenderer* r = rowRenderer(it.key());
        QCPAxis* x = r->xAxis();
        if (m_clock) {
            auto t = QSharedPointer<ClockTicker>::create();
            t->origin = m_origin;
            t->setTickCount(6);
            x->setTicker(t);
        } else {
            x->setTicker(it->linearTicker);
        }
        r->axisRect()->axis(QCPAxis::atTop)->setTicker(x->ticker());
        // The axis label of the lowest row carries the unit.
    }
    QCPAxis* nx = m_navRenderer->xAxis();
    if (m_clock) {
        auto t = QSharedPointer<ClockTicker>::create();
        t->origin = m_origin;
        nx->setTicker(t);
    } else {
        nx->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));
    }
    AxesInfo a = m_navModel->axesInfo();
    a.xLabel = m_clock ? QString("Time (%1)").arg(QDateTime::fromMSecsSinceEpoch(qint64(m_origin * 1000))
                                                     .toString("yyyy-MM-dd"))
                       : QString("Time (s)");
    m_navModel->setAxesInfo(a);
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

QString Timeline::formatTime(double t, double resolution) const
{
    if (m_clock) {
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(qint64(std::llround((m_origin + t) * 1000)));
        return dt.toString("hh:mm:ss.zzz");
    }
    return formatSeconds(t, resolution);
}

// ---- cursor ---------------------------------------------------------------------------------------

QList<Timeline::Reading> Timeline::valuesAt(double t) const
{
    QList<Reading> out;
    for (int row : m_order) {
        if (!m_rows.value(row).visible) continue;
        for (const Dataset& d : m_win->axes(row).model->datasets()) {
            if (!d.style.visible) continue;
            out << Reading{row, d.name, interpolate(d, t), d.style.color};
        }
    }
    return out;
}

void Timeline::setCursorTime(std::optional<double> t)
{
    if (t == m_cursor) return;
    m_cursor = t;
    updateCursorVisuals();
    emit cursorChanged(t);
}

void Timeline::positionReadouts()
{
    // Pixel geometry: done again after every layout pass (exports of a window
    // never shown, resizes), not only when the cursor moves.
    if (!m_cursor) return;
    const double t = *m_cursor;
    for (int row : m_order) {
        RowVisual& v = m_rows[row];
        if (!v.readout->visible()) continue;
        // Right of the line, or left when close to the right edge.
        PlotRenderer* rr = rowRenderer(row);
        const double px = rr->xAxis()->coordToPixel(t);
        const QRect ar = rr->axisRect()->rect();
        const bool left = px > ar.left() + ar.width() * 0.72;
        // A small gap (6 px) between the line and the box.
        // Start below the time-marker labels at the top of the row (~22 px).
        const double yRatio = ar.height() > 0 ? std::min(0.5, 22.0 / ar.height()) : 0.1;
        v.readout->position->setCoords(rr->xAxis()->pixelToCoord(px + (left ? -6 : 6)), yRatio);
        v.readout->setPositionAlignment((left ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignTop);
    }
}

void Timeline::updateCursorVisuals()
{
    const bool on = m_cursor.has_value();
    const auto [w0, w1] = window();
    const double t = on ? *m_cursor : 0;
    const bool inWindow = on && t >= w0 && t <= w1;
    QFont f = m_plot->font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1));
    const QList<Reading> readings = inWindow ? valuesAt(t) : QList<Reading>();
    for (int row : m_order) {
        RowVisual& v = m_rows[row];
        const bool show = inWindow && v.visible;
        v.cursor->setVisible(show);
        v.readout->setVisible(show);
        if (!show) continue;
        v.cursor->point1->setCoords(t, 0);
        v.cursor->point2->setCoords(t, 1);
        QStringList lines;
        for (const Reading& r : readings) {
            if (r.row != row) continue;
            lines << QString("<span style='color:%1'>&#9632;</span>&nbsp;%2:&nbsp;<b>%3</b>")
                         .arg(r.color.name(), r.name.toHtmlEscaped(), fmtValue(r.value));
        }
        v.readout->setVisible(!lines.isEmpty());
        v.readout->setHtml(lines.join("<br>"), f);
    }
    positionReadouts();
    m_navCursor->setVisible(on);
    m_navTimeLabel->setVisible(on);
    if (on) {
        m_navCursor->point1->setCoords(t, 0);
        m_navCursor->point2->setCoords(t, 1);
        const double res = (w1 - w0) / 1000.0;
        QString text = formatTime(t, res);
        // Time from the nearest marker, for measuring durations between events.
        const TimePin* near = nullptr;
        for (const TimePin& p : m_pins)
            if (!near || std::abs(p.t - t) < std::abs(near->t - t)) near = &p;
        if (near)
            // Non-breaking spaces: the label is rich text, where plain runs collapse.
            text += QString("\u00a0\u00a0\u00a0%1t from %2: %3%4")
                        .arg(QChar(0x0394), near->label.isEmpty() ? QString("marker") : near->label,
                             t >= near->t ? "+" : "",
                             formatSeconds(t - near->t, res));
        m_navTimeLabel->setText(text, f, false);
        m_navTimeLabel->position->setCoords(t, 0.0);
    }
    m_cursorLayer->replot();
}

// ---- pins ---------------------------------------------------------------------------------------------

ObjectId Timeline::addPin(double t, const QString& label, const QColor& color)
{
    TimePin p;
    p.id = ++m_lastPinId;
    p.t = t;
    p.label = label.isEmpty() ? QString("P%1").arg(m_pins.size() + 1) : label;
    if (color.isValid()) p.color = color;
    m_win->undoStack()->push(new PinCommand(this, p.id, std::nullopt, p, "Add time marker"));
    return p.id;
}

void Timeline::updatePin(const TimePin& after, std::optional<TimePin> before)
{
    auto it = std::find_if(m_pins.begin(), m_pins.end(), [&](const TimePin& p) { return p.id == after.id; });
    if (it == m_pins.end()) return;
    const TimePin b = before.value_or(*it);
    if (b == after) return;
    m_win->undoStack()->push(new PinCommand(this, after.id, b, after, "Edit time marker"));
}

void Timeline::removePin(ObjectId id)
{
    auto it = std::find_if(m_pins.begin(), m_pins.end(), [&](const TimePin& p) { return p.id == id; });
    if (it == m_pins.end()) return;
    m_win->undoStack()->push(new PinCommand(this, id, *it, std::nullopt, "Delete time marker"));
}

void Timeline::applyPin(ObjectId id, const std::optional<TimePin>& state)
{
    auto it = std::find_if(m_pins.begin(), m_pins.end(), [&](const TimePin& p) { return p.id == id; });
    if (!state) {
        if (it != m_pins.end()) m_pins.erase(it);
    } else if (it != m_pins.end()) {
        *it = *state;
    } else {
        m_pins.push_back(*state);
        m_lastPinId = std::max(m_lastPinId, id);
    }
    std::sort(m_pins.begin(), m_pins.end(), [](const TimePin& a, const TimePin& b) { return a.t < b.t; });
    rebuildPinVisuals();
    updateCursorVisuals();  // the delta-t readout depends on the markers
    emit pinsChanged();
}

void Timeline::jumpToPin(ObjectId id)
{
    for (const TimePin& p : m_pins)
        if (p.id == id) {
            const auto [a, b] = window();
            const double half = (b - a) / 2;
            setWindow(p.t - half, p.t + half);
            return;
        }
}

ObjectId Timeline::stepPin(int direction)
{
    if (m_pins.empty()) return kInvalidId;
    const auto [a, b] = window();
    const double centre = (a + b) / 2;
    const double eps = (b - a) * 1e-6;  // "already centred on this pin"
    const TimePin* target = nullptr;
    // m_pins is kept sorted by time.
    if (direction > 0) {
        for (const TimePin& p : m_pins)
            if (p.t > centre + eps) { target = &p; break; }
    } else {
        for (auto it = m_pins.rbegin(); it != m_pins.rend(); ++it)
            if (it->t < centre - eps) { target = &*it; break; }
    }
    if (!target) return kInvalidId;
    const ObjectId id = target->id;
    jumpToPin(id);
    return id;
}

void Timeline::rebuildPinVisuals()
{
    for (QCPAbstractItem* it : m_pinItems) m_plot->removeItem(it);
    m_pinItems.clear();
    int topRow = -1;
    for (int row : m_order)
        if (m_rows[row].visible) {
            topRow = row;
            break;
        }
    QFont f = m_plot->font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1));
    f.setBold(true);
    for (const TimePin& p : m_pins) {
        QPen pen(p.color, 1.5, Qt::DashLine);
        pen.setCosmetic(true);
        auto addLine = [&](PlotRenderer* r) {
            auto* l = new QCPItemStraightLine(m_plot);
            bindToRect(l, r);
            l->setLayer("objects");
            l->setPen(pen);
            l->setSelectable(false);
            l->point1->setTypeY(QCPItemPosition::ptAxisRectRatio);
            l->point2->setTypeY(QCPItemPosition::ptAxisRectRatio);
            l->point1->setCoords(p.t, 0);
            l->point2->setCoords(p.t, 1);
            m_pinItems.push_back(l);
        };
        for (int row : m_order)
            if (m_rows[row].visible) addLine(rowRenderer(row));
        addLine(m_navRenderer);
        if (topRow >= 0 && !p.label.isEmpty()) {
            auto* t = new RichTextItem(m_plot);
            bindToRect(t, rowRenderer(topRow));
            t->setLayer("objects");
            t->position->setTypeY(QCPItemPosition::ptAxisRectRatio);
            t->position->setCoords(p.t, 0.0);
            t->setPositionAlignment(Qt::AlignTop | Qt::AlignHCenter);
            t->setPadding(QMargins(4, 1, 4, 1));
            const Style st = m_navRenderer->displayStyle();
            QColor box = st.axesFace;
            box.setAlpha(235);
            t->setBrush(box);
            t->setPen(QPen(p.color));
            // Darker text on light backgrounds, lighter on dark ones.
            t->setColor(st.axesFace.lightnessF() < 0.5 ? p.color.lighter(150) : p.color.darker(140));
            t->setText(p.label, f, true);
            m_pinItems.push_back(t);
        }
    }
    m_plot->layer("objects")->replot();
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

ObjectId Timeline::pinAt(const QPointF& px, int row) const
{
    PlotRenderer* r = row >= 0 ? rowRenderer(row) : m_navRenderer;
    ObjectId best = kInvalidId;
    double bestD = kPinTolerancePx + 1;
    for (const TimePin& p : m_pins) {
        const double d = std::abs(r->xAxis()->coordToPixel(p.t) - px.x());
        if (d <= kPinTolerancePx && d < bestD) {
            bestD = d;
            best = p.id;
        }
    }
    return best;
}

// ---- mouse ------------------------------------------------------------------------------------------------

bool Timeline::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_plot) return false;
    switch (event->type()) {
    case QEvent::MouseMove: {
        auto* e = static_cast<QMouseEvent*>(event);
        const QPointF px = e->position();
        // Drags owned by the timeline.
        if (m_drag != Drag::None) {
            // Pins move in row time (all rows share it), the window in navigator time.
            // (any visible row: hidden rows keep stale geometry)
            QCPAxis* axis = m_drag == Drag::Pin ? rowRenderer(firstVisibleRow())->xAxis()
                                                : m_navRenderer->xAxis();
            const double t = axis->pixelToCoord(px.x());
            // Ignore jitter: a click with a 1-2 px wobble is still a click.
            if (!m_dragMoved && (px.toPoint() - m_pressPx).manhattanLength() < kDragThresholdPx) return true;
            m_dragMoved = true;
            const auto [a, b] = m_pressWindow;
            switch (m_drag) {
            case Drag::NavMove: setWindowFromNavigator(a + (t - m_pressT), b + (t - m_pressT)); break;
            case Drag::NavMin: setWindowFromNavigator(t, b); break;
            case Drag::NavMax: setWindowFromNavigator(a, t); break;
            case Drag::NavCreate: setWindowFromNavigator(m_pressT, t); break;
            case Drag::Pin: {
                TimePin p = m_dragPin;
                p.t = t;
                applyPin(p.id, p);  // preview; committed on release
                break;
            }
            case Drag::None: break;
            }
            setCursorTime(t);
            return true;
        }
        const int row = rowAt(px);
        // While the interaction controller drags (rubber band, marker, pan...)
        // only follow the cursor and let it have every move event.
        const bool controllerDrag = e->buttons() != Qt::NoButton;
        if (row >= 0) setCursorTime(rowRenderer(row)->xAxis()->pixelToCoord(px.x()));
        else if (m_navRenderer->axisRect()->rect().contains(px.toPoint()))
            setCursorTime(m_navRenderer->xAxis()->pixelToCoord(px.x()));
        else setCursorTime(std::nullopt);
        if (controllerDrag) return false;
        if (inNavigator(px)) {
            const RegionHit h = m_navRenderer->hitTestRegion(px);
            m_plot->setCursor(h == RegionHit::MinEdge || h == RegionHit::MaxEdge ? Qt::SplitHCursor
                              : h == RegionHit::Inside                          ? Qt::OpenHandCursor
                                                                                : Qt::CrossCursor);
            return true;
        }
        if (row >= 0 && pinAt(px, row) != kInvalidId) {
            m_plot->setCursor(Qt::SizeHorCursor);
            return true;  // don't let the controller override the cursor
        }
        return false;  // the controller handles hover/drag in rows
    }
    case QEvent::Leave:
        setCursorTime(std::nullopt);
        return false;
    case QEvent::KeyPress:
        if (m_drag != Drag::None && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            if (m_drag == Drag::Pin) applyPin(m_dragPin.id, m_dragPin);
            else setWindowFromNavigator(m_pressWindow.first, m_pressWindow.second);
            m_drag = Drag::None;
            return true;
        }
        return false;
    case QEvent::MouseButtonPress: {
        auto* e = static_cast<QMouseEvent*>(event);
        const QPointF px = e->position();
        if (inNavigator(px)) {
            if (e->button() != Qt::LeftButton) return true;
            m_pressPx = px.toPoint();
            m_pressT = m_navRenderer->xAxis()->pixelToCoord(px.x());
            m_pressWindow = window();
            m_dragMoved = false;
            const RegionHit h = m_navRenderer->hitTestRegion(px);
            m_drag = h == RegionHit::MinEdge ? Drag::NavMin
                   : h == RegionHit::MaxEdge ? Drag::NavMax
                   : h == RegionHit::Inside  ? Drag::NavMove
                                             : Drag::NavCreate;
            if (m_drag == Drag::NavMove) m_plot->setCursor(Qt::ClosedHandCursor);
            return true;
        }
        const int row = rowAt(px);
        if (row < 0) return false;
        const double t = rowRenderer(row)->xAxis()->pixelToCoord(px.x());
        const ObjectId pin = pinAt(px, row);
        if (e->button() == Qt::RightButton && pin != kInvalidId) {
            QMenu menu;
            menu.addAction("Rename…", this, [this, pin] {
                for (const TimePin& p : m_pins)
                    if (p.id == pin) {
                        bool ok = false;
                        const QString s = QInputDialog::getText(m_win, "Time marker", "Label:",
                                                                QLineEdit::Normal, p.label, &ok);
                        if (ok) {
                            TimePin q = p;
                            q.label = s;
                            updatePin(q);
                        }
                        return;
                    }
            });
            menu.addAction("Delete", this, [this, pin] { removePin(pin); });
            menu.exec(e->globalPosition().toPoint());
            return true;
        }
        if (e->button() != Qt::LeftButton) return false;
        if (e->modifiers() & Qt::ControlModifier) {  // Ctrl+click: drop a time marker
            addPin(t);
            return true;
        }
        if (pin != kInvalidId && m_win->controller()->mode() != InteractionMode::AddVMarker) {
            for (const TimePin& p : m_pins)
                if (p.id == pin) m_dragPin = p;
            m_pressPx = px.toPoint();
            m_drag = Drag::Pin;
            m_dragMoved = false;
            return true;
        }
        return false;
    }
    case QEvent::MouseButtonRelease: {
        if (m_drag == Drag::None) return false;
        auto* e = static_cast<QMouseEvent*>(event);
        const Drag d = m_drag;
        m_drag = Drag::None;
        if (d == Drag::Pin) {
            if (m_dragMoved)
                for (const TimePin& p : m_pins)
                    if (p.id == m_dragPin.id) updatePin(p, m_dragPin);
        } else if (d == Drag::NavCreate && !m_dragMoved) {
            // Simple click in the navigator: centre the window there.
            const auto [a, b] = m_pressWindow;
            const double half = (b - a) / 2;
            setWindowFromNavigator(m_pressT - half, m_pressT + half);
        }
        m_plot->setCursor(inNavigator(e->position()) ? Qt::OpenHandCursor : Qt::CrossCursor);
        return true;
    }
    case QEvent::MouseButtonDblClick: {
        auto* e = static_cast<QMouseEvent*>(event);
        if (inNavigator(e->position())) {
            showAll();
            return true;
        }
        const int row = rowAt(e->position());
        const ObjectId pin = row >= 0 ? pinAt(e->position(), row) : kInvalidId;
        if (pin != kInvalidId) {
            for (const TimePin& p : m_pins)
                if (p.id == pin) {
                    bool ok = false;
                    const QString s = QInputDialog::getText(m_win, "Time marker", "Label:",
                                                            QLineEdit::Normal, p.label, &ok);
                    if (ok) {
                        TimePin q = p;
                        q.label = s;
                        updatePin(q);
                    }
                    break;
                }
            return true;
        }
        if (row >= 0) {
            // Double-click on a marker / annotation edits it (controller);
            // on empty space: fit the whole recording (time) instead of X+Y.
            if (rowRenderer(row)->hitTestObject(e->position()).isValid()) return false;
            showAll();
            return true;
        }
        return false;
    }
    case QEvent::Wheel: {
        auto* e = static_cast<QWheelEvent*>(event);
        if (!inNavigator(e->position())) return false;
        const double steps = e->angleDelta().y() / 120.0;
        if (steps == 0) return true;
        const double f = std::pow(0.85, steps);
        const double c = m_navRenderer->xAxis()->pixelToCoord(e->position().x());
        const auto [a, b] = window();
        setWindowFromNavigator(c + (a - c) * f, c + (b - c) * f);
        return true;
    }
    default: return false;
    }
}

}  // namespace gp
