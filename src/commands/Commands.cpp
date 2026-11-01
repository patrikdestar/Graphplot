#include "Commands.h"

namespace gp {

SetDatasetDataCommand::SetDatasetDataCommand(PlotModel* model, ObjectId id, QVector<double> x,
                                             QVector<double> y, QUndoCommand* parent)
    : QUndoCommand("Update dataset data", parent), m_model(model), m_id(id),
      m_newX(std::move(x)), m_newY(std::move(y))
{
    if (const Dataset* d = model->dataset(id)) {
        m_oldX = d->x;  // implicitly shared, no copy
        m_oldY = d->y;
        m_oldXErrLow = d->xErrLow;
        m_oldXErrHigh = d->xErrHigh;
        m_oldYErrLow = d->yErrLow;
        m_oldYErrHigh = d->yErrHigh;
    }
}

void SetDatasetDataCommand::redo() { m_model->setDatasetData(m_id, m_newX, m_newY); }

void SetDatasetDataCommand::undo()
{
    m_model->setDatasetData(m_id, m_oldX, m_oldY);
    m_model->setDatasetErrors(m_id, m_oldXErrLow, m_oldXErrHigh, m_oldYErrLow, m_oldYErrHigh);
}

SetRegionCommand::SetRegionCommand(PlotModel* model, Region before, Region after,
                                   QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_before(before), m_after(after)
{
    setText(after.active ? (before.active ? "Modify selection" : "Select region")
                         : "Clear selection");
}

}  // namespace gp
