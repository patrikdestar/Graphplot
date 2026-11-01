// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "TimelinePanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDropEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

namespace gp {

namespace {
constexpr int kRowRole = Qt::UserRole + 1;   // panel index
constexpr int kIdRole = Qt::UserRole + 2;    // dataset id (signal items only)
}  // namespace

// ---- RowTree: drag & drop rearranging -----------------------------------------

RowTree::RowTree(Timeline* tl, QWidget* parent) : QTreeWidget(parent), m_tl(tl)
{
    setColumnCount(2);
    setHeaderLabels({"Rows and signals", "Height"});
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    setDragDropMode(QAbstractItemView::InternalMove);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDropIndicatorShown(true);
    setToolTip("Drag a row to reorder it; drag a signal onto another row to move it there.\n"
               "Tick/untick to show or hide a row. Double-click a name or height to edit it.");
}

void RowTree::dropEvent(QDropEvent* event)
{
    // Apply the change to the timeline ourselves (the tree is rebuilt from it),
    // instead of letting QTreeWidget move items around.
    QTreeWidgetItem* dragged = currentItem();
    QTreeWidgetItem* target = itemAt(event->position().toPoint());
    event->setDropAction(Qt::IgnoreAction);
    event->accept();
    if (!dragged || !target || dragged == target) return;
    const bool draggedIsRow = dragged->parent() == nullptr;
    QTreeWidgetItem* targetRowItem = target->parent() ? target->parent() : target;
    const int targetRow = targetRowItem->data(0, kRowRole).toInt();
    if (draggedIsRow) {
        QList<int> order = m_tl->rows();
        const int moving = dragged->data(0, kRowRole).toInt();
        order.removeAll(moving);
        int idx = order.indexOf(targetRow);
        const auto pos = dropIndicatorPosition();
        if (pos == QAbstractItemView::BelowItem || (target->parent() && pos != QAbstractItemView::AboveItem))
            ++idx;
        order.insert(std::clamp<qsizetype>(idx, 0, order.size()), moving);
        m_tl->setRowOrder(order);
    } else {
        const SignalRef s{dragged->data(0, kRowRole).toInt(), dragged->data(0, kIdRole).toULongLong()};
        m_tl->moveSignal(s, targetRow);
    }
    emit rearranged();
}

// ---- TimelinePanel ------------------------------------------------------------------

TimelinePanel::TimelinePanel(Timeline* tl, QWidget* parent) : QWidget(parent), m_tl(tl)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* rowsBox = new QGroupBox("Rows");
    auto* rl = new QVBoxLayout(rowsBox);
    m_rows = new RowTree(tl);
    m_rows->setMinimumHeight(170);
    rl->addWidget(m_rows, 1);
    auto* rb = new QHBoxLayout;
    auto* up = new QPushButton("▲");
    auto* down = new QPushButton("▼");
    up->setToolTip("Move the selected row up");
    down->setToolTip("Move the selected row down");
    up->setMaximumWidth(36);
    down->setMaximumWidth(36);
    auto* add = new QPushButton("New row");
    add->setToolTip("Add an empty row; then drag signals onto it");
    auto* remove = new QPushButton("Remove empty row");
    rb->addWidget(up);
    rb->addWidget(down);
    rb->addWidget(add);
    rb->addWidget(remove);
    rl->addLayout(rb);
    layout->addWidget(rowsBox, 2);

    auto* viewBox = new QGroupBox("View");
    auto* form = new QFormLayout(viewBox);
    m_nav = new QComboBox;
    m_nav->setToolTip("Signal shown in the navigator strip at the bottom");
    m_autoFit = new QCheckBox("Auto-fit Y to the visible window");
    m_clock = new QCheckBox("Clock time (instead of seconds)");
    m_cursorLabel = new QLabel("–");
    m_cursorLabel->setStyleSheet("color: #52514e;");
    form->addRow("Navigator", m_nav);
    form->addRow(m_autoFit);
    form->addRow(m_clock);
    form->addRow("Cursor", m_cursorLabel);
    layout->addWidget(viewBox);

    auto* pinBox = new QGroupBox("Time markers");
    auto* pl = new QVBoxLayout(pinBox);
    m_pins = new QTableWidget(0, 2);
    m_pins->setHorizontalHeaderLabels({"Time", "Label"});
    m_pins->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_pins->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_pins->verticalHeader()->setVisible(false);
    m_pins->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pins->setToolTip("Double-click a time to jump there; double-click a label to rename.\n"
                       "In the plot: Ctrl+click or M adds a marker, drag it to move, right-click for options.");
    pl->addWidget(m_pins, 1);
    auto* delKey = new QShortcut(QKeySequence::Delete, m_pins, nullptr, nullptr, Qt::WidgetShortcut);
    auto* pb = new QHBoxLayout;
    auto* addPin = new QPushButton("Add at cursor (M)");
    auto* delPin = new QPushButton("Delete");
    pb->addWidget(addPin);
    pb->addWidget(delPin);
    pl->addLayout(pb);
    layout->addWidget(pinBox, 1);

    // ---- wiring
    auto selectedRow = [this]() -> int {
        QTreeWidgetItem* it = m_rows->currentItem();
        if (!it) return -1;
        if (it->parent()) it = it->parent();
        return it->data(0, kRowRole).toInt();
    };
    auto shift = [this, selectedRow](int delta) {
        const int row = selectedRow();
        QList<int> order = m_tl->rows();
        const int i = int(order.indexOf(row));
        if (i < 0 || i + delta < 0 || i + delta >= order.size()) return;
        order.move(i, i + delta);
        m_tl->setRowOrder(order);
    };
    connect(up, &QPushButton::clicked, this, [shift] { shift(-1); });
    connect(down, &QPushButton::clicked, this, [shift] { shift(+1); });
    connect(add, &QPushButton::clicked, this, [this] { m_tl->addRow(QString("Row %1").arg(m_tl->rows().size() + 1)); });
    connect(remove, &QPushButton::clicked, this, [this, selectedRow] { m_tl->removeRowIfEmpty(selectedRow()); });
    connect(m_rows, &QTreeWidget::itemChanged, this, &TimelinePanel::onRowItemChanged);
    connect(m_rows, &RowTree::rearranged, this, &TimelinePanel::rebuildRows);
    connect(m_nav, &QComboBox::activated, this, [this](int i) {
        const QVariantList v = m_nav->itemData(i).toList();
        if (v.size() == 2) m_tl->setNavigatorSignal({v[0].toInt(), v[1].toULongLong()});
    });
    connect(m_autoFit, &QCheckBox::toggled, m_tl, &Timeline::setAutoFitY);
    connect(m_clock, &QCheckBox::toggled, m_tl, &Timeline::setClockTime);
    connect(addPin, &QPushButton::clicked, this, [this] {
        const auto [a, b] = m_tl->window();
        m_tl->addPin(m_tl->cursorTime().value_or(0.5 * (a + b)));
    });
    auto deleteSelectedPin = [this] {
        const int r = m_pins->currentRow();
        if (r >= 0) m_tl->removePin(m_pins->item(r, 0)->data(Qt::UserRole).toULongLong());
    };
    connect(delPin, &QPushButton::clicked, this, deleteSelectedPin);
    connect(delKey, &QShortcut::activated, this, deleteSelectedPin);
    connect(m_pins, &QTableWidget::cellDoubleClicked, this, [this](int r, int c) {
        if (c == 0) m_tl->jumpToPin(m_pins->item(r, 0)->data(Qt::UserRole).toULongLong());
    });
    connect(m_pins, &QTableWidget::cellChanged, this, [this](int r, int c) {
        if (m_updating || c != 1) return;
        const ObjectId id = m_pins->item(r, 0)->data(Qt::UserRole).toULongLong();
        for (const TimePin& p : m_tl->pins())
            if (p.id == id) {
                TimePin q = p;
                q.label = m_pins->item(r, 1)->text();
                m_tl->updatePin(q);
            }
    });

    connect(m_tl, &Timeline::rowsChanged, this, &TimelinePanel::rebuildRows);
    connect(m_tl, &Timeline::signalsChanged, this, [this] {
        rebuildRows();
        rebuildSignals();
    });
    // Queued: a pin edit made from a table cell must not delete that cell's item.
    connect(m_tl, &Timeline::pinsChanged, this, &TimelinePanel::rebuildPins, Qt::QueuedConnection);
    connect(m_tl, &Timeline::optionsChanged, this, [this] {
        const QSignalBlocker b1(m_clock), b2(m_autoFit);
        m_clock->setEnabled(m_tl->hasAbsoluteTime());
        m_clock->setChecked(m_tl->clockTime());
        m_autoFit->setChecked(m_tl->autoFitY());
        rebuildPins();  // time format
    });
    connect(m_tl, &Timeline::cursorChanged, this, [this](std::optional<double> t) {
        const auto [a, b] = m_tl->window();
        m_cursorLabel->setText(t ? m_tl->formatTime(*t, (b - a) / 1000.0) : QStringLiteral("–"));
    });
    // Pan/zoom only changes the time format: update texts, keep any edit open.
    connect(m_tl, &Timeline::windowChanged, this, [this] {
        const auto [a, b] = m_tl->window();
        m_updating = true;
        for (int r = 0; r < m_pins->rowCount(); ++r) {
            const ObjectId id = m_pins->item(r, 0)->data(Qt::UserRole).toULongLong();
            for (const TimePin& p : m_tl->pins())
                if (p.id == id) m_pins->item(r, 0)->setText(m_tl->formatTime(p.t, (b - a) / 1000.0));
        }
        m_updating = false;
    });
    rebuildRows();
    rebuildSignals();
    rebuildPins();
}

void TimelinePanel::rebuildRows()
{
    m_updating = true;
    const int keepRow = m_rows->currentItem() ? (m_rows->currentItem()->parent()
                                                     ? m_rows->currentItem()->parent()
                                                     : m_rows->currentItem())->data(0, kRowRole).toInt()
                                              : -1;
    m_rows->clear();
    for (int row : m_tl->rows()) {
        auto* it = new QTreeWidgetItem(m_rows);
        it->setText(0, m_tl->rowName(row).isEmpty() ? QString("Row %1").arg(row + 1) : m_tl->rowName(row));
        it->setText(1, QString::number(m_tl->rowHeight(row), 'g', 3));
        it->setData(0, kRowRole, row);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsUserCheckable |
                     Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        it->setCheckState(0, m_tl->rowVisible(row) ? Qt::Checked : Qt::Unchecked);
        QFont f = it->font(0);
        f.setBold(true);
        it->setFont(0, f);
        for (const SignalRef& s : m_tl->signalList()) {
            if (s.row != row) continue;
            auto* c = new QTreeWidgetItem(it);
            c->setText(0, m_tl->signalName(s).section(" / ", 1));
            c->setData(0, kRowRole, row);
            c->setData(0, kIdRole, QVariant::fromValue<qulonglong>(s.id));
            c->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        }
        it->setExpanded(true);
        if (row == keepRow) m_rows->setCurrentItem(it);
    }
    m_clock->setEnabled(m_tl->hasAbsoluteTime());
    m_clock->setChecked(m_tl->clockTime());
    m_autoFit->setChecked(m_tl->autoFitY());
    m_updating = false;
}

void TimelinePanel::rebuildSignals()
{
    m_nav->clear();
    for (const SignalRef& s : m_tl->signalList()) {
        m_nav->addItem(m_tl->signalName(s), QVariantList{s.row, QVariant::fromValue<qulonglong>(s.id)});
        if (s == m_tl->navigatorSignal()) m_nav->setCurrentIndex(m_nav->count() - 1);
    }
}

void TimelinePanel::rebuildPins()
{
    m_updating = true;
    const auto [a, b] = m_tl->window();
    m_pins->setRowCount(int(m_tl->pins().size()));
    int r = 0;
    for (const TimePin& p : m_tl->pins()) {
        auto* t = new QTableWidgetItem(m_tl->formatTime(p.t, (b - a) / 1000.0));
        t->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(p.id));
        t->setFlags(t->flags() & ~Qt::ItemIsEditable);
        t->setForeground(p.color);
        m_pins->setItem(r, 0, t);
        m_pins->setItem(r, 1, new QTableWidgetItem(p.label));
        ++r;
    }
    m_updating = false;
}

void TimelinePanel::onRowItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_updating || item->parent()) return;
    // Copy everything first: the changes below rebuild the tree, which
    // deletes `item`. Apply them after Qt has finished with the item.
    const int row = item->data(0, kRowRole).toInt();
    const bool visible = item->checkState(0) == Qt::Checked;
    const QString name = item->text(0);
    const QString height = item->text(1);
    QMetaObject::invokeMethod(this, [this, row, column, visible, name, height] {
        if (column == 0) {
            // Only a real rename changes the name (not the "Row N" placeholder).
            const QString shown = m_tl->rowName(row).isEmpty() ? QString("Row %1").arg(row + 1)
                                                               : m_tl->rowName(row);
            if (name != shown) m_tl->setRowName(row, name);
            m_tl->setRowVisible(row, visible);
        } else if (column == 1) {
            bool ok = false;
            const double h = height.toDouble(&ok);
            if (ok && h > 0) m_tl->setRowHeight(row, h);
            else rebuildRows();
        }
    }, Qt::QueuedConnection);
}

}  // namespace gp
