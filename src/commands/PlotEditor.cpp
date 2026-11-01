#include "PlotEditor.h"

#include "Commands.h"
#include "model/Contour.h"

#include <QUndoStack>

namespace gp {

PlotEditor::PlotEditor(PlotModel* model, QObject* parent)
    : QObject(parent), m_model(model), m_stack(new QUndoStack(this))
{
    m_stack->setUndoLimit(500);
}

ObjectId PlotEditor::addDataset(Dataset d)
{
    if (d.id == kInvalidId) d.id = m_model->allocateId();
    if (!d.style.color.isValid()) d.style.color = m_model->takeNextDatasetColor();
    if (d.name.isEmpty()) d.name = QString("Dataset %1").arg(m_model->datasets().size() + 1);
    const ObjectId id = d.id;
    auto* cmd = new AddDatasetCommand(m_model, std::move(d));
    cmd->setText(QString("Add dataset"));
    m_stack->push(cmd);
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

ObjectId PlotEditor::addMatrix(Matrix m)
{
    if (m.id == kInvalidId) m.id = m_model->allocateId();
    if (!m.style.positiveColor.isValid()) m.style.positiveColor = m_model->takeNextDatasetColor();
    if (!(m.style.baseLevel > 0))
        m.style.baseLevel = defaultBaseLevel(m, m.style.factor, m.style.levelCount);
    if (m.name.isEmpty()) m.name = QString("2D data %1").arg(m_model->matrices().size() + 1);
    const ObjectId id = m.id;
    m_stack->push(new AddMatrixCommand(m_model, std::move(m)));
    return id;
}

void PlotEditor::removeMatrix(ObjectId id)
{
    if (m_model->matrix(id)) m_stack->push(new RemoveMatrixCommand(m_model, id));
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
    if (!(factor > 0) || ids.isEmpty()) return;
    const bool macro = ids.size() > 1;
    if (macro) m_stack->beginMacro("Scale contour levels");
    for (ObjectId id : ids) {
        const Matrix* cur = m_model->matrix(id);
        if (!cur) continue;
        ContourStyle s = cur->style;
        s.baseLevel *= factor;
        Matrix before;
        before.id = id;
        before.name = cur->name;
        before.style = cur->style;
        Matrix after = before;
        after.style = s;
        // Mergeable, so a continuous wheel scroll becomes one undo step.
        m_stack->push(new EditMatrixCommand(m_model, before, after, "Scale contour levels",
                                            !macro));
    }
    if (macro) m_stack->endMacro();
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
    case ObjectKind::None: break;
    }
}

void PlotEditor::undo() { m_stack->undo(); }
void PlotEditor::redo() { m_stack->redo(); }

}  // namespace gp
