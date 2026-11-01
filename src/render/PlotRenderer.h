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
class QCPColorMap;

namespace gp {

class ContourPlottable;
class PeakOverlay;

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

    // Contour plottable of a 2D dataset (nullptr if unknown); for tests/tools.
    ContourPlottable* contourPlottable(ObjectId matrixId) const;
    // Matrices affected by contour level scrolling: the selected one, or all
    // visible ones when no matrix is selected.
    QList<ObjectId> levelTargets() const;
    PeakOverlay* peakOverlay() const { return m_peaks; }
    // The visible 2D dataset whose extent contains the data point (topmost first).
    const Matrix* matrixAt(const QPointF& data) const;

    void replot();               // queued full replot
    void replotObjectLayers();   // immediate replot of marker/annotation/region layers only

signals:
    void viewChanged();

private:
    struct MarkerVisual {
        QCPItemStraightLine* line = nullptr;
        QCPItemText* label = nullptr;
    };
    struct MatrixVisual {
        ContourPlottable* contour = nullptr;
        QCPColorMap* image = nullptr;
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

    void onMatrixAdded(ObjectId id);
    void onMatrixRemoved(ObjectId id);
    void applyMatrixAppearance(ObjectId id);
    void applyMatrixData(ObjectId id);
    void updateMatrixImage(ObjectId id);

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
    QCPLayer* m_imageLayer = nullptr;
    QCPLayer* m_regionLayer = nullptr;
    QCPLayer* m_objectLayer = nullptr;
    QCPTextElement* m_title = nullptr;
    QCPItemStraightLine* m_diagonal = nullptr;
    PeakOverlay* m_peaks = nullptr;

    QHash<ObjectId, QCPAbstractPlottable*> m_plottables;
    QHash<ObjectId, MatrixVisual> m_matrices;
    QHash<ObjectId, MarkerVisual> m_markers;
    QHash<ObjectId, QCPItemText*> m_annotations;
    RegionVisual m_region;

    ObjectRef m_highlight;
    bool m_exportMode = false;
    bool m_hasHome = false;
    ViewRange m_home;
};

}  // namespace gp
