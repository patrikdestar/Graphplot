// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "PlotRenderer.h"

#include "DisplayTheme.h"

#include "ColorBar.h"
#include "ContourPlottable.h"
#include "PeakOverlay.h"
#include "RichElements.h"
#include "qcustomplot.h"

#include <algorithm>
#include <limits>
#include <cmath>

namespace gp {

namespace {

// Tick-label number format of a linear axis from the style's "formatter":
// sci = 1.5x10^3 ("gbc"), plain = 1500 ("g"), auto = 1.5.10^3 only when needed.
// Log axes keep their own "eb" (powers of ten) format.
void applyNumberFormat(QCPAxis* axis, const Style& st)
{
    if (axis->scaleType() != QCPAxis::stLinear) return;
    axis->setNumberFormat(st.formatter == "sci" ? "gbc" : st.formatter == "plain" ? "g" : "gbd");
    axis->setNumberPrecision(st.formatter == "plain" ? 10 : 6);
}

const QColor kRegionFill(0x2a, 0x78, 0xd6, 38);
const QColor kRegionEdge(0x2a, 0x78, 0xd6, 160);
const QColor kHighlight(0x2a, 0x78, 0xd6);

Qt::PenStyle toPenStyle(LineStyle s)
{
    switch (s) {
    case LineStyle::Solid: return Qt::SolidLine;
    case LineStyle::Dash: return Qt::DashLine;
    case LineStyle::Dot: return Qt::DotLine;
    case LineStyle::DashDot: return Qt::DashDotLine;
    case LineStyle::None: return Qt::NoPen;
    }
    return Qt::SolidLine;
}

QCPScatterStyle::ScatterShape toScatterShape(ScatterShape s)
{
    switch (s) {
    case ScatterShape::None: return QCPScatterStyle::ssNone;
    case ScatterShape::Circle: return QCPScatterStyle::ssCircle;
    case ScatterShape::Square: return QCPScatterStyle::ssSquare;
    case ScatterShape::Triangle: return QCPScatterStyle::ssTriangle;
    case ScatterShape::Cross: return QCPScatterStyle::ssCross;
    case ScatterShape::Plus: return QCPScatterStyle::ssPlus;
    case ScatterShape::Diamond: return QCPScatterStyle::ssDiamond;
    }
    return QCPScatterStyle::ssNone;
}

// Tick labels: optional real minus sign ("−") as in typeset documents.
class StyledTicker : public QCPAxisTicker {
public:
    bool unicodeMinus = true;

protected:
    QString getTickLabel(double tick, const QLocale& locale, QChar formatChar, int precision) override
    {
        QString s = QCPAxisTicker::getTickLabel(tick, locale, formatChar, precision);
        // Leave exponent notation to QCustomPlot's "beautiful powers" parser.
        if (unicodeMinus && !s.contains('e')) s.replace('-', QChar(0x2212));
        return s;
    }
};

Qt::PenStyle gridPenStyle(const QString& s)
{
    if (s == "--") return Qt::DashLine;
    if (s == ":") return Qt::DotLine;
    if (s == "-.") return Qt::DashDotLine;
    return Qt::SolidLine;
}

Qt::Alignment locToAlignment(const QString& loc)
{
    Qt::Alignment a;
    a |= loc.contains("upper") ? Qt::AlignTop : loc.contains("lower") ? Qt::AlignBottom : Qt::AlignVCenter;
    a |= loc.contains("left") ? Qt::AlignLeft : loc.contains("right") ? Qt::AlignRight : Qt::AlignHCenter;
    return a;
}

// QCPGraph needs increasing, finite x; anything else is drawn as a QCPCurve.
bool isSorted(const QVector<double>& v)
{
    for (qsizetype i = 0; i < v.size(); ++i) {
        if (!std::isfinite(v[i])) return false;
        if (i > 0 && v[i] < v[i - 1]) return false;
    }
    return true;
}

QString fmt(double v) { return QString::number(v, 'g', 6); }

// Pads a range so data does not touch the frame; handles degenerate ranges.
void padRange(double& lo, double& hi, double fraction)
{
    if (!(hi > lo)) {
        const double c = lo;
        const double d = (c == 0.0) ? 1.0 : std::abs(c) * 0.1;
        lo = c - d;
        hi = c + d;
        return;
    }
    const double pad = (hi - lo) * fraction;
    lo -= pad;
    hi += pad;
}

}  // namespace

PlotRenderer::PlotRenderer(PlotModel* model, QCustomPlot* plot, QObject* parent)
    : PlotRenderer(model, plot, plot->axisRect(), true, parent)
{
}

PlotRenderer::PlotRenderer(PlotModel* model, QCustomPlot* plot, QCPAxisRect* rect, bool primary,
                           QObject* parent)
    : QObject(parent), m_model(model), m_plot(plot), m_rect(rect), m_primary(primary)
{
    m_rect->setupFullAxesBox(true);  // closed frame: creates top/right axes if needed
    // QCustomPlot puts only its default axis rect on the "background" layer;
    // others would paint their background over the grid / image / region layers.
    m_rect->setLayer(QLatin1String("background"));
    m_x = m_rect->axis(QCPAxis::atBottom);
    m_y = m_rect->axis(QCPAxis::atLeft);
    m_x2 = m_rect->axis(QCPAxis::atTop);
    m_y2 = m_rect->axis(QCPAxis::atRight);
    m_legend = (rect == plot->axisRect() && plot->legend) ? plot->legend : new QCPLegend;
    setupAppearance();

    connect(m_model, &PlotModel::datasetAdded, this, &PlotRenderer::onDatasetAdded);
    connect(m_model, &PlotModel::datasetRemoved, this, &PlotRenderer::onDatasetRemoved);
    connect(m_model, &PlotModel::datasetAppearanceChanged, this, [this](ObjectId id) {
        applyDatasetAppearance(id);
        syncLegend();
        replot();
    });
    connect(m_model, &PlotModel::datasetDataChanged, this, [this](ObjectId id) {
        applyDatasetData(id);
        replot();
    });
    connect(m_model, &PlotModel::matrixAdded, this, &PlotRenderer::onMatrixAdded);
    connect(m_model, &PlotModel::matrixRemoved, this, &PlotRenderer::onMatrixRemoved);
    connect(m_model, &PlotModel::matrixAppearanceChanged, this, [this](ObjectId id) {
        applyMatrixAppearance(id);
        syncLegend();
        replot();
    });
    connect(m_model, &PlotModel::matrixDataChanged, this, [this](ObjectId id) {
        applyMatrixData(id);
        replot();
    });
    for (auto sig : {&PlotModel::peakAdded, &PlotModel::peakRemoved, &PlotModel::peakChanged})
        connect(m_model, sig, this, [this](ObjectId id) {
            if (m_highlight.kind == ObjectKind::Peak && m_highlight.id == id && !m_model->peak(id))
                m_highlight = {};
            replotObjectLayers();
        });
    connect(m_model, &PlotModel::markerAdded, this, &PlotRenderer::onMarkerAdded);
    connect(m_model, &PlotModel::markerRemoved, this, &PlotRenderer::onMarkerRemoved);
    connect(m_model, &PlotModel::markerChanged, this, [this](ObjectId id) {
        applyMarker(id);
        replotObjectLayers();
    });
    connect(m_model, &PlotModel::annotationAdded, this, &PlotRenderer::onAnnotationAdded);
    connect(m_model, &PlotModel::annotationRemoved, this, &PlotRenderer::onAnnotationRemoved);
    connect(m_model, &PlotModel::annotationChanged, this, [this](ObjectId id) {
        applyAnnotation(id);
        replotObjectLayers();
    });
    connect(m_model, &PlotModel::regionChanged, this, [this] {
        applyRegion();
        replotObjectLayers();
    });
    connect(DisplayTheme::instance(), &DisplayTheme::changed, this, &PlotRenderer::refreshAppearance);
    connect(m_model, &PlotModel::styleChanged, this, [this] {
        applyStyle();
        replot();
    });
    connect(m_model, &PlotModel::axesInfoChanged, this, [this] {
        applyAxesInfo();
        replot();
    });

    connect(m_x, qOverload<const QCPRange&>(&QCPAxis::rangeChanged), this,
            [this] { emit viewChanged(); });
    connect(m_y, qOverload<const QCPRange&>(&QCPAxis::rangeChanged), this,
            [this] { emit viewChanged(); });

    rebuildAll();
}

PlotRenderer::~PlotRenderer() = default;

void PlotRenderer::bindItem(QCPAbstractItem* item)
{
    // Items default to the plot's first axes; tie them to this panel.
    for (QCPItemPosition* pos : item->positions()) {
        pos->setAxes(m_x, m_y);
        pos->setAxisRect(m_rect);
    }
    item->setClipAxisRect(m_rect);
}

void PlotRenderer::setPanelVisible(bool visible)
{
    if (visible == m_panelVisible) return;
    m_panelVisible = visible;
    // Axes, plottables and the inset legend follow the axis rect's visibility.
    m_rect->setVisible(visible);
    m_axisLabels->setVisible(visible);
    m_peaks->setVisible(visible);
    if (m_colorBar) m_colorBar->setVisible(false);  // re-shown by updateColorBar()
    for (int i = 0; i < m_plot->itemCount(); ++i) {
        QCPAbstractItem* it = m_plot->item(i);
        if (it->clipAxisRect() == m_rect) it->setVisible(visible);
    }
    if (visible) {
        // Re-apply per-object visibility rules (empty labels, inactive region, ...).
        applyStyle();
        applyRegion();
        updateColorBar();
    }
}

void PlotRenderer::setupAppearance()
{
    QCustomPlot* p = m_plot;
    p->setAntialiasedElements(QCP::aePlottables | QCP::aeItems | QCP::aeLegendItems |
                              QCP::aeAxes | QCP::aeOther);
    p->setNotAntialiasedElements(QCP::aeGrid);  // crisp 1-px grid lines
    p->setPlottingHints(QCP::phCacheLabels | QCP::phImmediateRefresh);
    p->setInteractions({});  // all interaction is handled by InteractionController
    p->setAutoAddPlottableToLegend(false);  // legends are built by syncLegend() per panel
    for (QCPAxis* axis : m_rect->axes()) {
        auto ticker = QSharedPointer<StyledTicker>::create();
        axis->setTicker(ticker);
        axis->grid()->setZeroLinePen(Qt::NoPen);
    }
    m_x2->grid()->setVisible(false);
    m_y2->grid()->setVisible(false);
    if (m_legend->parent() == nullptr || !m_rect->insetLayout()->elements(false).contains(m_legend)) {
        // QCustomPlot's default legend lives in the default axis rect; other
        // panels get their own legend in their inset layout.
        m_rect->insetLayout()->addElement(m_legend, Qt::AlignTop | Qt::AlignRight);
        m_legend->setLayer("legend");
    }
    m_legend->setVisible(true);
    m_legend->setIconSize(24, 10);
    m_legend->setSelectableParts(QCPLegend::spNone);

    // Rich-text axis labels (drawn on the "axes" layer) and header row.
    m_axisLabels = new RichAxisLabels(p, m_rect);
    m_axisLabels->setLayer("axes");
    m_header = new HeaderElement(p, m_rect);
    m_insideLabel = new RichTextItem(p);
    bindItem(m_insideLabel);
    m_insideLabel->setLayer("legend");
    m_insideLabel->setSelectable(false);
    m_insideLabel->position->setType(QCPItemPosition::ptAxisRectRatio);

    // Dedicated layers: the region is drawn below the curves, markers and
    // annotations above them. Both are buffered so that dragging an object
    // only repaints that layer, independent of how much data is plotted.
    // Colour-map images sit directly above the grid, below everything else.
    // Layers are shared by all axes of a figure: create them only once.
    if (!p->layer("images")) {
        p->addLayer("images", p->layer("grid"), QCustomPlot::limAbove);
        p->addLayer("regions", p->layer("images"), QCustomPlot::limAbove);
        p->addLayer("objects", p->layer("main"), QCustomPlot::limAbove);
    }
    m_imageLayer = p->layer("images");
    m_regionLayer = p->layer("regions");
    m_objectLayer = p->layer("objects");
    m_regionLayer->setMode(QCPLayer::lmBuffered);
    m_objectLayer->setMode(QCPLayer::lmBuffered);

    // Region visual (hidden until a region is selected).
    auto makeRegionLine = [&]() {
        auto* l = new QCPItemStraightLine(p);
        bindItem(l);
        l->setLayer(m_regionLayer);
        QPen pen(kRegionEdge, 1);
        pen.setCosmetic(true);
        l->setPen(pen);
        l->setSelectable(false);
        l->point1->setTypeY(QCPItemPosition::ptAxisRectRatio);
        l->point2->setTypeY(QCPItemPosition::ptAxisRectRatio);
        l->setVisible(false);
        return l;
    };
    m_region.rect = new QCPItemRect(p);
    bindItem(m_region.rect);
    m_region.rect->setLayer(m_regionLayer);
    m_region.rect->setPen(Qt::NoPen);
    m_region.rect->setBrush(kRegionFill);
    m_region.rect->setSelectable(false);
    m_region.rect->topLeft->setTypeY(QCPItemPosition::ptAxisRectRatio);
    m_region.rect->bottomRight->setTypeY(QCPItemPosition::ptAxisRectRatio);
    m_region.rect->setVisible(false);
    m_peaks = new PeakOverlay(p, m_x, m_y, m_model);
    m_peaks->setLayer(m_objectLayer);

    // Diagonal y = x (plot coordinates on both axes), below the curves.
    m_diagonal = new QCPItemStraightLine(p);
    bindItem(m_diagonal);
    m_diagonal->setLayer(m_regionLayer);
    m_diagonal->setSelectable(false);
    QPen diagPen(QColor(0x52, 0x51, 0x4e), 1, Qt::DashLine);
    diagPen.setCosmetic(true);
    m_diagonal->setPen(diagPen);
    m_diagonal->point1->setCoords(0, 0);
    m_diagonal->point2->setCoords(1, 1);
    m_diagonal->setVisible(false);

    m_region.minEdge = makeRegionLine();
    m_region.maxEdge = makeRegionLine();
    m_region.label = new QCPItemText(p);
    bindItem(m_region.label);
    m_region.label->setLayer(m_objectLayer);
    m_region.label->position->setTypeY(QCPItemPosition::ptAxisRectRatio);
    m_region.label->setPositionAlignment(Qt::AlignBottom | Qt::AlignHCenter);
    m_region.label->setColor(QColor(0x0b, 0x0b, 0x0b));
    m_region.label->setPadding(QMargins(4, 2, 4, 2));
    m_region.label->setBrush(QColor(255, 255, 255, 220));
    m_region.label->setPen(QPen(kRegionEdge));
    m_region.label->setClipToAxisRect(true);
    m_region.label->setVisible(false);
}

void PlotRenderer::rebuildAll()
{
    applyStyle();
    for (const auto& d : m_model->datasets()) onDatasetAdded(d.id);
    for (const auto& m : m_model->matrices()) onMatrixAdded(m.id);
    for (const auto& m : m_model->markers()) onMarkerAdded(m.id);
    for (const auto& a : m_model->annotations()) onAnnotationAdded(a.id);
    applyRegion();
    applyAxesInfo();
    replot();
}

// ---- view ----------------------------------------------------------------

ViewRange PlotRenderer::viewRange() const
{
    const QCPRange x = m_x->range();
    const QCPRange y = m_y->range();
    return {x.lower, x.upper, y.lower, y.upper};
}

void PlotRenderer::setViewRange(const ViewRange& r)
{
    m_x->setRange(r.xmin, r.xmax);
    m_y->setRange(r.ymin, r.ymax);
    // keep the mirrored frame axes in sync (tick positions)
    m_x2->setRange(m_x->range());
    m_y2->setRange(m_y->range());
    replot();
}

void PlotRenderer::autoscale(bool doX, bool doY)
{
    double xmin, xmax, ymin, ymax;
    const bool xLog = m_model->axesInfo().xLog, yLog = m_model->axesInfo().yLog;
    if (!m_model->dataBounds(xmin, xmax, ymin, ymax, true, xLog, yLog)) return;
    // Padding in log space for logarithmic axes.
    auto pad = [](double& lo, double& hi, double fraction, bool log) {
        if (!log) return padRange(lo, hi, fraction);
        double a = std::log10(lo), b = std::log10(hi);
        padRange(a, b, fraction);
        lo = std::pow(10.0, a);
        hi = std::pow(10.0, b);
    };
    ViewRange r = viewRange();
    if (doX) {
        pad(xmin, xmax, 0.0, xLog);  // x usually spans the data exactly
        r.xmin = xmin;
        r.xmax = xmax;
    }
    if (doY) {
        // Curves get headroom; a pure 2D (contour) view is fitted exactly.
        bool anyCurve = false;
        for (const auto& d : m_model->datasets()) anyCurve |= d.style.visible && !d.x.isEmpty();
        pad(ymin, ymax, anyCurve ? 0.05 : 0.0, yLog);
        r.ymin = ymin;
        r.ymax = ymax;
    }
    setViewRange(r);
}

void PlotRenderer::setHomeView(const ViewRange& r)
{
    m_home = r;
    m_hasHome = true;
}

void PlotRenderer::resetView()
{
    if (m_hasHome) setViewRange(m_home);
    else autoscale();
}

void PlotRenderer::maybeCaptureHome()
{
    if (m_hasHome || !m_autoHome) return;
    autoscale();
    captureHomeView();
}

// ---- coordinates -----------------------------------------------------------

QPointF PlotRenderer::pixelToData(const QPointF& px) const
{
    return {m_x->pixelToCoord(px.x()), m_y->pixelToCoord(px.y())};
}

QPointF PlotRenderer::dataToPixel(const QPointF& d) const
{
    return {m_x->coordToPixel(d.x()), m_y->coordToPixel(d.y())};
}

bool PlotRenderer::isInsidePlotArea(const QPointF& px) const
{
    return m_rect->rect().contains(px.toPoint());
}

// ---- hit testing -------------------------------------------------------------

ObjectRef PlotRenderer::hitTestObject(const QPointF& px, double tol) const
{
    // QCPItemText::selectTest reports a click *inside* the text box as
    // 0.99 * selectionTolerance, so text items use at least that tolerance.
    const double textTol = std::max(tol, double(m_plot->selectionTolerance()));
    // Annotations are drawn above markers, so test them first (last added on top).
    const auto& anns = m_model->annotations();
    for (auto it = anns.rbegin(); it != anns.rend(); ++it) {
        if (auto* item = m_annotations.value(it->id)) {
            const double d = item->selectTest(px, false);
            if (d >= 0 && d <= textTol) return {ObjectKind::Annotation, it->id};
        }
    }
    // Peaks (crosses and their labels) come next.
    if (m_peaks) {
        const PeakOverlay::Hit h = m_peaks->hitTest(px, tol);
        if (h.id != kInvalidId) return {ObjectKind::Peak, h.id};
    }
    if (!isInsidePlotArea(px)) return {};
    const auto& markers = m_model->markers();
    ObjectRef best;
    double bestDist = tol;
    for (auto it = markers.rbegin(); it != markers.rend(); ++it) {
        const MarkerVisual v = m_markers.value(it->id);
        if (!v.line) continue;
        double d;
        if (it->orientation == Orientation::Vertical)
            d = std::abs(px.x() - m_x->coordToPixel(it->position));
        else
            d = std::abs(px.y() - m_y->coordToPixel(it->position));
        if (v.label && v.label->visible()) {
            const double dl = v.label->selectTest(px, false);
            if (dl >= 0 && dl <= textTol) d = std::min(d, std::min(dl, tol));
        }
        if (d <= bestDist) {
            bestDist = d;
            best = {ObjectKind::Marker, it->id};
        }
    }
    return best;
}

RegionHit PlotRenderer::hitTestRegion(const QPointF& px, double tol) const
{
    const Region& r = m_model->region();
    if (!r.active || !isInsidePlotArea(px)) return RegionHit::None;
    const double p0 = m_x->coordToPixel(r.xmin);
    const double p1 = m_x->coordToPixel(r.xmax);
    const double lo = std::min(p0, p1), hi = std::max(p0, p1);
    // Edge nearest to the cursor wins (matters when the region is narrow).
    const double d0 = std::abs(px.x() - p0), d1 = std::abs(px.x() - p1);
    if (std::min(d0, d1) <= tol) return d0 <= d1 ? RegionHit::MinEdge : RegionHit::MaxEdge;
    if (px.x() > lo && px.x() < hi) return RegionHit::Inside;
    return RegionHit::None;
}

void PlotRenderer::setHighlight(const ObjectRef& ref)
{
    if (ref == m_highlight) return;
    const ObjectRef old = m_highlight;
    m_highlight = ref;
    for (const ObjectRef& r : {old, ref}) {
        if (r.kind == ObjectKind::Marker) applyMarker(r.id);
        else if (r.kind == ObjectKind::Annotation) applyAnnotation(r.id);
    }
    m_peaks->setHighlight(ref.kind == ObjectKind::Peak ? ref.id : kInvalidId);
    replotObjectLayers();
}

Style PlotRenderer::displayStyle() const
{
    Style s = m_model->style();
    if (DisplayTheme::instance()->plotOverlayActive(m_exportMode)) DisplayTheme::instance()->applyTo(s);
    return s;
}

QColor PlotRenderer::ink(const QColor& c) const
{
    return DisplayTheme::instance()->plotOverlayActive(m_exportMode) ? DisplayTheme::instance()->ink(c) : c;
}

QColor PlotRenderer::labelBox(const Style& st)
{
    QColor c = st.axesFace;  // white on light styles, the theme's plot background on dark ones
    c.setAlpha(225);
    return c;
}

void PlotRenderer::refreshAppearance()
{
    applyStyle();  // also markers, annotations, axes labels, peaks
    for (const Dataset& d : m_model->datasets()) {
        applyDatasetAppearance(d.id);
        updateErrorBars(d.id);
    }
    for (const Matrix& m : m_model->matrices()) applyMatrixAppearance(m.id);
    applyRegion();
    replot();
}

void PlotRenderer::setExportMode(bool on)
{
    const bool themeChanges = DisplayTheme::instance()->plotOverlayActive(m_exportMode) !=
                              DisplayTheme::instance()->plotOverlayActive(on);
    m_exportMode = on;
    if (themeChanges) {  // exports use the figure's own colours (see DisplayTheme)
        refreshAppearance();
        if (m_plot) m_plot->replot(QCustomPlot::rpImmediateRefresh);
    }
    m_peaks->setExportMode(on);
    if (m_highlight.kind == ObjectKind::Marker) applyMarker(m_highlight.id);
    else if (m_highlight.kind == ObjectKind::Annotation) applyAnnotation(m_highlight.id);
}

void PlotRenderer::replot()
{
    if (m_plot) m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void PlotRenderer::replotObjectLayers()
{
    if (!m_plot) return;
    m_regionLayer->replot();
    m_objectLayer->replot();
}

// ---- datasets ------------------------------------------------------------------

void PlotRenderer::onDatasetAdded(ObjectId id)
{
    const Dataset* d = m_model->dataset(id);
    if (!d || m_plottables.contains(id)) return;
    applyDatasetData(id);  // creates the plottable
    applyDatasetAppearance(id);
    for (const auto& o : m_model->datasets())  // bands filled towards this dataset
        if (o.style.fillTo == id) applyDatasetAppearance(o.id);
    syncLegend();
    maybeCaptureHome();
    replot();
}

void PlotRenderer::onDatasetRemoved(ObjectId id)
{
    removeErrorBars(id);
    if (auto* p = m_plottables.take(id)) m_plot->removePlottable(p);
    // Bands that were filled towards this dataset lose their partner.
    for (const auto& d : m_model->datasets())
        if (d.style.fillTo == id) applyDatasetAppearance(d.id);
    syncLegend();
    replot();
}

void PlotRenderer::applyDatasetData(ObjectId id)
{
    const Dataset* d = m_model->dataset(id);
    if (!d) return;
    // QCPGraph requires monotonically increasing x (it sorts by key and uses
    // adaptive sampling, which keeps very large data sets fast). Data that is
    // not sorted in x - e.g. a parametric curve - uses QCPCurve, which keeps
    // the point order.
    const bool sorted = isSorted(d->x);
    QCPAbstractPlottable* p = m_plottables.value(id);
    const bool needGraph = sorted;
    if (p && (qobject_cast<QCPGraph*>(p) != nullptr) != needGraph) {
        removeErrorBars(id);  // attached to the old plottable
        m_plot->removePlottable(p);
        p = nullptr;
    }
    if (!p) {
        if (needGraph) p = new QCPGraph(m_x, m_y);
        else p = new QCPCurve(m_x, m_y);
        p->setSelectable(QCP::stNone);
        m_plottables.insert(id, p);
        applyDatasetAppearance(id);
        // Bands filled towards this dataset point at the old plottable: relink.
        for (const auto& o : m_model->datasets())
            if (o.style.fillTo == id) applyDatasetAppearance(o.id);
        syncLegend();
    }
    if (auto* g = qobject_cast<QCPGraph*>(p)) g->setData(d->x, d->y, true);
    else if (auto* c = qobject_cast<QCPCurve*>(p)) c->setData(d->x, d->y);
    updateErrorBars(id);
}

void PlotRenderer::removeErrorBars(ObjectId id)
{
    const ErrorVisual e = m_errorBars.take(id);
    if (e.y) m_plot->removePlottable(e.y);
    if (e.x) m_plot->removePlottable(e.x);
}

void PlotRenderer::updateErrorBars(ObjectId id)
{
    const Dataset* d = m_model->dataset(id);
    auto* target = dynamic_cast<QCPPlottableInterface1D*>(m_plottables.value(id));
    if (!d || !target) return;
    ErrorVisual& e = m_errorBars[id];
    const Style st = displayStyle();
    auto make = [&](QCPErrorBars*& bars, const QVector<double>& lo, const QVector<double>& hi,
                    QCPErrorBars::ErrorType type) {
        if (lo.isEmpty() && hi.isEmpty()) {
            if (bars) m_plot->removePlottable(bars);
            bars = nullptr;
            return;
        }
        if (!bars) {
            bars = new QCPErrorBars(m_x, m_y);
            bars->setSelectable(QCP::stNone);
            bars->removeFromLegend();
        }
        bars->setErrorType(type);
        bars->setDataPlottable(m_plottables.value(id));
        const QVector<double>& l = lo.isEmpty() ? hi : lo;
        const QVector<double>& h = hi.isEmpty() ? lo : hi;
        bars->setData(l, h);
        QPen pen(ink(d->style.color), std::max(1.0, d->style.lineWidth * 0.8));
        pen.setCapStyle(Qt::FlatCap);
        bars->setPen(pen);
        bars->setWhiskerWidth(st.errorbarCapSize * 2);
        bars->setSymbolGap(d->style.scatter != ScatterShape::None ? d->style.scatterSize : 0);
        bars->setVisible(d->style.visible);
    };
    // Error arrays are in data order: QCPGraph only receives already sorted x
    // (setData(..., alreadySorted = true)) and QCPCurve keeps the order.
    make(e.y, d->yErrLow, d->yErrHigh, QCPErrorBars::etValueError);
    make(e.x, d->xErrLow, d->xErrHigh, QCPErrorBars::etKeyError);
    if (!e.x && !e.y) m_errorBars.remove(id);
}

void PlotRenderer::applyDatasetAppearance(ObjectId id)
{
    const Dataset* d = m_model->dataset(id);
    QCPAbstractPlottable* p = m_plottables.value(id);
    if (!d || !p) return;
    const DatasetStyle& s = d->style;
    const QColor color = ink(s.color);  // e.g. black lines stay visible on dark themes
    QPen pen(color, s.lineWidth, toPenStyle(s.lineStyle), Qt::RoundCap, Qt::RoundJoin);
    p->setPen(pen);
    p->setName(d->name);
    p->setVisible(s.visible);
    // Filled symbols (open symbols like x/+ ignore the brush).
    const QCPScatterStyle scatter(toScatterShape(s.scatter), color, color, s.scatterSize);
    p->setBrush(s.fillColor.isValid() ? QBrush(s.fillColor) : QBrush(Qt::NoBrush));
    if (auto* g = qobject_cast<QCPGraph*>(p)) {
        QCPGraph::LineStyle ls = QCPGraph::lsLine;
        // QCustomPlot names: lsStepLeft = point on the left of its step (= "post").
        switch (s.step) {
        case StepMode::None: ls = QCPGraph::lsLine; break;
        case StepMode::Post: ls = QCPGraph::lsStepLeft; break;
        case StepMode::Pre: ls = QCPGraph::lsStepRight; break;
        case StepMode::Mid: ls = QCPGraph::lsStepCenter; break;
        }
        // "No line" keeps the line geometry (needed for fills and as a band
        // partner) but draws it with an invisible pen.
        if (s.lineStyle == LineStyle::None) p->setPen(Qt::NoPen);
        g->setLineStyle(ls);
        g->setScatterStyle(scatter);
        QCPGraph* partner = qobject_cast<QCPGraph*>(m_plottables.value(s.fillTo));
        g->setChannelFillGraph(s.fillColor.isValid() && partner != g ? partner : nullptr);
    } else if (auto* c = qobject_cast<QCPCurve*>(p)) {
        c->setLineStyle(s.lineStyle == LineStyle::None ? QCPCurve::lsNone : QCPCurve::lsLine);
        c->setScatterStyle(scatter);
    }
    if (m_errorBars.contains(id)) updateErrorBars(id);
}

void PlotRenderer::syncLegend()
{
    // Legend order follows dataset order in the model; hidden or unnamed
    // datasets are omitted.
    m_legend->clearItems();
    for (const auto& d : m_model->datasets()) {
        QCPAbstractPlottable* p = m_plottables.value(d.id);
        if (p && d.style.visible && d.style.inLegend && !d.name.isEmpty())
            m_legend->addItem(new RichLegendItem(m_legend, p, m_model->style().mathText));
    }
    for (const auto& m : m_model->matrices()) {
        const MatrixVisual v = m_matrices.value(m.id);
        // Images without contours have no legend entry (as in matplotlib); the
        // colour bar carries their name.
        if (v.contour && m.style.visible && m.style.showContours && !m.name.isEmpty())
            m_legend->addItem(new RichLegendItem(m_legend, v.contour, m_model->style().mathText));
    }
    m_legend->setVisible(m_model->axesInfo().legendVisible &&
                               m_legend->itemCount() > 0);
}

// ---- 2D data ---------------------------------------------------------------------------

ContourPlottable* PlotRenderer::contourPlottable(ObjectId id) const
{
    return m_matrices.value(id).contour;
}

const Matrix* PlotRenderer::matrixAt(const QPointF& d) const
{
    const auto& ms = m_model->matrices();
    for (auto it = ms.rbegin(); it != ms.rend(); ++it) {
        if (!it->style.visible) continue;
        if (d.x() >= std::min(it->x0, it->x1) && d.x() <= std::max(it->x0, it->x1) &&
            d.y() >= std::min(it->y0, it->y1) && d.y() <= std::max(it->y0, it->y1))
            return &*it;
    }
    return nullptr;
}

QList<ObjectId> PlotRenderer::levelTargets() const
{
    if (m_highlight.kind == ObjectKind::Matrix && m_model->matrix(m_highlight.id))
        return {m_highlight.id};
    QList<ObjectId> ids;
    for (const auto& m : m_model->matrices())
        if (m.style.visible) ids.push_back(m.id);
    return ids;
}

void PlotRenderer::onMatrixAdded(ObjectId id)
{
    if (!m_model->matrix(id) || m_matrices.contains(id)) return;
    MatrixVisual v;
    v.contour = new ContourPlottable(m_x, m_y);
    m_matrices.insert(id, v);
    applyMatrixData(id);
    applyMatrixAppearance(id);
    syncLegend();
    maybeCaptureHome();
    replot();
}

void PlotRenderer::onMatrixRemoved(ObjectId id)
{
    const MatrixVisual v = m_matrices.take(id);
    if (v.contour) m_plot->removePlottable(v.contour);
    if (v.image) m_plot->removePlottable(v.image);
    if (m_highlight.kind == ObjectKind::Matrix && m_highlight.id == id) m_highlight = {};
    syncLegend();
    replot();
}

void PlotRenderer::applyMatrixData(ObjectId id)
{
    const Matrix* m = m_model->matrix(id);
    const MatrixVisual v = m_matrices.value(id);
    if (!m || !v.contour) return;
    v.contour->setMatrix(*m);
    updateMatrixImage(id);
}

void PlotRenderer::applyMatrixAppearance(ObjectId id)
{
    const Matrix* m = m_model->matrix(id);
    const MatrixVisual v = m_matrices.value(id);
    if (!m || !v.contour) return;
    ContourStyle cs = m->style;
    if (!cs.showImage) {  // over the (light) colour-map image the original colours read best
        cs.positiveColor = ink(cs.positiveColor);  // dark contours stay visible on dark themes
        cs.negativeColor = ink(cs.negativeColor);
    }
    v.contour->setContourStyle(cs);  // recomputes only if the levels changed
    v.contour->setName(m->name);
    v.contour->setVisible(m->style.visible && m->style.showContours);
    // The colour map's cells only have to be rebuilt when it is switched on
    // (the data path rebuilds them too); level scrolling must not redo nx*ny
    // cells. Colours and limits are cheap.
    if (m->style.showImage != (v.image != nullptr)) updateMatrixImage(id);
    else if (v.image) applyImageAppearance(*m, m_matrices[id]);
    updateColorBar();
    syncLegend();  // contours on/off, name
}

void PlotRenderer::applyImageAppearance(const Matrix& m, MatrixVisual& v)
{
    if (!v.image) return;
    const ContourStyle& s = m.style;
    const bool signedData = v.lo < 0 && v.hi > 0;
    QCPColorGradient g;
    colormapGradient(s.colormap, signedData, &g);
    QCPRange range;
    if (s.fixedRange && s.vmax > s.vmin) {
        range = QCPRange(s.vmin, s.vmax);
    } else if (colormapIsDiverging(s.colormap, signedData)) {
        const double a = std::max({std::abs(v.lo), std::abs(v.hi), 1e-300});
        range = QCPRange(-a, a);  // zero sits in the middle of the map
    } else {
        range = QCPRange(v.lo, v.hi > v.lo ? v.hi : v.lo + 1);
    }
    v.image->setGradient(g);
    v.image->setDataRange(range);
    v.image->setVisible(s.visible);
}

void PlotRenderer::updateColorBar()
{
    // One colour bar per panel: for the first visible image that asks for one.
    const Matrix* shown = nullptr;
    for (const Matrix& m : m_model->matrices())
        if (m.style.visible && m.style.showImage && m.style.colorBar && m_matrices.value(m.id).image) {
            shown = &m;
            break;
        }
    if (!shown) {
        if (m_colorBar && m_colorBar->visible()) {
            m_colorBar->setVisible(false);
            m_y2->setPadding(m_y2Padding);
        }
        return;
    }
    if (!m_colorBar) {
        m_colorBar = new ColorBar(m_plot, m_rect);
        m_colorBar->setLayer(m_objectLayer);
        m_y2Padding = m_y2->padding();
    }
    const Style st = displayStyle();
    const QCPColorMap* img = m_matrices.value(shown->id).image;
    m_colorBar->set(img->gradient(), img->dataRange(), shown->name, font(st.ytick.labelSize), st.axesEdge,
                    st.ytick.labelColor, st.mathText);
    m_colorBar->setVisible(m_panelVisible);
    m_y2->setPadding(m_y2Padding + m_colorBar->requiredWidth());  // room in the right margin
}

void PlotRenderer::updateMatrixImage(ObjectId id)
{
    const Matrix* m = m_model->matrix(id);
    MatrixVisual& v = m_matrices[id];
    const bool want = m && m->style.showImage && m->nx > 0 && m->ny > 0;
    if (!want) {
        if (v.image) m_plot->removePlottable(v.image);
        v.image = nullptr;
        return;
    }
    if (!v.image) {
        v.image = new QCPColorMap(m_x, m_y);
        v.image->setLayer(m_imageLayer);
        v.image->setSelectable(QCP::stNone);
        v.image->setInterpolate(true);
        v.image->setTightBoundary(false);
    }
    auto* data = new QCPColorMapData(m->nx, m->ny, QCPRange(m->x0, m->x1), QCPRange(m->y0, m->y1));
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (int iy = 0; iy < m->ny; ++iy)
        for (int ix = 0; ix < m->nx; ++ix) {
            const double z = m->z[qsizetype(iy) * m->nx + ix];
            data->setCell(ix, iy, z);
            if (std::isfinite(z)) lo = std::min(lo, z), hi = std::max(hi, z);
        }
    if (!(hi >= lo)) lo = hi = 0;  // no finite value
    v.lo = lo;
    v.hi = hi;
    v.image->setData(data, false);
    applyImageAppearance(*m, v);
    updateColorBar();
}

// ---- markers -------------------------------------------------------------------------

void PlotRenderer::onMarkerAdded(ObjectId id)
{
    if (m_markers.contains(id)) return;
    MarkerVisual v;
    v.line = new QCPItemStraightLine(m_plot);
    bindItem(v.line);
    v.line->setLayer(m_objectLayer);
    v.line->setSelectable(false);
    v.label = new RichTextItem(m_plot);
    bindItem(v.label);
    v.label->setLayer(m_objectLayer);
    v.label->setSelectable(false);
    v.label->setPadding(QMargins(4, 2, 4, 2));
    v.label->setBrush(QColor(255, 255, 255, 225));
    v.label->setClipToAxisRect(true);
    m_markers.insert(id, v);
    applyMarker(id);
    replotObjectLayers();
}

void PlotRenderer::onMarkerRemoved(ObjectId id)
{
    const MarkerVisual v = m_markers.take(id);
    if (v.line) m_plot->removeItem(v.line);
    if (v.label) m_plot->removeItem(v.label);
    if (m_highlight.kind == ObjectKind::Marker && m_highlight.id == id) m_highlight = {};
    replotObjectLayers();
}

void PlotRenderer::applyMarker(ObjectId id)
{
    const Marker* m = m_model->marker(id);
    const MarkerVisual v = m_markers.value(id);
    if (!m || !v.line) return;
    const bool hl = !m_exportMode && m_highlight == ObjectRef{ObjectKind::Marker, id};

    QPen pen(ink(m->color), m->lineWidth + (hl ? 1.0 : 0.0), toPenStyle(m->lineStyle));
    if (m->lineStyle == LineStyle::None) pen.setStyle(Qt::SolidLine);
    v.line->setPen(pen);

    if (m->orientation == Orientation::Vertical) {
        v.line->point1->setTypeX(QCPItemPosition::ptPlotCoords);
        v.line->point1->setTypeY(QCPItemPosition::ptAxisRectRatio);
        v.line->point2->setTypeX(QCPItemPosition::ptPlotCoords);
        v.line->point2->setTypeY(QCPItemPosition::ptAxisRectRatio);
        v.line->point1->setCoords(m->position, 0.0);
        v.line->point2->setCoords(m->position, 1.0);
        // Label hangs from the top of the plot area, just right of the line.
        v.label->position->setTypeX(QCPItemPosition::ptPlotCoords);
        v.label->position->setTypeY(QCPItemPosition::ptAxisRectRatio);
        v.label->position->setCoords(m->position, 0.008);
        v.label->setPositionAlignment(Qt::AlignTop | Qt::AlignLeft);
        v.label->setPadding(QMargins(4, 2, 4, 2));
    } else {
        v.line->point1->setTypeX(QCPItemPosition::ptAxisRectRatio);
        v.line->point1->setTypeY(QCPItemPosition::ptPlotCoords);
        v.line->point2->setTypeX(QCPItemPosition::ptAxisRectRatio);
        v.line->point2->setTypeY(QCPItemPosition::ptPlotCoords);
        v.line->point1->setCoords(0.0, m->position);
        v.line->point2->setCoords(1.0, m->position);
        v.label->position->setTypeX(QCPItemPosition::ptAxisRectRatio);
        v.label->position->setTypeY(QCPItemPosition::ptPlotCoords);
        v.label->position->setCoords(0.006, m->position);
        v.label->setPositionAlignment(Qt::AlignBottom | Qt::AlignLeft);
    }

    QString text = m->label;
    if (m->showValue) {
        const QString value = (m->orientation == Orientation::Vertical ? "x = " : "y = ") +
                              fmt(m->position);
        text = text.isEmpty() ? value : text + "\n" + value;
    }
    const Style st = displayStyle();
    v.label->setText(text, font(st.xtick.labelSize), st.mathText);
    v.label->setColor(st.labelColor);
    v.label->setVisible(!text.isEmpty() && m_panelVisible);
    v.line->setVisible(m_panelVisible);
    QPen border(ink(m->color), hl ? 1.5 : 1.0);
    v.label->setPen(border);
    v.label->setBrush(labelBox(st));
}

// ---- annotations -------------------------------------------------------------------------

void PlotRenderer::onAnnotationAdded(ObjectId id)
{
    if (m_annotations.contains(id)) return;
    auto* t = new RichTextItem(m_plot);
    bindItem(t);
    t->setLayer(m_objectLayer);
    t->setSelectable(false);
    t->position->setType(QCPItemPosition::ptPlotCoords);
    t->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
    t->setPadding(QMargins(3, 2, 3, 2));
    m_annotations.insert(id, t);
    applyAnnotation(id);
    replotObjectLayers();
}

void PlotRenderer::onAnnotationRemoved(ObjectId id)
{
    if (auto* t = m_annotations.take(id)) m_plot->removeItem(t);
    if (m_highlight.kind == ObjectKind::Annotation && m_highlight.id == id) m_highlight = {};
    replotObjectLayers();
}

void PlotRenderer::applyAnnotation(ObjectId id)
{
    const Annotation* a = m_model->annotation(id);
    RichTextItem* t = m_annotations.value(id);
    if (!a || !t) return;
    const Style st = displayStyle();
    const bool hl = !m_exportMode && m_highlight == ObjectRef{ObjectKind::Annotation, id};
    t->position->setCoords(a->x, a->y);
    // Unset size/colour follow the style (annotation.size / annotation.color).
    t->setText(a->text.isEmpty() ? QStringLiteral(" ") : a->text,
               font(a->fontSize > 0 ? a->fontSize : st.annotationSize), st.mathText);
    t->setColor(a->color.isValid() ? ink(a->color) : st.annotationColor);
    t->setPen(hl ? QPen(kHighlight, 1, Qt::DashLine) : QPen(Qt::NoPen));
}

// ---- region / axes ---------------------------------------------------------------------------

void PlotRenderer::applyRegion()
{
    const Region& r = m_model->region();
    const bool on = r.active && m_panelVisible;
    m_region.rect->setVisible(on);
    m_region.minEdge->setVisible(on);
    m_region.maxEdge->setVisible(on);
    m_region.label->setVisible(on && m_regionLabelOn);
    if (!on) return;
    m_region.rect->topLeft->setCoords(r.xmin, 0.0);
    m_region.rect->bottomRight->setCoords(r.xmax, 1.0);
    m_region.minEdge->point1->setCoords(r.xmin, 0.0);
    m_region.minEdge->point2->setCoords(r.xmin, 1.0);
    m_region.maxEdge->point1->setCoords(r.xmax, 0.0);
    m_region.maxEdge->point2->setCoords(r.xmax, 1.0);
    m_region.label->position->setCoords(0.5 * (r.xmin + r.xmax), 0.992);
    m_region.label->setText(QString("%1 – %2   (Δ %3)")
                                .arg(fmt(r.xmin), fmt(r.xmax), fmt(r.xmax - r.xmin)));
}

QFont PlotRenderer::font(double pointSize, bool bold) const
{
    const Style st = displayStyle();
    QFont f = m_plot->font();
    QStringList families;
    for (const QString& part : st.fontFamily.split(',', Qt::SkipEmptyParts)) {
        const QString name = part.trimmed();
        if (name == "sans-serif") f.setStyleHint(QFont::SansSerif);
        else if (name == "serif") f.setStyleHint(QFont::Serif);
        else if (name == "monospace") f.setStyleHint(QFont::Monospace);
        else families << name;
    }
    if (!families.isEmpty()) f.setFamilies(families);
    f.setPointSizeF(std::max(1.0, pointSize));
    f.setBold(bold);
    return f;
}

void PlotRenderer::applyStyle()
{
    const Style st = displayStyle();
    QCustomPlot* p = m_plot;
    if (m_primary) {  // figure-level settings come from the first panel
        p->setFont(font(st.fontSize));
        p->setBackground(st.figureFace);
        const int pad = int(std::lround(st.figurePadding));
        p->plotLayout()->setMargins(QMargins(pad, pad, pad, pad));
    }
    m_rect->setBackground(st.axesFace);
    QPen diagPen(st.axesEdge, 1, Qt::DashLine);  // the y = x line follows the frame colour
    diagPen.setCosmetic(true);
    m_diagonal->setPen(diagPen);

    auto cosmetic = [](const QColor& c, double w, Qt::PenStyle style = Qt::SolidLine) {
        QPen pen(c, w, style, Qt::FlatCap);
        pen.setCosmetic(true);
        return pen;
    };
    for (QCPAxis* axis : m_rect->axes()) {
        const bool isX = axis->orientation() == Qt::Horizontal;
        const TickStyle& ts = isX ? st.xtick : st.ytick;
        const bool mirror = axis == m_x2 || axis == m_y2;
        axis->setBasePen(cosmetic(st.axesEdge, st.axesLineWidth));
        axis->setTickPen(cosmetic(st.axesEdge, ts.width));
        axis->setSubTickPen(cosmetic(st.axesEdge, ts.width));
        const double inMaj = ts.direction == "out" ? 0 : ts.majorSize;
        const double outMaj = ts.direction == "in" ? 0 : ts.majorSize;
        const double inMin = ts.direction == "out" ? 0 : ts.minorSize;
        const double outMin = ts.direction == "in" ? 0 : ts.minorSize;
        axis->setTickLength(int(std::lround(inMaj)), int(std::lround(outMaj)));
        axis->setSubTickLength(int(std::lround(inMin)), int(std::lround(outMin)));
        const bool ticksOn = !mirror || ts.mirror;
        axis->setTicks(ticksOn);
        axis->setSubTicks(ticksOn && ts.minorVisible);
        axis->setTickLabelFont(font(ts.labelSize));
        axis->setTickLabelColor(ts.labelColor);
        applyNumberFormat(axis, st);
        if (auto t = axis->ticker().dynamicCast<StyledTicker>()) {
            t->unicodeMinus = st.unicodeMinus;
            t->setTickCount(std::max(1, ts.count));
        }
        axis->grid()->setPen(cosmetic(st.gridColor, st.gridWidth, gridPenStyle(st.gridStyle)));
        axis->grid()->setSubGridPen(cosmetic(st.minorGridColor, st.gridWidth, gridPenStyle(st.gridStyle)));
        axis->grid()->setSubGridVisible(!mirror && st.minorGrid);
        axis->setLabelColor(st.labelColor);
        axis->setLabelFont(font(st.labelSize));
        axis->setPadding(0);
    }
    // Mirrored axes follow the primary tick ranges (setupFullAxesBox).
    m_x2->setTicker(m_x->ticker());
    m_y2->setTicker(m_y->ticker());
    for (QCPAxis* axis : {m_x, m_y})
        if (auto t = axis->ticker().dynamicCast<QCPAxisTickerLog>()) t->setSubTickCount(8);

    // Legend
    QCPLegend* lg = m_legend;
    lg->setFont(font(st.legendFontSize));
    lg->setTextColor(st.labelColor);
    lg->setBorderPen(st.legendFrame ? QPen(st.legendEdge) : QPen(Qt::NoPen));
    lg->setBrush(st.legendFrame ? QBrush(st.legendFace) : QBrush(Qt::NoBrush));
    lg->setRowSpacing(st.legendFrame ? 0 : 2);
    m_rect->insetLayout()->setInsetAlignment(0, locToAlignment(st.legendLoc));

    // In-frame label (e.g. "**CMS** *Simulation*")
    const Qt::Alignment ia = locToAlignment(st.labelInsideLoc);
    m_insideLabel->setText(st.labelInside, font(st.labelInsideSize), st.mathText);
    m_insideLabel->setColor(st.labelColor);
    m_insideLabel->setPositionAlignment(ia);
    m_insideLabel->position->setCoords(ia & Qt::AlignLeft ? 0.03 : 0.97, ia & Qt::AlignTop ? 0.03 : 0.97);
    m_insideLabel->setVisible(m_primary && m_panelVisible && !st.labelInside.isEmpty());  // first panel only

    // Peaks
    PeakAppearance pa = m_peaks->appearance();
    pa.color = st.peakColor;
    pa.labelColor = st.peakLabelColor;
    pa.crossSize = st.peakSize;
    pa.lineWidth = st.peakLineWidth;
    pa.halo = labelBox(st);
    pa.halo.setAlpha(200);
    pa.fontSize = st.peakLabelSize;
    pa.fontFamily = font(st.peakLabelSize);
    pa.math = st.mathText;
    m_peaks->setAppearance(pa);
    m_region.label->setFont(font(st.xtick.labelSize));
    m_region.label->setBrush(labelBox(st));
    m_region.label->setColor(st.labelColor);

    for (const auto& m : m_model->markers()) applyMarker(m.id);
    for (const auto& a : m_model->annotations()) applyAnnotation(a.id);
    applyAxesInfo();
}

void PlotRenderer::updateHeader()
{
    if (!m_primary) return;  // one header per figure
    const AxesInfo& info = m_model->axesInfo();
    const Style st = displayStyle();
    m_header->setTexts(info.title, font(st.titleSize, st.titleBold), st.titleColor, st.labelTopLeft,
                       st.labelTopRight, font(st.labelSize), st.labelColor, st.mathText);
    QCPLayoutGrid* layout = m_plot->plotLayout();
    const bool inLayout = layout->hasElement(0, 0) && layout->element(0, 0) == m_header;
    if (!m_header->isEmpty() && !inLayout) {
        layout->insertRow(0);
        layout->addElement(0, 0, m_header);
    } else if (m_header->isEmpty() && inLayout) {
        layout->take(m_header);
        layout->simplify();
    }
}

void PlotRenderer::applyAxesInfo()
{
    const AxesInfo& info = m_model->axesInfo();
    const Style st = displayStyle();
    // Rich-text labels: QCustomPlot reserves the space (a blank label in the
    // label font plus extra padding for taller rich text), RichAxisLabels draws.
    const QFont lf = font(st.labelSize);
    m_axisLabels->setLabels(info.xLabel, info.yLabel, lf, st.labelColor, st.mathText);
    const double lineH = QFontMetricsF(lf).height();
    m_x->setLabel(info.xLabel.isEmpty() ? QString() : QStringLiteral(" "));
    m_y->setLabel(info.yLabel.isEmpty() ? QString() : QStringLiteral(" "));
    m_x->setLabelPadding(int(std::lround(st.labelPad + std::max(0.0, m_axisLabels->xSize().height() - lineH))));
    m_y->setLabelPadding(int(std::lround(st.labelPad + std::max(0.0, m_axisLabels->ySize().height() - lineH))));
    m_x->grid()->setVisible(info.gridVisible);
    m_y->grid()->setVisible(info.gridVisible);
    // Linear / logarithmic scales (each with its own ticker).
    auto setScale = [](QCPAxis* a, QCPAxis* mirror, bool log, const QSharedPointer<QCPAxisTicker>& lin) {
        for (QCPAxis* ax : {a, mirror}) {
            if (log && ax->scaleType() != QCPAxis::stLogarithmic) {
                ax->setScaleType(QCPAxis::stLogarithmic);
                auto t = QSharedPointer<QCPAxisTickerLog>::create();
                t->setLogBase(10);
                t->setSubTickCount(8);
                ax->setTicker(t);
            } else if (!log && ax->scaleType() != QCPAxis::stLinear) {
                ax->setScaleType(QCPAxis::stLinear);
                ax->setTicker(lin);
            }
        }
        a->setNumberFormat(log ? "eb" : a->numberFormat());
        if (log) a->setNumberPrecision(0);
        mirror->setTicker(a->ticker());
    };
    if (!m_linTickerX) m_linTickerX = m_x->ticker(), m_linTickerY = m_y->ticker();
    setScale(m_x, m_x2, info.xLog, m_linTickerX);
    setScale(m_y, m_y2, info.yLog, m_linTickerY);
    // Custom ticks replace the axis' ticker (and are taken away again when
    // switched off, restoring whatever ticker the axis had, e.g. clock time).
    auto setCustomTicks = [](QCPAxis* a, QCPAxis* mirror, bool on, const QVector<double>& pos,
                             const QStringList& labels, QSharedPointer<QCPAxisTicker>& saved) {
        if (on) {
            if (!saved) saved = a->ticker();
            auto t = QSharedPointer<QCPAxisTickerText>::create();
            for (qsizetype i = 0; i < pos.size(); ++i) {
                QString text = i < labels.size() ? labels[i] : QString::number(pos[i], 'g', 6);
                t->addTick(pos[i], text.replace('-', QChar(0x2212)));
            }
            t->setSubTickCount(0);
            a->setTicker(t);
            mirror->setTicker(t);
        } else if (saved) {
            a->setTicker(saved);
            mirror->setTicker(saved);
            saved.reset();
        }
    };
    setCustomTicks(m_x, m_x2, info.customXTicks, info.xTicks, info.xTickLabels, m_tickerBeforeCustomX);
    setCustomTicks(m_y, m_y2, info.customYTicks, info.yTicks, info.yTickLabels, m_tickerBeforeCustomY);
    // An axis switched back from log to linear gets the style's format again.
    for (QCPAxis* a : {m_x, m_y}) applyNumberFormat(a, st);
    m_x->setRangeReversed(info.xReversed);
    m_x2->setRangeReversed(info.xReversed);
    m_y->setRangeReversed(info.yReversed);
    m_y2->setRangeReversed(info.yReversed);
    m_diagonal->setVisible(info.showDiagonal && m_panelVisible);
    PeakAppearance pa = m_peaks->appearance();
    pa.showLabels = info.showPeakLabels;
    m_peaks->setAppearance(pa);
    updateHeader();
    syncLegend();
    replotObjectLayers();
}

}  // namespace gp
