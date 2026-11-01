#include "PlotRenderer.h"

#include "ContourPlottable.h"
#include "qcustomplot.h"

#include <algorithm>
#include <cmath>

namespace gp {

namespace {

const QColor kInk(0x0b, 0x0b, 0x0b);
const QColor kInkSecondary(0x52, 0x51, 0x4e);
const QColor kGrid(0xe4, 0xe3, 0xde);
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

bool isSorted(const QVector<double>& v)
{
    for (qsizetype i = 1; i < v.size(); ++i)
        if (v[i] < v[i - 1]) return false;
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
    : QObject(parent), m_model(model), m_plot(plot)
{
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
    connect(m_model, &PlotModel::axesInfoChanged, this, [this] {
        applyAxesInfo();
        replot();
    });

    connect(m_plot->xAxis, qOverload<const QCPRange&>(&QCPAxis::rangeChanged), this,
            [this] { emit viewChanged(); });
    connect(m_plot->yAxis, qOverload<const QCPRange&>(&QCPAxis::rangeChanged), this,
            [this] { emit viewChanged(); });

    rebuildAll();
}

PlotRenderer::~PlotRenderer() = default;

void PlotRenderer::setupAppearance()
{
    QCustomPlot* p = m_plot;
    p->setBackground(Qt::white);
    p->setAntialiasedElements(QCP::aePlottables | QCP::aeItems | QCP::aeLegendItems);
    p->setPlottingHints(QCP::phCacheLabels | QCP::phImmediateRefresh);
    p->setInteractions({});  // all interaction is handled by InteractionController
    p->axisRect()->setupFullAxesBox(true);  // closed frame, ticks on all sides
    p->axisRect()->setBackground(Qt::white);

    QFont tickFont = p->font();
    tickFont.setPointSizeF(9);
    QFont labelFont = p->font();
    labelFont.setPointSizeF(10);

    for (QCPAxis* axis : p->axisRect()->axes()) {
        QPen axisPen(kInkSecondary, 1);
        axisPen.setCosmetic(true);
        axis->setBasePen(axisPen);
        axis->setTickPen(axisPen);
        axis->setSubTickPen(axisPen);
        axis->setTickLength(5, 0);  // inward ticks, typical for scientific plots
        axis->setSubTickLength(3, 0);
        axis->setTickLabelFont(tickFont);
        axis->setTickLabelColor(kInkSecondary);
        axis->setLabelFont(labelFont);
        axis->setLabelColor(kInk);
        QPen gridPen(kGrid, 1);
        gridPen.setCosmetic(true);
        axis->grid()->setPen(gridPen);
        axis->grid()->setZeroLinePen(Qt::NoPen);
        axis->grid()->setSubGridVisible(false);
    }
    p->xAxis2->grid()->setVisible(false);
    p->yAxis2->grid()->setVisible(false);

    p->legend->setVisible(true);
    p->legend->setBrush(QColor(255, 255, 255, 230));
    p->legend->setBorderPen(QPen(kGrid));
    QFont legendFont = p->font();
    legendFont.setPointSizeF(9);
    p->legend->setFont(legendFont);
    p->legend->setTextColor(kInk);
    p->legend->setIconSize(24, 10);
    p->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignRight);
    p->legend->setSelectableParts(QCPLegend::spNone);

    // Dedicated layers: the region is drawn below the curves, markers and
    // annotations above them. Both are buffered so that dragging an object
    // only repaints that layer, independent of how much data is plotted.
    // Colour-map images sit directly above the grid, below everything else.
    p->addLayer("images", p->layer("grid"), QCustomPlot::limAbove);
    m_imageLayer = p->layer("images");
    p->addLayer("regions", m_imageLayer, QCustomPlot::limAbove);
    p->addLayer("objects", p->layer("main"), QCustomPlot::limAbove);
    m_regionLayer = p->layer("regions");
    m_objectLayer = p->layer("objects");
    m_regionLayer->setMode(QCPLayer::lmBuffered);
    m_objectLayer->setMode(QCPLayer::lmBuffered);

    // Region visual (hidden until a region is selected).
    auto makeRegionLine = [&]() {
        auto* l = new QCPItemStraightLine(p);
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
    m_region.rect->setLayer(m_regionLayer);
    m_region.rect->setPen(Qt::NoPen);
    m_region.rect->setBrush(kRegionFill);
    m_region.rect->setSelectable(false);
    m_region.rect->topLeft->setTypeY(QCPItemPosition::ptAxisRectRatio);
    m_region.rect->bottomRight->setTypeY(QCPItemPosition::ptAxisRectRatio);
    m_region.rect->setVisible(false);
    m_region.minEdge = makeRegionLine();
    m_region.maxEdge = makeRegionLine();
    m_region.label = new QCPItemText(p);
    m_region.label->setLayer(m_objectLayer);
    m_region.label->position->setTypeY(QCPItemPosition::ptAxisRectRatio);
    m_region.label->setPositionAlignment(Qt::AlignBottom | Qt::AlignHCenter);
    m_region.label->setColor(kInk);
    m_region.label->setPadding(QMargins(4, 2, 4, 2));
    m_region.label->setBrush(QColor(255, 255, 255, 220));
    m_region.label->setPen(QPen(kRegionEdge));
    m_region.label->setFont(tickFont);
    m_region.label->setClipToAxisRect(true);
    m_region.label->setVisible(false);
}

void PlotRenderer::rebuildAll()
{
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
    const QCPRange x = m_plot->xAxis->range();
    const QCPRange y = m_plot->yAxis->range();
    return {x.lower, x.upper, y.lower, y.upper};
}

void PlotRenderer::setViewRange(const ViewRange& r)
{
    m_plot->xAxis->setRange(r.xmin, r.xmax);
    m_plot->yAxis->setRange(r.ymin, r.ymax);
    // keep the mirrored frame axes in sync (tick positions)
    m_plot->xAxis2->setRange(m_plot->xAxis->range());
    m_plot->yAxis2->setRange(m_plot->yAxis->range());
    replot();
}

void PlotRenderer::autoscale(bool doX, bool doY)
{
    double xmin, xmax, ymin, ymax;
    if (!m_model->dataBounds(xmin, xmax, ymin, ymax)) return;
    ViewRange r = viewRange();
    if (doX) {
        padRange(xmin, xmax, 0.0);  // x usually spans the data exactly
        r.xmin = xmin;
        r.xmax = xmax;
    }
    if (doY) {
        // Curves get headroom; a pure 2D (contour) view is fitted exactly.
        bool anyCurve = false;
        for (const auto& d : m_model->datasets()) anyCurve |= d.style.visible && !d.x.isEmpty();
        padRange(ymin, ymax, anyCurve ? 0.05 : 0.0);
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
    if (m_hasHome) return;
    autoscale();
    captureHomeView();
}

// ---- coordinates -----------------------------------------------------------

QPointF PlotRenderer::pixelToData(const QPointF& px) const
{
    return {m_plot->xAxis->pixelToCoord(px.x()), m_plot->yAxis->pixelToCoord(px.y())};
}

QPointF PlotRenderer::dataToPixel(const QPointF& d) const
{
    return {m_plot->xAxis->coordToPixel(d.x()), m_plot->yAxis->coordToPixel(d.y())};
}

bool PlotRenderer::isInsidePlotArea(const QPointF& px) const
{
    return m_plot->axisRect()->rect().contains(px.toPoint());
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
    if (!isInsidePlotArea(px)) return {};
    const auto& markers = m_model->markers();
    ObjectRef best;
    double bestDist = tol;
    for (auto it = markers.rbegin(); it != markers.rend(); ++it) {
        const MarkerVisual v = m_markers.value(it->id);
        if (!v.line) continue;
        double d;
        if (it->orientation == Orientation::Vertical)
            d = std::abs(px.x() - m_plot->xAxis->coordToPixel(it->position));
        else
            d = std::abs(px.y() - m_plot->yAxis->coordToPixel(it->position));
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
    const double p0 = m_plot->xAxis->coordToPixel(r.xmin);
    const double p1 = m_plot->xAxis->coordToPixel(r.xmax);
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
    replotObjectLayers();
}

void PlotRenderer::setExportMode(bool on)
{
    m_exportMode = on;
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
    syncLegend();
    maybeCaptureHome();
    replot();
}

void PlotRenderer::onDatasetRemoved(ObjectId id)
{
    if (auto* p = m_plottables.take(id)) m_plot->removePlottable(p);
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
        m_plot->removePlottable(p);
        p = nullptr;
    }
    if (!p) {
        if (needGraph) p = new QCPGraph(m_plot->xAxis, m_plot->yAxis);
        else p = new QCPCurve(m_plot->xAxis, m_plot->yAxis);
        p->setSelectable(QCP::stNone);
        m_plottables.insert(id, p);
        applyDatasetAppearance(id);
        syncLegend();
    }
    if (auto* g = qobject_cast<QCPGraph*>(p)) g->setData(d->x, d->y, true);
    else if (auto* c = qobject_cast<QCPCurve*>(p)) c->setData(d->x, d->y);
}

void PlotRenderer::applyDatasetAppearance(ObjectId id)
{
    const Dataset* d = m_model->dataset(id);
    QCPAbstractPlottable* p = m_plottables.value(id);
    if (!d || !p) return;
    const DatasetStyle& s = d->style;
    QPen pen(s.color, s.lineWidth, toPenStyle(s.lineStyle), Qt::RoundCap, Qt::RoundJoin);
    p->setPen(pen);
    p->setName(d->name);
    p->setVisible(s.visible);
    const QCPScatterStyle scatter(toScatterShape(s.scatter), s.color, Qt::white, s.scatterSize);
    if (auto* g = qobject_cast<QCPGraph*>(p)) {
        g->setLineStyle(s.lineStyle == LineStyle::None ? QCPGraph::lsNone : QCPGraph::lsLine);
        g->setScatterStyle(scatter);
    } else if (auto* c = qobject_cast<QCPCurve*>(p)) {
        c->setLineStyle(s.lineStyle == LineStyle::None ? QCPCurve::lsNone : QCPCurve::lsLine);
        c->setScatterStyle(scatter);
    }
}

void PlotRenderer::syncLegend()
{
    // Legend order follows dataset order in the model; hidden or unnamed
    // datasets are omitted.
    m_plot->legend->clearItems();
    for (const auto& d : m_model->datasets()) {
        QCPAbstractPlottable* p = m_plottables.value(d.id);
        if (p && d.style.visible && !d.name.isEmpty()) p->addToLegend();
    }
    for (const auto& m : m_model->matrices()) {
        const MatrixVisual v = m_matrices.value(m.id);
        if (v.contour && m.style.visible && !m.name.isEmpty()) v.contour->addToLegend();
    }
    m_plot->legend->setVisible(m_model->axesInfo().legendVisible &&
                               m_plot->legend->itemCount() > 0);
}

// ---- 2D data ---------------------------------------------------------------------------

ContourPlottable* PlotRenderer::contourPlottable(ObjectId id) const
{
    return m_matrices.value(id).contour;
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
    v.contour = new ContourPlottable(m_plot->xAxis, m_plot->yAxis);
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
    v.contour->setContourStyle(m->style);  // recomputes only if the levels changed
    v.contour->setName(m->name);
    v.contour->setVisible(m->style.visible);
    updateMatrixImage(id);
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
        v.image = new QCPColorMap(m_plot->xAxis, m_plot->yAxis);
        v.image->setLayer(m_imageLayer);
        v.image->setSelectable(QCP::stNone);
        v.image->setInterpolate(true);
        v.image->setTightBoundary(false);
    }
    auto* data = new QCPColorMapData(m->nx, m->ny, QCPRange(m->x0, m->x1), QCPRange(m->y0, m->y1));
    double lo = 0, hi = 0;
    for (int iy = 0; iy < m->ny; ++iy)
        for (int ix = 0; ix < m->nx; ++ix) {
            const double z = m->z[qsizetype(iy) * m->nx + ix];
            data->setCell(ix, iy, z);
            if (std::isfinite(z)) lo = std::min(lo, z), hi = std::max(hi, z);
        }
    v.image->setData(data, false);
    // Signed data: diverging blue-white-red around zero; otherwise white->blue.
    QCPColorGradient g;
    g.clearColorStops();
    if (lo < 0 && hi > 0) {
        const double a = std::max(-lo, hi);
        g.setColorStopAt(0.0, QColor(0x2a, 0x78, 0xd6));
        g.setColorStopAt(0.5, Qt::white);
        g.setColorStopAt(1.0, QColor(0xe3, 0x49, 0x48));
        v.image->setDataRange(QCPRange(-a, a));
    } else {
        g.setColorStopAt(0.0, Qt::white);
        g.setColorStopAt(1.0, QColor(0x2a, 0x78, 0xd6));
        v.image->setDataRange(QCPRange(lo, hi > lo ? hi : lo + 1));
    }
    v.image->setGradient(g);
    v.image->setVisible(m->style.visible);
}

// ---- markers -------------------------------------------------------------------------

void PlotRenderer::onMarkerAdded(ObjectId id)
{
    if (m_markers.contains(id)) return;
    MarkerVisual v;
    v.line = new QCPItemStraightLine(m_plot);
    v.line->setLayer(m_objectLayer);
    v.line->setSelectable(false);
    v.label = new QCPItemText(m_plot);
    v.label->setLayer(m_objectLayer);
    v.label->setSelectable(false);
    v.label->setPadding(QMargins(4, 2, 4, 2));
    v.label->setBrush(QColor(255, 255, 255, 225));
    v.label->setColor(kInk);
    QFont f = m_plot->font();
    f.setPointSizeF(9);
    v.label->setFont(f);
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

    QPen pen(m->color, m->lineWidth + (hl ? 1.0 : 0.0), toPenStyle(m->lineStyle));
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
    v.label->setText(text);
    v.label->setVisible(!text.isEmpty());
    QPen border(m->color, hl ? 1.5 : 1.0);
    v.label->setPen(border);
    v.label->setTextAlignment(Qt::AlignLeft);
}

// ---- annotations -------------------------------------------------------------------------

void PlotRenderer::onAnnotationAdded(ObjectId id)
{
    if (m_annotations.contains(id)) return;
    auto* t = new QCPItemText(m_plot);
    t->setLayer(m_objectLayer);
    t->setSelectable(false);
    t->position->setType(QCPItemPosition::ptPlotCoords);
    t->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
    t->setTextAlignment(Qt::AlignLeft);
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
    QCPItemText* t = m_annotations.value(id);
    if (!a || !t) return;
    const bool hl = !m_exportMode && m_highlight == ObjectRef{ObjectKind::Annotation, id};
    t->position->setCoords(a->x, a->y);
    t->setText(a->text.isEmpty() ? QStringLiteral(" ") : a->text);
    t->setColor(a->color);
    QFont f = m_plot->font();
    f.setPointSizeF(std::max(1.0, a->fontSize));
    t->setFont(f);
    if (hl) {
        QPen pen(kHighlight, 1, Qt::DashLine);
        t->setPen(pen);
    } else {
        t->setPen(Qt::NoPen);
    }
}

// ---- region / axes ---------------------------------------------------------------------------

void PlotRenderer::applyRegion()
{
    const Region& r = m_model->region();
    const bool on = r.active;
    m_region.rect->setVisible(on);
    m_region.minEdge->setVisible(on);
    m_region.maxEdge->setVisible(on);
    m_region.label->setVisible(on);
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

void PlotRenderer::applyAxesInfo()
{
    const AxesInfo& info = m_model->axesInfo();
    m_plot->xAxis->setLabel(info.xLabel);
    m_plot->yAxis->setLabel(info.yLabel);
    m_plot->xAxis->grid()->setVisible(info.gridVisible);
    m_plot->yAxis->grid()->setVisible(info.gridVisible);

    if (!info.title.isEmpty()) {
        if (!m_title) {
            m_plot->plotLayout()->insertRow(0);
            m_title = new QCPTextElement(m_plot, info.title);
            QFont f = m_plot->font();
            f.setPointSizeF(11);
            f.setBold(true);
            m_title->setFont(f);
            m_title->setTextColor(kInk);
            m_plot->plotLayout()->addElement(0, 0, m_title);
        }
        m_title->setText(info.title);
    } else if (m_title) {
        m_plot->plotLayout()->remove(m_title);
        m_plot->plotLayout()->simplify();
        m_title = nullptr;
    }
    syncLegend();
}

}  // namespace gp
