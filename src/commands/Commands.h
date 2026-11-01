// Undoable editing commands (QUndoCommand) operating on a PlotModel.
//
// Each command stores only the state it needs to revert itself (e.g. one
// marker before/after an edit), never a snapshot of the whole plot. Dataset
// arrays are QVector (implicitly shared), so keeping a removed dataset in a
// command does not copy its data.
//
// Three generic templates cover datasets, markers and annotations; the
// per-type differences are captured in small "traits" structs.
#pragma once

#include "model/PlotModel.h"

#include <QUndoCommand>

namespace gp {

enum CommandId {
    CmdEditDataset = 1001,
    CmdEditMarker,
    CmdEditAnnotation,
    CmdSetRegion,
};

// ---- Traits -----------------------------------------------------------------

struct DatasetTraits {
    using Type = Dataset;
    static constexpr const char* kName = "Dataset";
    static constexpr int kEditId = CmdEditDataset;
    static const Type* get(PlotModel& m, ObjectId id) { return m.dataset(id); }
    static int index(PlotModel& m, ObjectId id) { return m.datasetIndex(id); }
    static void insert(PlotModel& m, const Type& v, int i) { m.insertDataset(v, i); }
    static std::optional<Type> take(PlotModel& m, ObjectId id) { return m.takeDataset(id); }
    // Dataset edits change appearance only; data updates use SetDatasetDataCommand.
    static void update(PlotModel& m, const Type& v) { m.setDatasetAppearance(v.id, v.name, v.style); }
};

struct MarkerTraits {
    using Type = Marker;
    static constexpr const char* kName = "Marker";
    static constexpr int kEditId = CmdEditMarker;
    static const Type* get(PlotModel& m, ObjectId id) { return m.marker(id); }
    static int index(PlotModel& m, ObjectId id) { return m.markerIndex(id); }
    static void insert(PlotModel& m, const Type& v, int i) { m.insertMarker(v, i); }
    static std::optional<Type> take(PlotModel& m, ObjectId id) { return m.takeMarker(id); }
    static void update(PlotModel& m, const Type& v) { m.updateMarker(v); }
};

struct AnnotationTraits {
    using Type = Annotation;
    static constexpr const char* kName = "Annotation";
    static constexpr int kEditId = CmdEditAnnotation;
    static const Type* get(PlotModel& m, ObjectId id) { return m.annotation(id); }
    static int index(PlotModel& m, ObjectId id) { return m.annotationIndex(id); }
    static void insert(PlotModel& m, const Type& v, int i) { m.insertAnnotation(v, i); }
    static std::optional<Type> take(PlotModel& m, ObjectId id) { return m.takeAnnotation(id); }
    static void update(PlotModel& m, const Type& v) { m.updateAnnotation(v); }
};

// ---- Generic commands -------------------------------------------------------

// Adds an object. The object must already carry a valid id so that redo
// after undo recreates the same identity (handles in Python stay valid).
template <typename Traits>
class AddCommand : public QUndoCommand {
public:
    AddCommand(PlotModel* model, typename Traits::Type obj, int index = -1,
               QUndoCommand* parent = nullptr)
        : QUndoCommand(parent), m_model(model), m_obj(std::move(obj)), m_index(index)
    {
        setText(QString("Add %1").arg(Traits::kName).toLower());
    }
    void redo() override { Traits::insert(*m_model, m_obj, m_index); }
    void undo() override
    {
        if (auto o = Traits::take(*m_model, m_obj.id)) m_obj = std::move(*o);
    }

private:
    PlotModel* m_model;
    typename Traits::Type m_obj;
    int m_index;
};

template <typename Traits>
class RemoveCommand : public QUndoCommand {
public:
    RemoveCommand(PlotModel* model, ObjectId id, QUndoCommand* parent = nullptr)
        : QUndoCommand(parent), m_model(model), m_id(id)
    {
        setText(QString("Remove %1").arg(Traits::kName).toLower());
    }
    void redo() override
    {
        m_index = Traits::index(*m_model, m_id);
        if (auto o = Traits::take(*m_model, m_id)) m_obj = std::move(*o);
        else setObsolete(true);
    }
    void undo() override { Traits::insert(*m_model, m_obj, m_index); }

private:
    PlotModel* m_model;
    ObjectId m_id;
    typename Traits::Type m_obj;
    int m_index = -1;
};

// Changes an object's properties (or position). Consecutive edits of the
// same object made while "mergeable" is true (e.g. spin box steps, typing
// in a property editor) collapse into one undo step.
template <typename Traits>
class EditCommand : public QUndoCommand {
public:
    EditCommand(PlotModel* model, typename Traits::Type before, typename Traits::Type after,
                const QString& text, bool mergeable, QUndoCommand* parent = nullptr)
        : QUndoCommand(text, parent), m_model(model), m_before(std::move(before)),
          m_after(std::move(after)), m_mergeable(mergeable)
    {
    }
    void redo() override { Traits::update(*m_model, m_after); }
    void undo() override { Traits::update(*m_model, m_before); }
    int id() const override { return m_mergeable ? Traits::kEditId : -1; }
    bool mergeWith(const QUndoCommand* other) override
    {
        auto* o = static_cast<const EditCommand*>(other);
        if (!o->m_mergeable || o->m_after.id != m_after.id || o->text() != text()) return false;
        m_after = o->m_after;
        return true;
    }

private:
    PlotModel* m_model;
    typename Traits::Type m_before;
    typename Traits::Type m_after;
    bool m_mergeable;
};

using AddDatasetCommand = AddCommand<DatasetTraits>;
using RemoveDatasetCommand = RemoveCommand<DatasetTraits>;
using EditDatasetCommand = EditCommand<DatasetTraits>;
using AddMarkerCommand = AddCommand<MarkerTraits>;
using RemoveMarkerCommand = RemoveCommand<MarkerTraits>;
using EditMarkerCommand = EditCommand<MarkerTraits>;
using AddAnnotationCommand = AddCommand<AnnotationTraits>;
using RemoveAnnotationCommand = RemoveCommand<AnnotationTraits>;
using EditAnnotationCommand = EditCommand<AnnotationTraits>;

// ---- Specific commands --------------------------------------------------------

class SetDatasetDataCommand : public QUndoCommand {
public:
    SetDatasetDataCommand(PlotModel* model, ObjectId id, QVector<double> x, QVector<double> y,
                          QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    PlotModel* m_model;
    ObjectId m_id;
    QVector<double> m_newX, m_newY, m_oldX, m_oldY;
};

class SetRegionCommand : public QUndoCommand {
public:
    SetRegionCommand(PlotModel* model, Region before, Region after, QUndoCommand* parent = nullptr);
    void redo() override { m_model->setRegion(m_after); }
    void undo() override { m_model->setRegion(m_before); }

private:
    PlotModel* m_model;
    Region m_before, m_after;
};

class SetAxesInfoCommand : public QUndoCommand {
public:
    SetAxesInfoCommand(PlotModel* model, AxesInfo before, AxesInfo after,
                       QUndoCommand* parent = nullptr)
        : QUndoCommand("Change axes", parent), m_model(model), m_before(std::move(before)),
          m_after(std::move(after))
    {
    }
    void redo() override { m_model->setAxesInfo(m_after); }
    void undo() override { m_model->setAxesInfo(m_before); }

private:
    PlotModel* m_model;
    AxesInfo m_before, m_after;
};

}  // namespace gp
