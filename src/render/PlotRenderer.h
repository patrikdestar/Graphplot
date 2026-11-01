// PlotRenderer: maps the PlotModel onto QCustomPlot objects.
//
// The renderer is a one-way observer: it listens to PlotModel signals and
// creates/updates/destroys the corresponding QCustomPlot plottables and items.
// It never modifies the model. It also offers coordinate conversion and
// hit-testing, which the interaction layer uses.
#pragma once

#include "model/PlotModel.h"

#include <QHash>
#include <QObject>
#include <QPointF>

class QCustomPlot;
class QCPAbstractPlottable;
class QCPItemStraightLine;
class QCPItemText;
class QCPItemRect;
class QCPLayer;
class QCPTextElement;

namespace gp {

struct ViewRange {
    double xmin = 0, xmax = 1, ymin = 0, ymax = 1;
    bool operator==(const ViewRange&) const = default;
};

enum class RegionHit { None, MinEdge, MaxEdge, Inside };

class PlotRenderer : public QObject {
    Q_OBJECT
public:
    PlotRenderer(PlotModel* model, QCustomPlot* plot, QObject* parent = nullptr);
    ~PlotRenderer() override;

    QCustomPlot* plot() const { return m_plot; }
    PlotModel* model() const { return m_model; }

    // ---- view ---------------------------------------------------------
    ViewRange viewRange() const;
    void setViewRange(const ViewRange& r);
    // Fits the view to the visible data (with a small margin).
    void autoscale(bool x = true, bool y = true);
    // The "home" view restored by resetView(). It is captured automatically
    // the first time data arrives, or explicitly via setHomeView().
    void setHomeView(const ViewRange& r);
    void captureHomeView() { setHomeView(viewRange()); }
    void resetView();

    // ---- coordinates --------------------------------------------------
    QPointF pixelToData(const QPointF& px) const;
    QPointF dataToPixel(const QPointF& data) const;
    bool isInsidePlotArea(const QPointF& px) const;

    // ---- hit testing (pixel coordinates) -------------------------------
    // Returns the top-most marker or annotation under px.
    ObjectRef hitTestObject(const QPointF& px, double tolerancePx = 5.0) const;
    RegionHit hitTestRegion(const QPointF& px, double tolerancePx = 5.0) const;

    // Visual emphasis for the currently selected object.
    void setHighlight(const ObjectRef& ref);
    ObjectRef highlight() const { return m_highlight; }

    // Temporarily hide selection highlighting (used for export).
    void setExportMode(bool on);

    void replot();               // queued full replot
    void replotObjectLayers();   // immediate replot of marker/annotation/region layers only

signals:
    void viewChanged();

private:
    struct MarkerVisual {
        QCPItemStraightLine* line = nullptr;
        QCPItemText* label = nullptr;
    };
    struct RegionVisual {
        QCPItemRect* rect = nullptr;
        QCPItemStraightLine* minEdge = nullptr;
        QCPItemStraightLine* maxEdge = nullptr;
        QCPItemText* label = nullptr;
    };

    void setupAppearance();
    void rebuildAll();

    void onDatasetAdded(ObjectId id);
    void onDatasetRemoved(ObjectId id);
    void applyDatasetAppearance(ObjectId id);
    void applyDatasetData(ObjectId id);
    void syncLegend();

    void onMarkerAdded(ObjectId id);
    void onMarkerRemoved(ObjectId id);
    void applyMarker(ObjectId id);

    void onAnnotationAdded(ObjectId id);
    void onAnnotationRemoved(ObjectId id);
    void applyAnnotation(ObjectId id);

    void applyRegion();
    void applyAxesInfo();
    void maybeCaptureHome();

    PlotModel* m_model;
    QCustomPlot* m_plot;
    QCPLayer* m_regionLayer = nullptr;
    QCPLayer* m_objectLayer = nullptr;
    QCPTextElement* m_title = nullptr;

    QHash<ObjectId, QCPAbstractPlottable*> m_plottables;
    QHash<ObjectId, MarkerVisual> m_markers;
    QHash<ObjectId, QCPItemText*> m_annotations;
    RegionVisual m_region;

    ObjectRef m_highlight;
    bool m_exportMode = false;
    bool m_hasHome = false;
    ViewRange m_home;
};

}  // namespace gp
