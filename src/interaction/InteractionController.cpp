// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "InteractionController.h"

#include "commands/PlotEditor.h"
#include "model/PeakPicker.h"
#include "render/PeakOverlay.h"
#include "qcustomplot.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QRubberBand>
#include <QWheelEvent>
#include <cmath>

namespace gp {

namespace {
constexpr int kDragThresholdPx = 4;
constexpr double kWheelZoomPerStep = 0.85;  // range factor per 15° wheel step
constexpr double kLevelStepPerNotch = 1.2;  // contour base level factor per wheel step
}  // namespace

InteractionController::InteractionController(PlotRenderer* renderer, PlotEditor* editor,
                                             QObject* parent)
    : QObject(parent), m_renderer(renderer), m_editor(editor), m_plot(renderer->plot())
{
    m_plot->installEventFilter(this);
    m_plot->setMouseTracking(true);
    m_plot->setFocusPolicy(Qt::StrongFocus);
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, m_plot);

    addTarget(renderer, editor);
}

void InteractionController::addTarget(PlotRenderer* renderer, PlotEditor* editor)
{
    m_targets.push_back({renderer, editor});
    // Keep the selection valid when objects disappear (undo, Python, ...).
    PlotModel* model = editor->model();
    auto drop = [this, editor](ObjectKind kind) {
        return [this, editor, kind](ObjectId id) {
            if (m_editor == editor && m_selection == ObjectRef{kind, id}) setSelection({});
        };
    };
    connect(model, &PlotModel::markerRemoved, this, drop(ObjectKind::Marker));
    connect(model, &PlotModel::annotationRemoved, this, drop(ObjectKind::Annotation));
    connect(model, &PlotModel::datasetRemoved, this, drop(ObjectKind::Dataset));
    connect(model, &PlotModel::peakRemoved, this, drop(ObjectKind::Peak));
    connect(model, &PlotModel::matrixRemoved, this, drop(ObjectKind::Matrix));
    connect(model, &PlotModel::regionChanged, this, [this, editor, model] {
        if (m_editor == editor && m_selection.kind == ObjectKind::Region && !model->region().active)
            setSelection({});
    });
}

void InteractionController::removeTarget(PlotEditor* editor)
{
    for (size_t i = 0; i < m_targets.size(); ++i) {
        if (m_targets[i].second != editor) continue;
        disconnect(editor->model(), nullptr, this, nullptr);
        m_targets.erase(m_targets.begin() + long(i));
        if (m_editor == editor && !m_targets.empty()) setActiveTarget(0);
        return;
    }
}

void InteractionController::setActiveTarget(int index)
{
    if (index < 0 || index >= int(m_targets.size())) return;
    if (m_targets[index].first == m_renderer) return;
    setSelection({});
    m_renderer = m_targets[index].first;
    m_editor = m_targets[index].second;
    emit activeTargetChanged(index);
}

int InteractionController::activeTarget() const
{
    for (size_t i = 0; i < m_targets.size(); ++i)
        if (m_targets[i].first == m_renderer) return int(i);
    return -1;
}

int InteractionController::targetAt(const QPointF& px) const
{
    for (size_t i = 0; i < m_targets.size(); ++i) {
        PlotRenderer* r = m_targets[i].first;
        // Hidden panels (e.g. hidden timeline rows) keep stale geometry: skip them.
        if (r->panelVisible() && r->axisRect()->outerRect().contains(px.toPoint())) return int(i);
    }
    return -1;
}

void InteractionController::setMode(InteractionMode mode)
{
    // Navigation modes and the (sticky) peak tool are restored after one-shot tools.
    if (mode == InteractionMode::Zoom || mode == InteractionMode::Pan ||
        mode == InteractionMode::Region || mode == InteractionMode::AddPeak)
        m_navMode = mode;
    if (mode == m_mode) return;
    m_mode = mode;
    switch (mode) {
    case InteractionMode::Pan: m_plot->setCursor(Qt::OpenHandCursor); break;
    case InteractionMode::Region: m_plot->setCursor(Qt::IBeamCursor); break;
    case InteractionMode::Zoom: m_plot->setCursor(Qt::CrossCursor); break;
    default: m_plot->setCursor(Qt::CrossCursor); break;
    }
    emit modeChanged(mode);
}

void InteractionController::finishOneShotMode()
{
    if (m_mode == InteractionMode::AddVMarker || m_mode == InteractionMode::AddHMarker ||
        m_mode == InteractionMode::AddAnnotation)
        setMode(m_navMode);
}

void InteractionController::setSelection(const ObjectRef& ref)
{
    if (ref == m_selection) return;
    m_selection = ref;
    m_renderer->setHighlight(ref);
    emit selectionChanged(ref);
}

void InteractionController::setWheelAdjustsLevels(bool on)
{
    if (on == m_wheelLevels) return;
    m_wheelLevels = on;
    emit wheelAdjustsLevelsChanged(on);
}

void InteractionController::scaleContourLevels(double factor)
{
    m_editor->scaleContourLevels(m_renderer->levelTargets(), factor);
}

void InteractionController::deleteSelection()
{
    if (!m_selection.isValid()) return;
    const ObjectRef ref = m_selection;
    setSelection({});
    m_editor->removeObject(ref);
}

bool InteractionController::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_plot) return QObject::eventFilter(watched, event);
    switch (event->type()) {
    case QEvent::MouseButtonPress: mousePress(static_cast<QMouseEvent*>(event)); return true;
    case QEvent::MouseMove: mouseMove(static_cast<QMouseEvent*>(event)); return true;
    case QEvent::MouseButtonRelease: mouseRelease(static_cast<QMouseEvent*>(event)); return true;
    case QEvent::MouseButtonDblClick:
        mouseDoubleClick(static_cast<QMouseEvent*>(event));
        return true;
    case QEvent::Wheel: wheel(static_cast<QWheelEvent*>(event)); return true;
    case QEvent::KeyPress: return key(static_cast<QKeyEvent*>(event));
    case QEvent::Leave: emit cursorMoved({}, false); return false;
    case QEvent::FocusOut:  // e.g. a dialog popped up mid-drag
        if (m_drag != Drag::None) cancelDrag();
        return false;
    default: return QObject::eventFilter(watched, event);
    }
}

// ---- mouse ------------------------------------------------------------------

void InteractionController::mousePress(QMouseEvent* e)
{
    m_plot->setFocus(Qt::MouseFocusReason);
    // A second button while dragging (or a lost release) cancels the drag
    // instead of leaving an uncommitted preview behind.
    if (m_drag != Drag::None) {
        cancelDrag();
        return;
    }
    const QPointF px = e->position();
    // Multi-panel figures: the panel under the cursor becomes active.
    if (const int t = targetAt(px); t >= 0) setActiveTarget(t);
    m_pressPx = px.toPoint();
    m_pressData = m_renderer->pixelToData(px);
    m_pressView = m_renderer->viewRange();
    m_dragMoved = false;
    m_drag = Drag::None;
    const bool inside = m_renderer->isInsidePlotArea(px);
    if (inside && e->button() == Qt::LeftButton) emit clicked(m_pressData);  // measuring reference

    if (e->button() == Qt::MiddleButton) {
        m_drag = Drag::Pan;
        m_plot->setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() == Qt::RightButton) {
        const ObjectRef hit = m_renderer->hitTestObject(px);
        ObjectRef target = hit;
        if (!hit.isValid() && m_renderer->hitTestRegion(px) != RegionHit::None)
            target = {ObjectKind::Region, kInvalidId};
        if (target.isValid()) setSelection(target);
        emit contextMenuRequested(target, e->globalPosition().toPoint(), m_pressData);
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    // 1) Placing new objects (one-shot modes).
    PlotModel* model = m_editor->model();
    if (inside && m_mode == InteractionMode::AddVMarker) {
        Marker m;
        m.orientation = Orientation::Vertical;
        m.position = m_pressData.x();
        m.color = paletteColor(7 + int(model->markers().size()));
        m.label = QString("M%1").arg(model->markers().size() + 1);
        setSelection({ObjectKind::Marker, m_editor->addMarker(m)});
        finishOneShotMode();
        return;
    }
    if (inside && m_mode == InteractionMode::AddHMarker) {
        Marker m;
        m.orientation = Orientation::Horizontal;
        m.position = m_pressData.y();
        m.color = paletteColor(7 + int(model->markers().size()));
        m.label = QString("M%1").arg(model->markers().size() + 1);
        setSelection({ObjectKind::Marker, m_editor->addMarker(m)});
        finishOneShotMode();
        return;
    }
    if (inside && m_mode == InteractionMode::AddAnnotation) {
        Annotation a;
        a.x = m_pressData.x();
        a.y = m_pressData.y();
        a.text = "Annotation";
        const ObjectId id = m_editor->addAnnotation(a);
        setSelection({ObjectKind::Annotation, id});
        finishOneShotMode();
        emit editRequested({ObjectKind::Annotation, id});  // let the user type the text
        return;
    }

    // 2) Grabbing existing objects has priority over navigation.
    const ObjectRef hit = m_renderer->hitTestObject(px);
    if (hit.kind == ObjectKind::Peak) {
        setSelection(hit);
        m_dragStartPeak = *model->peak(hit.id);
        const auto h = m_renderer->peakOverlay()->hitTest(px, 5.0);
        m_drag = (h.id == hit.id && h.onLabel) ? Drag::PeakLabel : Drag::Peak;
        return;
    }
    // Peak tool: click picks the nearest maximum (Shift: exactly at the cursor).
    if (inside && m_mode == InteractionMode::AddPeak) {
        const Matrix* m = m_renderer->matrixAt(m_pressData);
        Peak p;
        p.x = m_pressData.x();
        p.y = m_pressData.y();
        if (m) {
            p.matrixId = m->id;
            p.intensity = sampleMatrix(*m, p.x, p.y);
            if (!(e->modifiers() & Qt::ShiftModifier)) {
                // Search radius: ~10 screen pixels expressed in grid points.
                const double gx = std::abs(m_renderer->xAxis()->coordToPixel(m->x1) -
                                           m_renderer->xAxis()->coordToPixel(m->x0)) / std::max(1, m->nx - 1);
                const double gy = std::abs(m_renderer->yAxis()->coordToPixel(m->y1) -
                                           m_renderer->yAxis()->coordToPixel(m->y0)) / std::max(1, m->ny - 1);
                const int radius = std::clamp(int(std::ceil(10.0 / std::max(1e-9, std::min(gx, gy)))), 1, 50);
                if (auto s = snapToPeak(*m, p.x, p.y, radius)) p = *s;
            }
        }
        const ObjectRef ref{ObjectKind::Peak, m_editor->addPeak(p)};
        setSelection(ref);
        return;
    }
    if (hit.kind == ObjectKind::Marker) {
        setSelection(hit);
        m_dragStartMarker = *model->marker(hit.id);
        m_drag = Drag::Marker;
        return;
    }
    if (hit.kind == ObjectKind::Annotation) {
        setSelection(hit);
        m_dragStartAnnotation = *model->annotation(hit.id);
        m_drag = Drag::Annotation;
        return;
    }

    // 3) Region edges can be dragged in every mode; the body only in region mode.
    m_dragStartRegion = model->region();
    const RegionHit rh = m_renderer->hitTestRegion(px);
    if (rh == RegionHit::MinEdge || rh == RegionHit::MaxEdge) {
        setSelection({ObjectKind::Region, kInvalidId});
        m_drag = (rh == RegionHit::MinEdge) ? Drag::RegionMin : Drag::RegionMax;
        return;
    }

    if (!inside) {
        setSelection({});
        return;
    }

    switch (m_mode) {
    case InteractionMode::Pan:
        setSelection({});
        m_drag = Drag::Pan;
        m_plot->setCursor(Qt::ClosedHandCursor);
        break;
    case InteractionMode::Region:
        if (rh == RegionHit::Inside && (e->modifiers() & Qt::ShiftModifier)) {
            setSelection({ObjectKind::Region, kInvalidId});
            m_drag = Drag::RegionMove;
        } else {
            m_drag = Drag::RegionCreate;
        }
        break;
    case InteractionMode::Zoom:
    default:
        setSelection(rh == RegionHit::Inside ? ObjectRef{ObjectKind::Region, kInvalidId}
                                             : ObjectRef{});
        m_drag = Drag::RubberBand;
        break;
    }
}

void InteractionController::mouseMove(QMouseEvent* e)
{
    const QPointF px = e->position();
    {
        const int t = targetAt(px);
        PlotRenderer* r = (m_drag == Drag::None && t >= 0) ? m_targets[t].first : m_renderer;
        emit cursorMoved(r->pixelToData(px), r->isInsidePlotArea(px));
    }
    const QPointF data = m_renderer->pixelToData(px);

    if (m_drag == Drag::None) {
        updateHoverCursor(px);
        return;
    }
    if (!m_dragMoved && (px.toPoint() - m_pressPx).manhattanLength() < kDragThresholdPx) return;
    m_dragMoved = true;
    PlotModel* model = m_editor->model();

    switch (m_drag) {
    case Drag::Pan: {
        // Shift by the pixel delta expressed in the *press-time* view, so the
        // data point under the cursor stays under the cursor.
        QCPAxis* xa = m_renderer->xAxis();
        QCPAxis* ya = m_renderer->yAxis();
        const ViewRange v = m_pressView;
        const QRect ar = m_renderer->axisRect()->rect();
        double dxPx = px.x() - m_pressPx.x();
        double dyPx = px.y() - m_pressPx.y();
        ViewRange nv = v;
        auto logShift = [](double& lo, double& hi, double fraction) {
            const double f = std::pow(hi / lo, fraction);
            lo *= f;
            hi *= f;
        };
        if (m_zoomAxes == ZoomAxes::XOnly) dyPx = 0;  // e.g. timeline: time only
        if (m_zoomAxes == ZoomAxes::YOnly) dxPx = 0;
        if (xa->scaleType() == QCPAxis::stLogarithmic)
            logShift(nv.xmin, nv.xmax, -dxPx / ar.width() * (xa->rangeReversed() ? -1 : 1));
        if (ya->scaleType() == QCPAxis::stLogarithmic)
            logShift(nv.ymin, nv.ymax, dyPx / ar.height() * (ya->rangeReversed() ? -1 : 1));
        if (xa->scaleType() == QCPAxis::stLinear) {
            const double dx = dxPx / ar.width() * (v.xmax - v.xmin) * (xa->rangeReversed() ? -1 : 1);
            nv.xmin -= dx;
            nv.xmax -= dx;
        }
        if (ya->scaleType() == QCPAxis::stLinear) {
            const double dy = dyPx / ar.height() * (v.ymax - v.ymin) * (ya->rangeReversed() ? -1 : 1);
            nv.ymin += dy;
            nv.ymax += dy;
        }
        m_renderer->setViewRange(nv);
        break;
    }
    case Drag::RubberBand: {
        QRect r = QRect(m_pressPx, px.toPoint()).normalized();
        const QRect ar = m_renderer->axisRect()->rect();
        if (m_zoomAxes == ZoomAxes::XOnly) r.setTop(ar.top()), r.setBottom(ar.bottom());
        if (m_zoomAxes == ZoomAxes::YOnly) r.setLeft(ar.left()), r.setRight(ar.right());
        m_rubberBand->setGeometry(r.intersected(ar));
        m_rubberBand->show();
        break;
    }
    case Drag::RegionCreate: {
        Region r{true, m_pressData.x(), data.x()};
        m_editor->previewRegion(r);
        break;
    }
    case Drag::RegionMin:
    case Drag::RegionMax: {
        Region r = m_dragStartRegion;
        (m_drag == Drag::RegionMin ? r.xmin : r.xmax) = data.x();
        m_editor->previewRegion(r);
        break;
    }
    case Drag::RegionMove: {
        Region r = m_dragStartRegion;
        const double dx = data.x() - m_pressData.x();
        r.xmin += dx;
        r.xmax += dx;
        m_editor->previewRegion(r);
        break;
    }
    case Drag::Marker: {
        Marker m = m_dragStartMarker;
        if (m.orientation == Orientation::Vertical) m.position += data.x() - m_pressData.x();
        else m.position += data.y() - m_pressData.y();
        if (model->marker(m.id)) m_editor->previewMarker(m);
        break;
    }
    case Drag::Peak: {
        Peak p = m_dragStartPeak;
        p.x += data.x() - m_pressData.x();
        p.y += data.y() - m_pressData.y();
        if (const Matrix* m = m_renderer->matrixAt({p.x, p.y})) p.intensity = sampleMatrix(*m, p.x, p.y);
        if (model->peak(p.id)) m_editor->previewPeak(p);
        break;
    }
    case Drag::PeakLabel: {
        Peak p = m_dragStartPeak;
        p.labelOffset += px - QPointF(m_pressPx);
        if (model->peak(p.id)) m_editor->previewPeak(p);
        break;
    }
    case Drag::Annotation: {
        Annotation a = m_dragStartAnnotation;
        a.x += data.x() - m_pressData.x();
        a.y += data.y() - m_pressData.y();
        if (model->annotation(a.id)) m_editor->previewAnnotation(a);
        break;
    }
    case Drag::None: break;
    }
}

void InteractionController::mouseRelease(QMouseEvent* e)
{
    if (m_drag != Drag::None && e->buttons() != Qt::NoButton) return;  // another button still held
    Q_UNUSED(e);
    const Drag drag = m_drag;
    m_drag = Drag::None;
    PlotModel* model = m_editor->model();

    switch (drag) {
    case Drag::Pan:
        m_plot->setCursor(m_mode == InteractionMode::Pan ? Qt::OpenHandCursor : Qt::CrossCursor);
        break;
    case Drag::RubberBand: {
        m_rubberBand->hide();
        if (!m_dragMoved) break;
        const QRect r = m_rubberBand->geometry();
        if (r.width() < kDragThresholdPx && r.height() < kDragThresholdPx) break;
        const QPointF a = m_renderer->pixelToData(r.topLeft());
        const QPointF b = m_renderer->pixelToData(r.bottomRight());
        ViewRange v = m_renderer->viewRange();
        if (m_zoomAxes != ZoomAxes::YOnly) v.xmin = std::min(a.x(), b.x()), v.xmax = std::max(a.x(), b.x());
        if (m_zoomAxes != ZoomAxes::XOnly) v.ymin = std::min(a.y(), b.y()), v.ymax = std::max(a.y(), b.y());
        m_renderer->setViewRange(v);
        break;
    }
    case Drag::RegionCreate:
        if (m_dragMoved) {
            setSelection({ObjectKind::Region, kInvalidId});
            m_editor->setRegion(model->region(), m_dragStartRegion);
        } else {
            // A simple click in region mode keeps the existing selection.
            m_editor->previewRegion(m_dragStartRegion);
        }
        break;
    case Drag::RegionMin:
    case Drag::RegionMax:
    case Drag::RegionMove:
        if (m_dragMoved) m_editor->setRegion(model->region(), m_dragStartRegion);
        break;
    case Drag::Marker:
        if (m_dragMoved) {
            if (const Marker* m = model->marker(m_dragStartMarker.id))
                m_editor->updateMarker(*m, m_dragStartMarker, false, "Move marker");
        }
        break;
    case Drag::Peak:
    case Drag::PeakLabel:
        if (m_dragMoved) {
            if (const Peak* p = model->peak(m_dragStartPeak.id))
                m_editor->updatePeak(*p, m_dragStartPeak, false,
                                     drag == Drag::Peak ? "Move peak" : "Move peak label");
        }
        break;
    case Drag::Annotation:
        if (m_dragMoved) {
            if (const Annotation* a = model->annotation(m_dragStartAnnotation.id))
                m_editor->updateAnnotation(*a, m_dragStartAnnotation, false, "Move annotation");
        }
        break;
    case Drag::None: break;
    }
    updateHoverCursor(e->position());
}

void InteractionController::mouseDoubleClick(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    const ObjectRef hit = m_renderer->hitTestObject(e->position());
    if (hit.isValid()) {
        setSelection(hit);
        emit editRequested(hit);
        return;
    }
    // Double-click on empty plot area: fit the data (common in MATLAB/Origin).
    if (m_renderer->isInsidePlotArea(e->position())) m_renderer->autoscale();
}

void InteractionController::wheel(QWheelEvent* e)
{
    const QPointF px = e->position();
    // Zoom the panel under the mouse without making it current (switching the
    // current panel rebuilds the side panels and clears the selection).
    const int t = targetAt(px);
    PlotRenderer* r = t >= 0 ? m_targets[size_t(t)].first : m_renderer;
    if (!r->axisRect()->outerRect().contains(px.toPoint())) return;
    // Some platforms turn Alt+wheel into a horizontal wheel; accept both.
    const int delta = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
    const double steps = delta / 120.0;
    if (steps == 0) return;

    const bool levels = m_wheelLevels || (e->modifiers() & Qt::AltModifier);
    if (levels && !r->levelTargets().isEmpty()) {
        m_targets[size_t(std::max(t, 0))].second->scaleContourLevels(r->levelTargets(),
                                                                    std::pow(kLevelStepPerNotch, -steps));
        return;
    }
    const double factor = std::pow(kWheelZoomPerStep, steps);
    ZoomAxes axes = m_zoomAxes;
    if (e->modifiers() & Qt::ControlModifier) axes = ZoomAxes::XOnly;
    if (e->modifiers() & Qt::ShiftModifier) axes = ZoomAxes::YOnly;
    // Wheel over an axis zooms only that axis.
    const QRect ar = r->axisRect()->rect();
    if (px.y() > ar.bottom()) axes = ZoomAxes::XOnly;
    else if (px.x() < ar.left()) axes = ZoomAxes::YOnly;
    if (axes != ZoomAxes::YOnly) r->xAxis()->scaleRange(factor, r->xAxis()->pixelToCoord(px.x()));
    if (axes != ZoomAxes::XOnly) r->yAxis()->scaleRange(factor, r->yAxis()->pixelToCoord(px.y()));
    r->replot();
}

void InteractionController::cancelDrag()
{
    // Restore the state captured at press time; nothing enters the history.
    PlotModel* model = m_editor->model();
    switch (m_drag) {
    case Drag::Marker:
        if (model->marker(m_dragStartMarker.id)) m_editor->previewMarker(m_dragStartMarker);
        break;
    case Drag::Peak:
    case Drag::PeakLabel:
        if (model->peak(m_dragStartPeak.id)) m_editor->previewPeak(m_dragStartPeak);
        break;
    case Drag::Annotation:
        if (model->annotation(m_dragStartAnnotation.id)) m_editor->previewAnnotation(m_dragStartAnnotation);
        break;
    case Drag::RegionCreate:
    case Drag::RegionMin:
    case Drag::RegionMax:
    case Drag::RegionMove: m_editor->previewRegion(m_dragStartRegion); break;
    case Drag::Pan: m_renderer->setViewRange(m_pressView); break;
    case Drag::RubberBand:
    case Drag::None: break;
    }
    m_rubberBand->hide();
    m_drag = Drag::None;
}

bool InteractionController::key(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        deleteSelection();
        return true;
    case Qt::Key_Asterisk:  // TopSpin-like "*2": intensity x2 = levels / 2
        if (m_renderer->levelTargets().isEmpty()) return false;
        scaleContourLevels(0.5);
        return true;
    case Qt::Key_Slash:     // "/2": intensity / 2 = levels x 2
        if (m_renderer->levelTargets().isEmpty()) return false;
        scaleContourLevels(2.0);
        return true;
    case Qt::Key_Escape:
        if (m_drag != Drag::None) {
            cancelDrag();
        } else if (m_mode != m_navMode) {
            setMode(m_navMode);
        } else if (m_mode == InteractionMode::AddPeak) {
            setMode(InteractionMode::Zoom);  // leave the sticky peak tool
        } else {
            setSelection({});
        }
        return true;
    default: return false;
    }
}

void InteractionController::updateHoverCursor(const QPointF& px)
{
    const int t = targetAt(px);
    PlotRenderer* r = t >= 0 ? m_targets[t].first : m_renderer;
    const PlotModel* hoverModel = t >= 0 ? m_targets[t].second->model() : m_editor->model();
    if (m_mode == InteractionMode::AddVMarker || m_mode == InteractionMode::AddHMarker ||
        m_mode == InteractionMode::AddAnnotation) {
        m_plot->setCursor(Qt::CrossCursor);
        return;
    }
    if (m_mode == InteractionMode::AddPeak && !r->hitTestObject(px).isValid()) {
        m_plot->setCursor(Qt::CrossCursor);
        return;
    }
    const ObjectRef hit = r->hitTestObject(px);
    if (hit.kind == ObjectKind::Marker) {
        const Marker* m = hoverModel->marker(hit.id);
        m_plot->setCursor(m && m->orientation == Orientation::Vertical ? Qt::SizeHorCursor
                                                                         : Qt::SizeVerCursor);
        return;
    }
    if (hit.kind == ObjectKind::Annotation || hit.kind == ObjectKind::Peak) {
        m_plot->setCursor(Qt::SizeAllCursor);
        return;
    }
    const RegionHit rh = r->hitTestRegion(px);
    if (rh == RegionHit::MinEdge || rh == RegionHit::MaxEdge) {
        m_plot->setCursor(Qt::SplitHCursor);
        return;
    }
    switch (m_mode) {
    case InteractionMode::Pan: m_plot->setCursor(Qt::OpenHandCursor); break;
    case InteractionMode::Region: m_plot->setCursor(Qt::IBeamCursor); break;
    default: m_plot->setCursor(Qt::CrossCursor); break;
    }
}

}  // namespace gp
