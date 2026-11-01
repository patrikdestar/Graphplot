// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
//
// Side panel for timeline figures:
//  * Rows: drag rows to reorder, drag a signal onto another row to move it,
//    tick to show/hide, edit names and relative heights, add/remove rows.
//  * Navigator signal, auto-fit Y and clock-time options.
//  * Time markers: list (time, label), add at the cursor, delete, jump to.
#pragma once

#include "Timeline.h"

#include <QTreeWidget>
#include <QWidget>

class QComboBox;
class QCheckBox;
class QTableWidget;
class QLabel;

namespace gp {

class RowTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit RowTree(Timeline* tl, QWidget* parent = nullptr);

signals:
    void rearranged();

protected:
    void dropEvent(QDropEvent* event) override;

private:
    Timeline* m_tl;
};

class TimelinePanel : public QWidget {
    Q_OBJECT
public:
    explicit TimelinePanel(Timeline* tl, QWidget* parent = nullptr);

private:
    void rebuildRows();
    void rebuildSignals();
    void rebuildPins();
    void onRowItemChanged(QTreeWidgetItem* item, int column);

    Timeline* m_tl;
    RowTree* m_rows = nullptr;
    QComboBox* m_nav = nullptr;
    QCheckBox* m_autoFit = nullptr;
    QCheckBox* m_clock = nullptr;
    QTableWidget* m_pins = nullptr;
    QLabel* m_cursorLabel = nullptr;
    bool m_updating = false;
};

}  // namespace gp
