// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "PlotEditor.h"

#include "Commands.h"
#include "model/Contour.h"

#include <QHash>
#include <QPointF>
#include <QUndoStack>
#include <algorithm>
#include <cmath>

namespace gp {

PlotEditor::PlotEditor(PlotModel* model, QObject* parent)
    : QObject(parent), m_model(model), m_stack(new QUndoStack(this))
{
    m_stack->setUndoLimit(500);
}

PlotEditor::PlotEditor(PlotModel* model, QUndoStack* sharedStack, QObject* parent)
    : QObject(parent), m_model(model), m_stack(sharedStack)
{
}

ObjectId PlotEditor::addDataset(Dataset d)
{
    if (d.id == kInvalidId) d.id = m_model->allocateId();
    if (!d.style.color.isValid()) d.style.color = m_model->takeNextDatasetColor();
    if (!(d.style.lineWidth > 0)) d.style.lineWidth = m_model->style().lineWidth;
    if (!(d.style.scatterSize > 0)) d.style.scatterSize = m_model->style().markerSize;
    if (d.name.isEmpty()) {
        // "Dataset N" with the first N not in use (counting would repeat a
        // name after a removal).
        for (int n = int(m_model->datasets().size()) + 1;; ++n) {
            const QString name = QString("Dataset %1").arg(n);
            if (!m_model->datasetByName(name)) {
                d.name = name;
                break;
            }
        }
    }
    const ObjectId id = d.id;
    m_stack->push(new AddDatasetCommand(m_model, std::move(d)));
    return id;
}

void PlotEditor::removeDataset(ObjectId id)
{
    if (m_model->dataset(id)) m_stack->push(new RemoveDatasetCommand(m_model, id));
}

void PlotEditor::setDatasetAppearance(ObjectId id, const QString& name, const DatasetStyle& style,
                                      bool mergeable)
{
    const Dataset* cur = m_model->dataset(id);
    if (!cur || (cur->name == name && cur->style == style)) return;
    // Only name/style matter for the edit; drop the arrays from the copies.
    Dataset before;
    before.id = id;
    before.name = cur->name;
    before.style = cur->style;
    Dataset after = before;
    after.name = name;
    after.style = style;
    m_stack->push(new EditDatasetCommand(m_model, before, after, "Change dataset appearance",
                                         mergeable));
}

void PlotEditor::setDatasetData(ObjectId id, QVector<double> x, QVector<double> y, bool undoable)
{
    if (!m_model->dataset(id)) return;
    if (undoable) m_stack->push(new SetDatasetDataCommand(m_model, id, std::move(x), std::move(y)));
    else m_model->setDatasetData(id, std::move(x), std::move(y));
}

void PlotEditor::setDatasetDataAndErrors(ObjectId id, const Dataset& src)
{
    if (!m_model->dataset(id)) return;
    auto* cmd = new SetDatasetDataCommand(m_model, id, src.x, src.y);
    cmd->setNewErrors(src.xErrLow, src.xErrHigh, src.yErrLow, src.yErrHigh);
    m_stack->push(cmd);
}

ObjectId PlotEditor::addMatrix(Matrix m)
{
    if (m.id == kInvalidId) m.id = m_model->allocateId();
    if (!m.style.positiveColor.isValid()) m.style.positiveColor = m_model->takeNextDatasetColor();
    const Style& st = m_model->style();
    if (m.style.levelCount <= 0) m.style.levelCount = st.contourLevels;
    if (!(m.style.factor > 1)) m.style.factor = st.contourFactor;
    if (!(m.style.lineWidth > 0)) m.style.lineWidth = st.contourLineWidth;
    if (!m.style.negativeColor.isValid()) m.style.negativeColor = st.contourNegativeColor;
    if (!(m.style.baseLevel > 0))
        m.style.baseLevel = defaultBaseLevel(m, m.style.factor, m.style.levelCount);
    if (m.name.isEmpty()) m.name = QString("2D data %1").arg(m_model->matrices().size() + 1);
    const ObjectId id = m.id;
    m_stack->push(new AddMatrixCommand(m_model, std::move(m)));
    return id;
}

void PlotEditor::removeMatrix(ObjectId id)
{
    if (!m_model->matrix(id)) return;
    // Its peaks go with it, in the same undo step (undo restores both).
    QList<ObjectId> peaks;
    for (const Peak& p : m_model->peaks())
        if (p.matrixId == id) peaks << p.id;
    m_stack->beginMacro("Remove 2D dataset");
    for (ObjectId p : peaks) m_stack->push(new RemovePeakCommand(m_model, p));
    m_stack->push(new RemoveMatrixCommand(m_model, id));
    m_stack->endMacro();
}

void PlotEditor::setMatrixAppearance(ObjectId id, const QString& name, const ContourStyle& style,
                                     bool mergeable)
{
    const Matrix* cur = m_model->matrix(id);
    if (!cur || (cur->name == name && cur->style == style)) return;
    Matrix before;  // name/style only - the grid is not needed for the edit
    before.id = id;
    before.name = cur->name;
    before.style = cur->style;
    Matrix after = before;
    after.name = name;
    after.style = style;
    m_stack->push(new EditMatrixCommand(m_model, before, after, "Change contour settings",
                                        mergeable));
}

void PlotEditor::setMatrixData(ObjectId id, int nx, int ny, QVector<double> z, double x0,
                               double x1, double y0, double y1)
{
    // Grid updates are typically streamed from analysis code: not undoable.
    m_model->setMatrixData(id, nx, ny, std::move(z), x0, x1, y0, y1);
}

void PlotEditor::scaleContourLevels(const QList<ObjectId>& ids, double factor)
{
    if (!(factor > 0)) return;
    std::vector<ScaleLevelsCommand::Change> changes;
    for (ObjectId id : ids) {
        const Matrix* cur = m_model->matrix(id);
        if (!cur) continue;
        ScaleLevelsCommand::Change c;
        c.before.id = id;
        c.before.name = cur->name;
        c.before.style = cur->style;
        c.after = c.before;
        // Clamp: a level of exactly 0 (underflow) could never be scaled back.
        c.after.style.baseLevel = std::clamp(cur->style.baseLevel * factor, 1e-300, 1e300);
        for (double& l : c.after.style.levels) l *= factor;  // explicit levels scroll too
        changes.push_back(std::move(c));
    }
    if (!changes.empty()) m_stack->push(new ScaleLevelsCommand(m_model, std::move(changes)));
}

ObjectId PlotEditor::addPeak(Peak p)
{
    if (p.id == kInvalidId) p.id = m_model->allocateId();
    const ObjectId id = p.id;
    m_stack->push(new AddPeakCommand(m_model, std::move(p)));
    return id;
}

QList<ObjectId> PlotEditor::addPeaks(std::vector<Peak> peaks, const QString& text)
{
    QList<ObjectId> ids;
    if (peaks.empty()) return ids;
    m_stack->beginMacro(text);
    for (Peak& p : peaks) ids.push_back(addPeak(std::move(p)));
    m_stack->endMacro();
    return ids;
}

QList<ObjectId> PlotEditor::pickPeaks(ObjectId matrixId, PeakPickOptions opt)
{
    return pickPeaks(QList<ObjectId>{matrixId}, opt);
}

QList<ObjectId> PlotEditor::pickPeaks(const QList<ObjectId>& matrixIds, const PeakPickOptions& opt)
{
    std::vector<Peak> fresh;
    for (ObjectId id : matrixIds) {
        std::vector<Peak> p = newPeaks(id, opt);
        std::move(p.begin(), p.end(), std::back_inserter(fresh));
    }
    return addPeaks(std::move(fresh), "Pick peaks");
}

std::vector<Peak> PlotEditor::newPeaks(ObjectId matrixId, PeakPickOptions opt) const
{
    const Matrix* m = m_model->matrix(matrixId);
    if (!m) return {};
    if (!(opt.threshold > 0)) {
        opt.threshold = m->style.baseLevel;
        if (opt.negative && !m->style.showNegative) opt.negative = false;
    }
    std::vector<Peak> found = gp::pickPeaks(*m, opt);
    // Skip peaks that duplicate existing ones.
    const double dx = std::abs(m->x1 - m->x0) / std::max(1, m->nx - 1);
    const double dy = std::abs(m->y1 - m->y0) / std::max(1, m->ny - 1);
    const double r = std::max(1, opt.minDistance);
    // Existing peaks in buckets of r x r grid points: a candidate only needs
    // to be compared with the peaks in its own and the 8 neighbouring buckets.
    const double bx = std::max(r * dx, 1e-300), by = std::max(r * dy, 1e-300);
    auto key = [](qint64 i, qint64 j) { return (i << 32) ^ (j & 0xffffffff); };
    QHash<qint64, QVector<QPointF>> buckets;
    for (const Peak& e : m_model->peaks())
        // Only peaks of this spectrum (or imported ones without one) count:
        // overlaid spectra may have peaks at the same position.
        if (e.matrixId == matrixId || e.matrixId == kInvalidId)
            buckets[key(qint64(std::floor(e.x / bx)), qint64(std::floor(e.y / by)))] << QPointF(e.x, e.y);
    std::vector<Peak> fresh;
    for (Peak& p : found) {
        const qint64 i = qint64(std::floor(p.x / bx)), j = qint64(std::floor(p.y / by));
        bool dup = false;
        for (qint64 di = -1; di <= 1 && !dup; ++di)
            for (qint64 dj = -1; dj <= 1 && !dup; ++dj)
                for (const QPointF& e : buckets.value(key(i + di, j + dj)))
                    if (std::abs(e.x() - p.x) < r * dx && std::abs(e.y() - p.y) < r * dy) {
                        dup = true;
                        break;
                    }
        if (!dup) fresh.push_back(std::move(p));
    }
    return fresh;
}

void PlotEditor::removePeak(ObjectId id)
{
    if (m_model->peak(id)) m_stack->push(new RemovePeakCommand(m_model, id));
}

void PlotEditor::removePeaks(const QList<ObjectId>& ids, const QString& text)
{
    if (ids.isEmpty()) return;
    m_stack->beginMacro(text);
    for (ObjectId id : ids) removePeak(id);
    m_stack->endMacro();
}

void PlotEditor::updatePeak(const Peak& after, std::optional<Peak> before, bool mergeable,
                            const QString& text)
{
    const Peak* cur = m_model->peak(after.id);
    if (!cur) return;
    const Peak b = before.value_or(*cur);
    if (b == after) return;
    m_stack->push(new EditPeakCommand(m_model, b, after, text, mergeable));
}

ObjectId PlotEditor::addMarker(Marker m)
{
    if (m.id == kInvalidId) m.id = m_model->allocateId();
    const ObjectId id = m.id;
    m_stack->push(new AddMarkerCommand(m_model, std::move(m)));
    return id;
}

void PlotEditor::removeMarker(ObjectId id)
{
    if (m_model->marker(id)) m_stack->push(new RemoveMarkerCommand(m_model, id));
}

void PlotEditor::updateMarker(const Marker& after, std::optional<Marker> before, bool mergeable,
                              const QString& text)
{
    const Marker* cur = m_model->marker(after.id);
    if (!cur) return;
    const Marker b = before.value_or(*cur);
    if (b == after) return;
    m_stack->push(new EditMarkerCommand(m_model, b, after, text, mergeable));
}

ObjectId PlotEditor::addAnnotation(Annotation a)
{
    if (a.id == kInvalidId) a.id = m_model->allocateId();
    const ObjectId id = a.id;
    m_stack->push(new AddAnnotationCommand(m_model, std::move(a)));
    return id;
}

void PlotEditor::removeAnnotation(ObjectId id)
{
    if (m_model->annotation(id)) m_stack->push(new RemoveAnnotationCommand(m_model, id));
}

void PlotEditor::updateAnnotation(const Annotation& after, std::optional<Annotation> before,
                                  bool mergeable, const QString& text)
{
    const Annotation* cur = m_model->annotation(after.id);
    if (!cur) return;
    const Annotation b = before.value_or(*cur);
    if (b == after) return;
    m_stack->push(new EditAnnotationCommand(m_model, b, after, text, mergeable));
}

void PlotEditor::setRegion(const Region& afterIn, std::optional<Region> before)
{
    Region after = afterIn;
    if (after.xmin > after.xmax) std::swap(after.xmin, after.xmax);
    if (!after.active) after = Region{};
    const Region b = before.value_or(m_model->region());
    if (b == after) {
        m_model->setRegion(after);  // make sure a preview is in its final state
        return;
    }
    m_stack->push(new SetRegionCommand(m_model, b, after));
}

void PlotEditor::setStyle(const Style& style, const QString& text)
{
    if (style == m_model->style()) return;
    // The per-figure grid toggle follows the style's axes.grid.
    m_stack->beginMacro(text);
    m_stack->push(new SetStyleCommand(m_model, m_model->style(), style, text));
    AxesInfo a = m_model->axesInfo();
    a.gridVisible = style.grid;
    setAxesInfo(a);
    m_stack->endMacro();
}

void PlotEditor::setAxesInfo(const AxesInfo& info)
{
    if (info == m_model->axesInfo()) return;
    m_stack->push(new SetAxesInfoCommand(m_model, m_model->axesInfo(), info));
}

void PlotEditor::removeObject(const ObjectRef& ref)
{
    switch (ref.kind) {
    case ObjectKind::Dataset: removeDataset(ref.id); break;
    case ObjectKind::Marker: removeMarker(ref.id); break;
    case ObjectKind::Annotation: removeAnnotation(ref.id); break;
    case ObjectKind::Region: clearRegion(); break;
    case ObjectKind::Matrix: removeMatrix(ref.id); break;
    case ObjectKind::Peak: removePeak(ref.id); break;
    case ObjectKind::None: break;
    }
}

void PlotEditor::undo() { m_stack->undo(); }
void PlotEditor::redo() { m_stack->redo(); }

}  // namespace gp
