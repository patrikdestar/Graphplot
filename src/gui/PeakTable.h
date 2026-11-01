// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
// PeakTable: dock widget listing all peaks with position, intensity, label
// and comment. Label and comment cells are editable (undoable); selecting a
// row selects the peak on the plot; double-clicking a position centres the
// view on the peak. Also hosts the automatic picking controls and CSV
// import/export.
#pragma once

#include "model/PlotTypes.h"

#include <QWidget>

class QTableWidget;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QLabel;

namespace gp {

class PlotEditor;
class PlotModel;
class PlotRenderer;
class InteractionController;

class PeakTable : public QWidget {
    Q_OBJECT
public:
    PeakTable(PlotEditor* editor, PlotRenderer* renderer, InteractionController* controller,
              QWidget* parent = nullptr);

    // Runs automatic picking with the options shown in the widget.
    void pickNow();
    bool exportCsv(const QString& fileName, QString* error = nullptr);
    bool importCsv(const QString& fileName, QString* error = nullptr);

private:
    void rebuild();
    void refreshRow(ObjectId id);
    int rowOf(ObjectId id) const;
    void onCellChanged(int row, int col);
    void syncSelectionFromController(const ObjectRef& ref);
    void centreOn(ObjectId id);

    PlotEditor* m_editor;
    PlotModel* m_model;
    PlotRenderer* m_renderer;
    InteractionController* m_controller;
    QTableWidget* m_table = nullptr;
    QLineEdit* m_threshold = nullptr;
    QSpinBox* m_minDist = nullptr;
    QCheckBox* m_negative = nullptr;
    QDoubleSpinBox* m_diagonal = nullptr;
    QCheckBox* m_viewOnly = nullptr;
    QLabel* m_count = nullptr;
    bool m_updating = false;
    bool m_rebuildPending = false;
};

}  // namespace gp
