// InteractionController: translates mouse/keyboard input on the plot widget
// into view changes (zoom/pan) and editing requests.
//
// It is installed as an event filter on the QCustomPlot widget, so
// QCustomPlot's own interaction handling is bypassed entirely and the
// behaviour is fully defined here.
#pragma once

#include "model/PlotTypes.h"
#include "render/PlotRenderer.h"

#include <QObject>
#include <QPoint>
#include <QPointer>

class QRubberBand;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;

namespace gp {

class PlotEditor;

enum class InteractionMode { Zoom, Pan, Region, AddVMarker, AddHMarker, AddAnnotation };
enum class ZoomAxes { Both, XOnly, YOnly };

class InteractionController : public QObject {
    Q_OBJECT
public:
    InteractionController(PlotRenderer* renderer, PlotEditor* editor, QObject* parent = nullptr);

    InteractionMode mode() const { return m_mode; }
    void setMode(InteractionMode mode);

    // Which axes wheel / rubber-band zoom affect. Holding Ctrl restricts a
    // single wheel step to X, holding Shift restricts it to Y.
    ZoomAxes zoomAxes() const { return m_zoomAxes; }
    void setZoomAxes(ZoomAxes axes) { m_zoomAxes = axes; }

    ObjectRef selection() const { return m_selection; }
    void setSelection(const ObjectRef& ref);
    void deleteSelection();

signals:
    void modeChanged(gp::InteractionMode mode);
    void selectionChanged(gp::ObjectRef ref);
    void cursorMoved(QPointF dataPos, bool insidePlot);
    // Emitted on double-click / "Edit…" context action for an object.
    void editRequested(gp::ObjectRef ref);
    void contextMenuRequested(gp::ObjectRef ref, QPoint globalPos, QPointF dataPos);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Drag { None, Pan, RubberBand, RegionCreate, RegionMin, RegionMax, RegionMove,
                      Marker, Annotation };

    void mousePress(QMouseEvent* e);
    void mouseMove(QMouseEvent* e);
    void mouseRelease(QMouseEvent* e);
    void mouseDoubleClick(QMouseEvent* e);
    void wheel(QWheelEvent* e);
    bool key(QKeyEvent* e);
    void updateHoverCursor(const QPointF& px);
    void finishOneShotMode();

    PlotRenderer* m_renderer;
    PlotEditor* m_editor;
    QCustomPlot* m_plot;
    QRubberBand* m_rubberBand = nullptr;

    InteractionMode m_mode = InteractionMode::Zoom;
    InteractionMode m_navMode = InteractionMode::Zoom;  // restored after one-shot modes
    ZoomAxes m_zoomAxes = ZoomAxes::Both;
    ObjectRef m_selection;

    Drag m_drag = Drag::None;
    QPoint m_pressPx;
    QPointF m_pressData;
    ViewRange m_pressView;
    Region m_dragStartRegion;
    Marker m_dragStartMarker;
    Annotation m_dragStartAnnotation;
    bool m_dragMoved = false;
};

}  // namespace gp
