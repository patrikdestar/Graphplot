// PlotModel: the single source of truth for a figure's content.
//
// It owns datasets, markers, annotations, the X-region selection and axis
// labels. It knows nothing about rendering, widgets or mouse handling; views
// observe it through signals. All mutating functions are "primitive"
// operations - undoable editing is layered on top by the commands in
// src/commands, which call these functions.
#pragma once

#include "PlotTypes.h"

#include <QObject>
#include <optional>
#include <vector>

namespace gp {

class PlotModel : public QObject {
    Q_OBJECT
public:
    explicit PlotModel(QObject* parent = nullptr);

    // ---- Datasets ------------------------------------------------------
    const std::vector<Dataset>& datasets() const { return m_datasets; }
    const Dataset* dataset(ObjectId id) const;
    const Dataset* datasetByName(const QString& name) const;
    int datasetIndex(ObjectId id) const;
    // Inserts at index (-1 = append). Assigns a new id if d.id is invalid.
    ObjectId insertDataset(Dataset d, int index = -1);
    std::optional<Dataset> takeDataset(ObjectId id);
    // Replaces name/style (keeps data). Returns false for unknown id.
    bool setDatasetAppearance(ObjectId id, const QString& name, const DatasetStyle& style);
    // Replaces the numeric data. x and y must have equal length.
    bool setDatasetData(ObjectId id, QVector<double> x, QVector<double> y);
    // Colour for the next dataset added without an explicit colour; advances
    // through the default palette in fixed order.
    QColor takeNextDatasetColor();

    // ---- Markers -------------------------------------------------------
    const std::vector<Marker>& markers() const { return m_markers; }
    const Marker* marker(ObjectId id) const;
    int markerIndex(ObjectId id) const;
    ObjectId insertMarker(Marker m, int index = -1);
    std::optional<Marker> takeMarker(ObjectId id);
    bool updateMarker(const Marker& m);

    // ---- Annotations ---------------------------------------------------
    const std::vector<Annotation>& annotations() const { return m_annotations; }
    const Annotation* annotation(ObjectId id) const;
    int annotationIndex(ObjectId id) const;
    ObjectId insertAnnotation(Annotation a, int index = -1);
    std::optional<Annotation> takeAnnotation(ObjectId id);
    bool updateAnnotation(const Annotation& a);

    // ---- Region selection ----------------------------------------------
    const Region& region() const { return m_region; }
    void setRegion(const Region& r);  // normalises xmin <= xmax

    // ---- Axes labels / decorations ---------------------------------------
    const AxesInfo& axesInfo() const { return m_axes; }
    void setAxesInfo(const AxesInfo& info);

    // Bounding box of all visible datasets; returns false if there is no data.
    bool dataBounds(double& xmin, double& xmax, double& ymin, double& ymax,
                    bool visibleOnly = true) const;

    void clear();

    // Reserves a fresh object id (used by commands so that an object keeps
    // the same id across undo/redo).
    ObjectId allocateId() { return newId(); }

signals:
    void datasetAdded(gp::ObjectId id);
    void datasetRemoved(gp::ObjectId id);
    void datasetAppearanceChanged(gp::ObjectId id);
    void datasetDataChanged(gp::ObjectId id);

    void markerAdded(gp::ObjectId id);
    void markerRemoved(gp::ObjectId id);
    void markerChanged(gp::ObjectId id);

    void annotationAdded(gp::ObjectId id);
    void annotationRemoved(gp::ObjectId id);
    void annotationChanged(gp::ObjectId id);

    void regionChanged();
    void axesInfoChanged();

private:
    ObjectId newId() { return ++m_lastId; }

    std::vector<Dataset> m_datasets;
    std::vector<Marker> m_markers;
    std::vector<Annotation> m_annotations;
    Region m_region;
    AxesInfo m_axes;
    ObjectId m_lastId = 0;
    int m_colorCounter = 0;
};

}  // namespace gp
