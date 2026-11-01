// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
// PlotRenderer: maps the PlotModel onto QCustomPlot objects.
//
// The renderer is a one-way observer: it listens to PlotModel signals and
// creates/updates/destroys the corresponding QCustomPlot plottables and items.
// It never modifies the model. It also offers coordinate conversion and
// hit-testing, which the interaction layer uses.
#pragma once

#include "model/PlotModel.h"

#include <QMargins>
#include <QHash>
#include <QObject>
#include <QFont>
#include <QPointF>
#include <QSharedPointer>

class QCustomPlot;
class QCPAbstractPlottable;
class QCPItemStraightLine;
class QCPItemLine;
class QCPItemRect;
class QCPItemText;
class QCPLayer;
class QCPTextElement;
class QCPColorMap;
class QCPErrorBars;
class QCPAxisRect;
class QCPAxis;
class QCPLegend;
class QCPAxisTicker;
class QCPGraph;
class QCPAbstractItem;

namespace gp {

class ContourPlottable;
class RichTextItem;
class HeaderElement;
class RichAxisLabels;
class PeakOverlay;

struct ViewRange {
    double xmin = 0, xmax = 1, ymin = 0, ymax = 1;
    bool operator==(const ViewRange&) const = default;
};

enum class RegionHit { None, MinEdge, MaxEdge, Inside };

class PlotRenderer : public QObject {
    Q_OBJECT
public:
    // Renders into the plot's default axis rect.
    PlotRenderer(PlotModel* model, QCustomPlot* plot, QObject* parent = nullptr);
    // Renders into `rect` (multi-panel figures). The primary renderer also
    // owns figure-level elements: header row, padding, background.
    PlotRenderer(PlotModel* model, QCustomPlot* plot, QCPAxisRect* rect, bool primary,
                 QObject* parent = nullptr);
    // A twin panel (twinx): drawn over `host`'s plot area, sharing its x
    // axis, with its own y axis on the right. `rect` must already be placed
    // over the host (see FigureWindow::createTwinPanel).
    PlotRenderer(PlotModel* model, QCustomPlot* plot, QCPAxisRect* rect, PlotRenderer* host,
                 QObject* parent = nullptr);
    PlotRenderer* twinHost() const { return m_twinHost; }
    PlotRenderer* twin() const { return m_twin; }
    ~PlotRenderer() override;

    QCustomPlot* plot() const { return m_plot; }
    QCPAxisRect* axisRect() const { return m_rect; }
    QCPAxis* xAxis() const { return m_x; }
    QCPAxis* yAxis() const { return m_y; }
    bool isPrimary() const { return m_primary; }
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
    // Off: the first data no longer autoscales the view (used by timeline
    // rows, whose x range is the shared time window).
    void setAutoHome(bool on) { m_autoHome = on; }
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

    // Fonts from the current style (also used by the GUI for consistency).
    QFont styleFont(double pointSize, bool bold = false) const { return font(pointSize, bold); }

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

    // Shows/hides the whole panel (frame, axes, data, items) - used for rows
    // that are temporarily removed from a layout.
    void setPanelVisible(bool visible);
    bool panelVisible() const { return m_panelVisible; }
    void setRegionLabelVisible(bool on) { m_regionLabelOn = on; applyRegion(); }
    // Panels whose x / y axis is linked to this one: autoscaling fits the
    // union of their data (shared axes).
    void setSharedWith(const QList<PlotRenderer*>& x, const QList<PlotRenderer*>& y)
    {
        m_shareX = x;
        m_shareY = y;
    }
    // Multi-panel figures: the panel draws its title above its own frame (and
    // the figure header shows the suptitle); single panels use the header.
    void setOwnTitle(bool on);
    // Upper limit for the number of y ticks (0 = the style's); short panels
    // such as timeline rows use fewer so labels do not overlap.
    // Takes effect in the current replot (it is called from afterLayout).
    void setMaxTicksY(int n);
    void startFresh();
    const QList<PlotRenderer*>& sharedX() const { return m_shareX; }
    const QList<PlotRenderer*>& sharedY() const { return m_shareY; }
    void fitTickCounts();
    bool fitEndLabels();
    bool applyEndMargins(int l, int t, int r, int b);
    // The view-fitting state (follow flags, home, current view), saved and
    // restored around an undoable clear.
    struct FitState {
        bool followX = true, followY = true, hasHome = false;
        ViewRange home, view;
    };
    FitState fitState() const { return {m_followX, m_followY, m_hasHome, m_home, viewRange()}; }
    void setFitState(const FitState& s)
    {
        m_followX = s.followX;
        m_followY = s.followY;
        m_hasHome = s.hasHome;
        m_home = s.home;
        setViewRange(s.view);
        m_followX = s.followX;  // setting the view is not a user limit
        m_followY = s.followY;
    }
    // Fit the axes that still follow the data (limits set by the user stay).
    void fitFollowing()
    {
        if (m_followX || m_followY) autoscale(m_followX, m_followY);
    }
    void applyLegendLoc(const Style& st);
    void placeLegendBest();

    bool measureTwinMargin();  // twin: right-axis width into the host padding; true if changed  // legend.loc 'best': the corner over the fewest data points
    // Draw the region also in exports (default: no, it is a selection aid).
    void setRegionInExports(bool on) { m_regionInExports = on; }

    // The style as drawn: the figure's style, with the display theme's plot
    // colours on top on screen (see DisplayTheme).
    Style displayStyle() const;
    // Re-applies every colour / style (after a theme change).
    void refreshAppearance();

    void replot();               // queued full replot
    void replotObjectLayers();   // immediate replot of marker/annotation/region layers only

signals:
    void viewChanged();

private:
    struct MarkerVisual {
        QCPItemStraightLine* line = nullptr;
        RichTextItem* label = nullptr;
        QCPItemRect* band = nullptr;  // span markers (axvspan / axhspan)
        QCPGraph* legendSample = nullptr;  // empty graph giving the legend entry its look
    };
    struct MatrixVisual {
        ContourPlottable* contour = nullptr;
        QCPColorMap* image = nullptr;
        double lo = 0, hi = 0;  // finite data range (for automatic colour limits)
        bool refined = false;   // image cells interpolated finer (filled contours)
    };
    struct RegionVisual {
        QCPItemRect* rect = nullptr;
        QCPItemStraightLine* minEdge = nullptr;
        QCPItemStraightLine* maxEdge = nullptr;
        QCPItemText* label = nullptr;
    };

    void setupAppearance();
    void bindItem(QCPAbstractItem* item);
    void rebuildAll();

    void onDatasetAdded(ObjectId id);
    void onDatasetRemoved(ObjectId id);
    void applyDatasetAppearance(ObjectId id);
    void applyDatasetData(ObjectId id);
    void syncLegend();
    void updateErrorBars(ObjectId id);
    void removeErrorBars(ObjectId id);

    void onMatrixAdded(ObjectId id);
    void onMatrixRemoved(ObjectId id);
    void applyMatrixAppearance(ObjectId id);
    void applyMatrixData(ObjectId id);
    void updateMatrixImage(ObjectId id);
    void applyImageAppearance(const Matrix& m, MatrixVisual& v);  // colour map + limits (cheap)
    void updateColorBar();

    void onMarkerAdded(ObjectId id);
    void onMarkerRemoved(ObjectId id);
    void applyMarker(ObjectId id);

    void onAnnotationAdded(ObjectId id);
    void onAnnotationRemoved(ObjectId id);
    void applyAnnotation(ObjectId id);

    void applyRegion();
    void applyAxesInfo();
    void applyStyle();
    void updateHeader();
    QFont font(double pointSize, bool bold = false) const;
    QColor ink(const QColor& c) const;           // data colour as drawn (theme-aware)
    static QColor labelBox(const Style& st);     // background of label boxes
    void maybeCaptureHome();

    PlotModel* m_model;
    QCustomPlot* m_plot;
    QCPAxisRect* m_rect;
    bool m_primary;
    QCPAxis* m_x = nullptr;
    QCPAxis* m_y = nullptr;
    QCPAxis* m_x2 = nullptr;
    QCPAxis* m_y2 = nullptr;
    QCPLegend* m_legend = nullptr;
    QSharedPointer<QCPAxisTicker> m_linTickerX, m_linTickerY;  // kept while an axis is logarithmic
    QSharedPointer<QCPAxisTicker> m_tickerBeforeCustomX, m_tickerBeforeCustomY;  // while custom ticks are shown
    QCPLayer* m_imageLayer = nullptr;
    QCPLayer* m_regionLayer = nullptr;
    QCPLayer* m_objectLayer = nullptr;
    HeaderElement* m_header = nullptr;
    QCPAbstractItem* m_supX = nullptr;  // figure-wide axis labels (primary panel)
    QCPAbstractItem* m_supY = nullptr;
    void applyFigureMargins(const Style& st);
    RichAxisLabels* m_axisLabels = nullptr;
    RichTextItem* m_insideLabel = nullptr;
    QCPItemStraightLine* m_diagonal = nullptr;
    PeakOverlay* m_peaks = nullptr;
    class ColorBar* m_colorBar = nullptr;

    QHash<ObjectId, QCPAbstractPlottable*> m_plottables;
    struct ErrorVisual {
        QCPErrorBars* y = nullptr;
        QCPErrorBars* x = nullptr;
    };
    QHash<ObjectId, ErrorVisual> m_errorBars;
    QHash<ObjectId, MatrixVisual> m_matrices;
    QHash<ObjectId, MarkerVisual> m_markers;
    QHash<ObjectId, RichTextItem*> m_annotations;
    QHash<ObjectId, QCPItemLine*> m_annotationArrows;
    RegionVisual m_region;

    ObjectRef m_highlight;
    bool m_exportMode = false;
    bool m_hasHome = false;
    bool m_autoHome = true;
    bool m_regionInExports = false;
    bool m_legendBest = false;
    bool m_ownTitle = false;
    PlotRenderer* m_twinHost = nullptr;   // set for a twin panel
    PlotRenderer* m_twin = nullptr;       // set on the host of a twin
    int m_twinMargin = 0;                 // right-margin space the twin's axis needs (host)
    int m_colorBarWidth = 0;
    void updateRightPadding();            // colour bar + twin axis space in the right margin
    void updateTwinMargin();              // twin: tell the host how much room its axis needs
    int m_maxTicksY = 0;
    int m_autoTicksX = 0, m_autoTicksY = 0;  // limits from label size (fitTickCounts)
    QList<PlotRenderer*> m_shareX, m_shareY;
    // The view follows the data (re-fits when data is added or changed) until
    // the user or code sets the view; autoscale() resumes following.
    bool m_followX = true, m_followY = true;  // per axis
    bool m_autoscaling = false;
    bool m_panelVisible = true;
    bool m_regionLabelOn = true;
    ViewRange m_home;
};

}  // namespace gp
