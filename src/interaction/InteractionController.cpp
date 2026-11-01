#include "InteractionController.h"

#include "commands/PlotEditor.h"
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

    // Keep the selection valid when objects disappear (undo, Python, ...).
    PlotModel* model = editor->model();
    connect(model, &PlotModel::markerRemoved, this, [this](ObjectId id) {
        if (m_selection == ObjectRef{ObjectKind::Marker, id}) setSelection({});
    });
    connect(model, &PlotModel::annotationRemoved, this, [this](ObjectId id) {
        if (m_selection == ObjectRef{ObjectKind::Annotation, id}) setSelection({});
    });
    connect(model, &PlotModel::datasetRemoved, this, [this](ObjectId id) {
        if (m_selection == ObjectRef{ObjectKind::Dataset, id}) setSelection({});
    });
    connect(model, &PlotModel::matrixRemoved, this, [this](ObjectId id) {
        if (m_selection == ObjectRef{ObjectKind::Matrix, id}) setSelection({});
    });
    connect(model, &PlotModel::regionChanged, this, [this, model] {
        if (m_selection.kind == ObjectKind::Region && !model->region().active) setSelection({});
    });
}

void InteractionController::setMode(InteractionMode mode)
{
    if (mode == InteractionMode::Zoom || mode == InteractionMode::Pan ||
        mode == InteractionMode::Region)
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
    default: return QObject::eventFilter(watched, event);
    }
}

// ---- mouse ------------------------------------------------------------------

void InteractionController::mousePress(QMouseEvent* e)
{
    m_plot->setFocus(Qt::MouseFocusReason);
    const QPointF px = e->position();
    m_pressPx = px.toPoint();
    m_pressData = m_renderer->pixelToData(px);
    m_pressView = m_renderer->viewRange();
    m_dragMoved = false;
    m_drag = Drag::None;
    const bool inside = m_renderer->isInsidePlotArea(px);

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
    const QPointF data = m_renderer->pixelToData(px);
    emit cursorMoved(data, m_renderer->isInsidePlotArea(px));

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
        QCPAxis* xa = m_plot->xAxis;
        QCPAxis* ya = m_plot->yAxis;
        const ViewRange v = m_pressView;
        const QRect ar = m_plot->axisRect()->rect();
        const double dxPx = px.x() - m_pressPx.x();
        const double dyPx = px.y() - m_pressPx.y();
        ViewRange nv = v;
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
        const QRect ar = m_plot->axisRect()->rect();
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
    if (!m_renderer->isInsidePlotArea(px) && !m_plot->axisRect()->outerRect().contains(px.toPoint()))
        return;
    // Some platforms turn Alt+wheel into a horizontal wheel; accept both.
    const int delta = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
    const double steps = delta / 120.0;
    if (steps == 0) return;

    const bool levels = m_wheelLevels || (e->modifiers() & Qt::AltModifier);
    if (levels && !m_renderer->levelTargets().isEmpty()) {
        scaleContourLevels(std::pow(kLevelStepPerNotch, -steps));
        return;
    }
    const double factor = std::pow(kWheelZoomPerStep, steps);

    ZoomAxes axes = m_zoomAxes;
    if (e->modifiers() & Qt::ControlModifier) axes = ZoomAxes::XOnly;
    if (e->modifiers() & Qt::ShiftModifier) axes = ZoomAxes::YOnly;
    // Wheel over an axis zooms only that axis.
    const QRect ar = m_plot->axisRect()->rect();
    if (px.y() > ar.bottom()) axes = ZoomAxes::XOnly;
    else if (px.x() < ar.left()) axes = ZoomAxes::YOnly;

    if (axes != ZoomAxes::YOnly) m_plot->xAxis->scaleRange(factor, m_plot->xAxis->pixelToCoord(px.x()));
    if (axes != ZoomAxes::XOnly) m_plot->yAxis->scaleRange(factor, m_plot->yAxis->pixelToCoord(px.y()));
    m_renderer->replot();
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
            // Cancel the drag: restore the state captured at press time.
            PlotModel* model = m_editor->model();
            if (m_drag == Drag::Marker && model->marker(m_dragStartMarker.id))
                m_editor->previewMarker(m_dragStartMarker);
            else if (m_drag == Drag::Annotation && model->annotation(m_dragStartAnnotation.id))
                m_editor->previewAnnotation(m_dragStartAnnotation);
            else if (m_drag == Drag::RegionCreate || m_drag == Drag::RegionMin ||
                     m_drag == Drag::RegionMax || m_drag == Drag::RegionMove)
                m_editor->previewRegion(m_dragStartRegion);
            else if (m_drag == Drag::Pan)
                m_renderer->setViewRange(m_pressView);
            m_rubberBand->hide();
            m_drag = Drag::None;
        } else if (m_mode != m_navMode) {
            setMode(m_navMode);
        } else {
            setSelection({});
        }
        return true;
    default: return false;
    }
}

void InteractionController::updateHoverCursor(const QPointF& px)
{
    if (m_mode == InteractionMode::AddVMarker || m_mode == InteractionMode::AddHMarker ||
        m_mode == InteractionMode::AddAnnotation) {
        m_plot->setCursor(Qt::CrossCursor);
        return;
    }
    const ObjectRef hit = m_renderer->hitTestObject(px);
    if (hit.kind == ObjectKind::Marker) {
        const Marker* m = m_editor->model()->marker(hit.id);
        m_plot->setCursor(m && m->orientation == Orientation::Vertical ? Qt::SizeHorCursor
                                                                         : Qt::SizeVerCursor);
        return;
    }
    if (hit.kind == ObjectKind::Annotation) {
        m_plot->setCursor(Qt::SizeAllCursor);
        return;
    }
    const RegionHit rh = m_renderer->hitTestRegion(px);
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
