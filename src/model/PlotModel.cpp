// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "PlotModel.h"

#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace gp {

namespace {

template <typename T>
int indexOf(const std::vector<T>& v, ObjectId id)
{
    for (int i = 0; i < int(v.size()); ++i)
        if (v[i].id == id) return i;
    return -1;
}

template <typename T>
const T* find(const std::vector<T>& v, ObjectId id)
{
    const int i = indexOf(v, id);
    return i < 0 ? nullptr : &v[i];
}

template <typename T>
void insertAt(std::vector<T>& v, T obj, int index)
{
    if (index < 0 || index > int(v.size())) index = int(v.size());
    v.insert(v.begin() + index, std::move(obj));
}

template <typename T>
std::optional<T> take(std::vector<T>& v, ObjectId id)
{
    const int i = indexOf(v, id);
    if (i < 0) return std::nullopt;
    T obj = std::move(v[i]);
    v.erase(v.begin() + i);
    return obj;
}

}  // namespace

PlotModel::PlotModel(QObject* parent) : QObject(parent) { m_axes.gridVisible = m_style.grid; }

// ---- Datasets ----------------------------------------------------------

const Dataset* PlotModel::dataset(ObjectId id) const { return find(m_datasets, id); }

const Dataset* PlotModel::datasetByName(const QString& name) const
{
    for (const auto& d : m_datasets)
        if (d.name == name) return &d;
    return nullptr;
}

int PlotModel::datasetIndex(ObjectId id) const { return indexOf(m_datasets, id); }

ObjectId PlotModel::insertDataset(Dataset d, int index)
{
    if (d.id == kInvalidId) d.id = newId();
    else m_lastId = std::max(m_lastId, d.id);
    if (d.x.size() != d.y.size()) {
        const auto n = std::min(d.x.size(), d.y.size());
        d.x.resize(n);
        d.y.resize(n);
    }
    // Error bars are kept only as complete (low, high) pairs of the data's length.
    for (auto [lo, hi] : {std::pair{&d.xErrLow, &d.xErrHigh}, std::pair{&d.yErrLow, &d.yErrHigh}})
        if (lo->size() != d.x.size() || hi->size() != d.x.size()) lo->clear(), hi->clear();
    const ObjectId id = d.id;
    insertAt(m_datasets, std::move(d), index);
    emit datasetAdded(id);
    return id;
}

std::optional<Dataset> PlotModel::takeDataset(ObjectId id)
{
    auto d = take(m_datasets, id);
    if (d) emit datasetRemoved(id);
    return d;
}

bool PlotModel::setDatasetAppearance(ObjectId id, const QString& name, const DatasetStyle& style)
{
    const int i = datasetIndex(id);
    if (i < 0) return false;
    Dataset& d = m_datasets[i];
    if (d.name == name && d.style == style) return true;
    d.name = name;
    d.style = style;
    emit datasetAppearanceChanged(id);
    return true;
}

bool PlotModel::setDatasetData(ObjectId id, QVector<double> x, QVector<double> y)
{
    const int i = datasetIndex(id);
    if (i < 0) return false;
    if (x.size() != y.size()) {
        const auto n = std::min(x.size(), y.size());
        x.resize(n);
        y.resize(n);
    }
    if (x.size() != m_datasets[i].x.size()) {
        for (auto* v : {&m_datasets[i].xErrLow, &m_datasets[i].xErrHigh, &m_datasets[i].yErrLow,
                        &m_datasets[i].yErrHigh})
            v->clear();
    }
    m_datasets[i].x = std::move(x);
    m_datasets[i].y = std::move(y);
    emit datasetDataChanged(id);
    return true;
}

bool PlotModel::setDatasetErrors(ObjectId id, QVector<double> xLow, QVector<double> xHigh,
                                 QVector<double> yLow, QVector<double> yHigh)
{
    const int i = datasetIndex(id);
    if (i < 0) return false;
    Dataset& d = m_datasets[i];
    const qsizetype n = d.x.size();
    for (auto* v : {&xLow, &xHigh, &yLow, &yHigh})
        if (!v->isEmpty() && v->size() != n) return false;
    d.xErrLow = std::move(xLow);
    d.xErrHigh = std::move(xHigh);
    d.yErrLow = std::move(yLow);
    d.yErrHigh = std::move(yHigh);
    emit datasetDataChanged(id);
    return true;
}

QColor PlotModel::takeNextDatasetColor() { return m_style.paletteColor(m_colorCounter++); }

// ---- Matrices -------------------------------------------------------------

const Matrix* PlotModel::matrix(ObjectId id) const { return find(m_matrices, id); }
int PlotModel::matrixIndex(ObjectId id) const { return indexOf(m_matrices, id); }

ObjectId PlotModel::insertMatrix(Matrix m, int index)
{
    if (m.id == kInvalidId) m.id = newId();
    else m_lastId = std::max(m_lastId, m.id);
    // Invalid grids (wrong size, negative dims, non-finite extent) become empty.
    const bool finiteExtent = std::isfinite(m.x0) && std::isfinite(m.x1) && std::isfinite(m.y0) &&
                              std::isfinite(m.y1);
    if (m.nx < 0 || m.ny < 0 || m.z.size() != qsizetype(m.nx) * m.ny || !finiteExtent) {
        m.nx = m.ny = 0;
        m.z.clear();
    }
    const ObjectId id = m.id;
    insertAt(m_matrices, std::move(m), index);
    emit matrixAdded(id);
    return id;
}

std::optional<Matrix> PlotModel::takeMatrix(ObjectId id)
{
    auto m = take(m_matrices, id);
    if (m) emit matrixRemoved(id);
    return m;
}

bool PlotModel::setMatrixAppearance(ObjectId id, const QString& name, const ContourStyle& style)
{
    const int i = matrixIndex(id);
    if (i < 0) return false;
    Matrix& m = m_matrices[i];
    if (m.name == name && m.style == style) return true;
    m.name = name;
    m.style = style;
    emit matrixAppearanceChanged(id);
    return true;
}

bool PlotModel::setMatrixData(ObjectId id, int nx, int ny, QVector<double> z, double x0, double x1,
                              double y0, double y1)
{
    const int i = matrixIndex(id);
    if (i < 0 || nx < 0 || ny < 0 || z.size() != qsizetype(nx) * ny) return false;
    if (!std::isfinite(x0) || !std::isfinite(x1) || !std::isfinite(y0) || !std::isfinite(y1)) return false;
    Matrix& m = m_matrices[i];
    m.nx = nx;
    m.ny = ny;
    m.z = std::move(z);
    m.x0 = x0;
    m.x1 = x1;
    m.y0 = y0;
    m.y1 = y1;
    emit matrixDataChanged(id);
    return true;
}

// ---- Markers ------------------------------------------------------------

const Marker* PlotModel::marker(ObjectId id) const { return find(m_markers, id); }
int PlotModel::markerIndex(ObjectId id) const { return indexOf(m_markers, id); }

ObjectId PlotModel::insertMarker(Marker m, int index)
{
    if (m.id == kInvalidId) m.id = newId();
    else m_lastId = std::max(m_lastId, m.id);
    const ObjectId id = m.id;
    insertAt(m_markers, std::move(m), index);
    emit markerAdded(id);
    return id;
}

std::optional<Marker> PlotModel::takeMarker(ObjectId id)
{
    auto m = take(m_markers, id);
    if (m) emit markerRemoved(id);
    return m;
}

bool PlotModel::updateMarker(const Marker& m)
{
    const int i = markerIndex(m.id);
    if (i < 0) return false;
    if (m_markers[i] == m) return true;
    m_markers[i] = m;
    emit markerChanged(m.id);
    return true;
}

// ---- Annotations --------------------------------------------------------

const Annotation* PlotModel::annotation(ObjectId id) const { return find(m_annotations, id); }
int PlotModel::annotationIndex(ObjectId id) const { return indexOf(m_annotations, id); }

ObjectId PlotModel::insertAnnotation(Annotation a, int index)
{
    if (a.id == kInvalidId) a.id = newId();
    else m_lastId = std::max(m_lastId, a.id);
    const ObjectId id = a.id;
    insertAt(m_annotations, std::move(a), index);
    emit annotationAdded(id);
    return id;
}

std::optional<Annotation> PlotModel::takeAnnotation(ObjectId id)
{
    auto a = take(m_annotations, id);
    if (a) emit annotationRemoved(id);
    return a;
}

bool PlotModel::updateAnnotation(const Annotation& a)
{
    const int i = annotationIndex(a.id);
    if (i < 0) return false;
    if (m_annotations[i] == a) return true;
    m_annotations[i] = a;
    emit annotationChanged(a.id);
    return true;
}

// ---- Peaks ----------------------------------------------------------------

const Peak* PlotModel::peak(ObjectId id) const { return find(m_peaks, id); }
int PlotModel::peakIndex(ObjectId id) const { return indexOf(m_peaks, id); }

ObjectId PlotModel::insertPeak(Peak p, int index)
{
    if (p.id == kInvalidId) p.id = newId();
    else m_lastId = std::max(m_lastId, p.id);
    const ObjectId id = p.id;
    insertAt(m_peaks, std::move(p), index);
    emit peakAdded(id);
    return id;
}

std::optional<Peak> PlotModel::takePeak(ObjectId id)
{
    auto p = take(m_peaks, id);
    if (p) emit peakRemoved(id);
    return p;
}

bool PlotModel::updatePeak(const Peak& p)
{
    const int i = peakIndex(p.id);
    if (i < 0) return false;
    if (m_peaks[i] == p) return true;
    m_peaks[i] = p;
    emit peakChanged(p.id);
    return true;
}

// ---- Region / axes ----------------------------------------------------------

void PlotModel::setRegion(const Region& rIn)
{
    Region r = rIn;
    if (r.xmin > r.xmax) std::swap(r.xmin, r.xmax);
    if (!r.active) r = Region{};
    if (r == m_region) return;
    m_region = r;
    emit regionChanged();
}

void PlotModel::setStyle(const Style& s)
{
    if (s == m_style) return;
    m_style = s;
    emit styleChanged();
}

void PlotModel::setAxesInfo(const AxesInfo& info)
{
    if (info == m_axes) return;
    m_axes = info;
    emit axesInfoChanged();
}

bool PlotModel::dataBounds(double& xmin, double& xmax, double& ymin, double& ymax,
                           bool visibleOnly, bool xPositive, bool yPositive) const
{
    bool any = false;
    xmin = ymin = std::numeric_limits<double>::infinity();
    xmax = ymax = -std::numeric_limits<double>::infinity();
    // A hidden line that a visible fill uses as its other edge (fill_between
    // bands) still bounds what is drawn.
    QSet<ObjectId> fillEdges;
    for (const auto& d : m_datasets)
        if (d.style.visible && d.style.fillColor.isValid() && d.style.fillTo != kInvalidId) fillEdges.insert(d.style.fillTo);
    for (const auto& d : m_datasets) {
        if (visibleOnly && !d.style.visible && !fillEdges.contains(d.id)) continue;
        auto err = [](const QVector<double>& v, qsizetype i) {
            return i < v.size() && std::isfinite(v[i]) ? std::abs(v[i]) : 0.0;
        };
        bool thisAny = false;  // this dataset contributed points
        for (qsizetype i = 0; i < d.x.size(); ++i) {
            const double x = d.x[i], y = d.y[i];
            if (!std::isfinite(x) || !std::isfinite(y)) continue;
            if ((xPositive && x <= 0) || (yPositive && y <= 0)) continue;
            // Error bars are part of the data extent (not below zero on log axes).
            const double xl = x - err(d.xErrLow, i), yl = y - err(d.yErrLow, i);
            xmin = std::min(xmin, xPositive && xl <= 0 ? x : xl);
            xmax = std::max(xmax, x + err(d.xErrHigh, i));
            ymin = std::min(ymin, yPositive && yl <= 0 ? y : yl);
            ymax = std::max(ymax, y + err(d.yErrHigh, i));
            any = thisAny = true;
        }
        // Areas filled to zero (histograms) include the baseline.
        if (thisAny && !yPositive && d.style.fillColor.isValid() && d.style.fillTo == kInvalidId) {
            ymin = std::min(ymin, 0.0);
            ymax = std::max(ymax, 0.0);
        }
    }
    for (const auto& m : m_matrices) {
        if ((visibleOnly && !m.style.visible) || m.nx < 1 || m.ny < 1) continue;
        double x0 = std::min(m.x0, m.x1), x1 = std::max(m.x0, m.x1);
        double y0 = std::min(m.y0, m.y1), y1 = std::max(m.y0, m.y1);
        if (m.style.showImage) {
            // An image covers whole cells: half a cell beyond the outer centres.
            const double hx = m.nx > 1 ? (x1 - x0) / (m.nx - 1) / 2 : 0.5;
            const double hy = m.ny > 1 ? (y1 - y0) / (m.ny - 1) / 2 : 0.5;
            x0 -= hx, x1 += hx, y0 -= hy, y1 += hy;
        }
        // On logarithmic axes only the positive part of the extent counts.
        if (xPositive) x0 = std::max(x0, x1 * 1e-6);
        if (yPositive) y0 = std::max(y0, y1 * 1e-6);
        if ((xPositive && !(x1 > 0)) || (yPositive && !(y1 > 0))) continue;
        xmin = std::min(xmin, x0);
        xmax = std::max(xmax, x1);
        ymin = std::min(ymin, y0);
        ymax = std::max(ymax, y1);
        any = true;
    }
    return any;
}

}  // namespace gp
