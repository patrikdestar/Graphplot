// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
// PlotModel: the single source of truth for a figure's content.
//
// It owns datasets, markers, annotations, the X-region selection and axis
// labels. It knows nothing about rendering, widgets or mouse handling; views
// observe it through signals. All mutating functions are "primitive"
// operations - undoable editing is layered on top by the commands in
// src/commands, which call these functions.
#pragma once

#include "PlotTypes.h"
#include "Style.h"

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
    // Replaces the error arrays (each empty or the same length as the data).
    bool setDatasetErrors(ObjectId id, QVector<double> xLow, QVector<double> xHigh,
                          QVector<double> yLow, QVector<double> yHigh);
    // Colour for the next dataset added without an explicit colour; advances
    // through the default palette in fixed order.
    QColor takeNextDatasetColor();

    // ---- 2D data (contour plots) ---------------------------------------
    const std::vector<Matrix>& matrices() const { return m_matrices; }
    const Matrix* matrix(ObjectId id) const;
    int matrixIndex(ObjectId id) const;
    ObjectId insertMatrix(Matrix m, int index = -1);
    std::optional<Matrix> takeMatrix(ObjectId id);
    bool setMatrixAppearance(ObjectId id, const QString& name, const ContourStyle& style);
    // Replaces grid values and extent; nx*ny must equal z.size().
    bool setMatrixData(ObjectId id, int nx, int ny, QVector<double> z, double x0, double x1,
                       double y0, double y1);

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

    // ---- Peaks -----------------------------------------------------------
    const std::vector<Peak>& peaks() const { return m_peaks; }
    const Peak* peak(ObjectId id) const;
    int peakIndex(ObjectId id) const;
    ObjectId insertPeak(Peak p, int index = -1);
    std::optional<Peak> takePeak(ObjectId id);
    bool updatePeak(const Peak& p);

    // ---- Region selection ----------------------------------------------
    const Region& region() const { return m_region; }
    void setRegion(const Region& r);  // normalises xmin <= xmax

    // ---- Style (all visual settings) -------------------------------------
    const Style& style() const { return m_style; }
    void setStyle(const Style& s);

    // ---- Axes labels / decorations ---------------------------------------
    const AxesInfo& axesInfo() const { return m_axes; }
    void setAxesInfo(const AxesInfo& info);

    // Bounding box of all visible datasets and matrices; returns false if there is no data.
    // xPositive / yPositive skip values <= 0 (for logarithmic axes).
    bool dataBounds(double& xmin, double& xmax, double& ymin, double& ymax,
                    bool visibleOnly = true, bool xPositive = false, bool yPositive = false) const;

    // Reserves a fresh object id (used by commands so that an object keeps
    // the same id across undo/redo).
    ObjectId allocateId() { return newId(); }

signals:
    void datasetAdded(gp::ObjectId id);
    void datasetRemoved(gp::ObjectId id);
    void datasetAppearanceChanged(gp::ObjectId id);
    void datasetDataChanged(gp::ObjectId id);

    void matrixAdded(gp::ObjectId id);
    void matrixRemoved(gp::ObjectId id);
    void matrixAppearanceChanged(gp::ObjectId id);
    void matrixDataChanged(gp::ObjectId id);

    void markerAdded(gp::ObjectId id);
    void markerRemoved(gp::ObjectId id);
    void markerChanged(gp::ObjectId id);

    void annotationAdded(gp::ObjectId id);
    void annotationRemoved(gp::ObjectId id);
    void annotationChanged(gp::ObjectId id);

    void peakAdded(gp::ObjectId id);
    void peakRemoved(gp::ObjectId id);
    void peakChanged(gp::ObjectId id);

    void regionChanged();
    void axesInfoChanged();
    void styleChanged();

private:
    ObjectId newId() { return ++m_lastId; }

    std::vector<Dataset> m_datasets;
    std::vector<Matrix> m_matrices;
    std::vector<Marker> m_markers;
    std::vector<Annotation> m_annotations;
    std::vector<Peak> m_peaks;
    Region m_region;
    AxesInfo m_axes;
    Style m_style = defaultStyle();
    ObjectId m_lastId = 0;
    int m_colorCounter = 0;

public:
    void resetColorCycle() { m_colorCounter = 0; }  // a cleared panel starts at the first colour
    int colorCounter() const { return m_colorCounter; }
    void setColorCounter(int n) { m_colorCounter = n; }
};

}  // namespace gp
