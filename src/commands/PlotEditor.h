// PlotEditor: the single entry point for *editing* a figure.
//
// Both the GUI (mouse interaction, property panel) and the Python bindings
// edit the plot through this class. Every user-visible edit is turned into a
// QUndoCommand and pushed on the QUndoStack, so undo/redo works the same way
// no matter where the edit came from.
//
// "preview*" functions change the model *without* recording history; they
// are used for live feedback while dragging. When the drag ends, the caller
// commits the change with the before-state captured at drag start.
#pragma once

#include "model/PeakPicker.h"
#include "model/PlotModel.h"

#include <QObject>
#include <optional>

class QUndoStack;

namespace gp {

class PlotEditor : public QObject {
    Q_OBJECT
public:
    explicit PlotEditor(PlotModel* model, QObject* parent = nullptr);
    // Shares an existing undo stack (one history for all panels of a figure).
    PlotEditor(PlotModel* model, QUndoStack* sharedStack, QObject* parent = nullptr);

    PlotModel* model() const { return m_model; }
    QUndoStack* undoStack() const { return m_stack; }

    // ---- datasets ------------------------------------------------------
    // If d.style.color is invalid, the next palette colour is used.
    ObjectId addDataset(Dataset d);
    void removeDataset(ObjectId id);
    void setDatasetAppearance(ObjectId id, const QString& name, const DatasetStyle& style,
                              bool mergeable = false);
    // Replaces data. Undoable by default; pass undoable=false for streaming
    // updates (e.g. from a Python loop) so the history does not grow.
    void setDatasetData(ObjectId id, QVector<double> x, QVector<double> y, bool undoable = true);

    // ---- 2D data (contours) ---------------------------------------------
    // Fills in an automatic base level and palette colour when unset.
    ObjectId addMatrix(Matrix m);
    void removeMatrix(ObjectId id);
    void setMatrixAppearance(ObjectId id, const QString& name, const ContourStyle& style,
                             bool mergeable = false);
    void setMatrixData(ObjectId id, int nx, int ny, QVector<double> z, double x0, double x1,
                       double y0, double y1);
    // Multiplies the contour base level of the given matrices by `factor`
    // (factor < 1 shows more/lower levels). Consecutive calls - e.g. one per
    // mouse-wheel notch - merge into a single undo step.
    void scaleContourLevels(const QList<ObjectId>& ids, double factor);

    // ---- markers -------------------------------------------------------
    ObjectId addMarker(Marker m);
    void removeMarker(ObjectId id);
    // Commits an edit. `before` defaults to the current model state; pass it
    // explicitly when the model already shows the new state (after a drag).
    void updateMarker(const Marker& after, std::optional<Marker> before = std::nullopt,
                      bool mergeable = false, const QString& text = "Edit marker");
    void previewMarker(const Marker& m) { m_model->updateMarker(m); }

    // ---- annotations ---------------------------------------------------
    ObjectId addAnnotation(Annotation a);
    void removeAnnotation(ObjectId id);
    void updateAnnotation(const Annotation& after, std::optional<Annotation> before = std::nullopt,
                          bool mergeable = false, const QString& text = "Edit annotation");
    void previewAnnotation(const Annotation& a) { m_model->updateAnnotation(a); }

    // ---- peaks -----------------------------------------------------------
    ObjectId addPeak(Peak p);
    // Adds many peaks as one undo step ("Pick peaks"); returns their ids.
    QList<ObjectId> addPeaks(std::vector<Peak> peaks, const QString& text = "Pick peaks");
    void removePeak(ObjectId id);
    void removePeaks(const QList<ObjectId>& ids, const QString& text = "Delete peaks");
    void updatePeak(const Peak& after, std::optional<Peak> before = std::nullopt,
                    bool mergeable = false, const QString& text = "Edit peak");
    void previewPeak(const Peak& p) { m_model->updatePeak(p); }
    // Runs automatic peak picking on a 2D dataset and adds the result as one
    // undo step. threshold <= 0 uses the dataset's lowest contour level (so
    // "what you see is what gets picked"). Peaks closer than minDistance grid
    // points to an existing peak are skipped, so picking twice adds nothing.
    QList<ObjectId> pickPeaks(ObjectId matrixId, PeakPickOptions opt);

    // ---- region ----------------------------------------------------------
    void setRegion(const Region& after, std::optional<Region> before = std::nullopt);
    void clearRegion() { setRegion(Region{}); }
    void previewRegion(const Region& r) { m_model->setRegion(r); }

    // ---- axes ------------------------------------------------------------
    void setAxesInfo(const AxesInfo& info);
    void setStyle(const Style& style, const QString& text = "Change style");

    // Removes any kind of object (dataset, marker, annotation, region).
    void removeObject(const ObjectRef& ref);

    void undo();
    void redo();

private:
    PlotModel* m_model;
    QUndoStack* m_stack;
};

}  // namespace gp
