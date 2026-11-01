#include "PeakTable.h"

#include "commands/PlotEditor.h"
#include "interaction/InteractionController.h"
#include "model/PeakPicker.h"
#include "model/PlotModel.h"
#include "render/PlotRenderer.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QUndoStack>
#include <QClipboard>
#include <QGuiApplication>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace gp {

namespace {
enum Col { ColX, ColY, ColIntensity, ColLabel, ColComment, ColCount };
constexpr int kIdRole = Qt::UserRole + 1;

// Numeric item that sorts by value, not by text.
class NumItem : public QTableWidgetItem {
public:
    explicit NumItem(double v) : QTableWidgetItem(QString::number(v, 'f', v != 0 && std::abs(v) < 0.01 ? 6 : 4))
    {
        setData(Qt::UserRole, v);
        setFlags(flags() & ~Qt::ItemIsEditable);
        setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    bool operator<(const QTableWidgetItem& o) const override
    {
        return data(Qt::UserRole).toDouble() < o.data(Qt::UserRole).toDouble();
    }
};
}  // namespace

PeakTable::PeakTable(PlotEditor* editor, PlotRenderer* renderer, InteractionController* controller,
                     QWidget* parent)
    : QWidget(parent), m_editor(editor), m_model(editor->model()), m_renderer(renderer),
      m_controller(controller)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    // ---- automatic picking controls
    auto* form = new QFormLayout;
    m_threshold = new QLineEdit;
    m_threshold->setPlaceholderText("lowest contour level");
    auto* v = new QDoubleValidator(m_threshold);
    v->setBottom(0);
    v->setNotation(QDoubleValidator::ScientificNotation);
    m_threshold->setValidator(v);
    m_threshold->setToolTip("Minimum peak height. Empty = the current lowest contour level,\n"
                            "so exactly the peaks you can see are picked.");
    m_minDist = new QSpinBox;
    m_minDist->setRange(1, 100);
    m_minDist->setValue(3);
    m_minDist->setSuffix(" points");
    m_minDist->setToolTip("Minimum separation between two peaks, in data points");
    m_diagonal = new QDoubleSpinBox;
    m_diagonal->setRange(0, 1000);
    m_diagonal->setDecimals(3);
    m_diagonal->setSingleStep(0.05);
    m_diagonal->setValue(0.0);
    m_diagonal->setSpecialValueText("off");
    m_diagonal->setToolTip("Skip peaks closer than this to the diagonal (|x - y|, in axis units).\n"
                           "Use it to pick only off-diagonal cross peaks.");
    m_negative = new QCheckBox("Negative peaks too");
    m_viewOnly = new QCheckBox("Visible area only");
    m_viewOnly->setChecked(true);
    form->addRow("Threshold", m_threshold);
    form->addRow("Min. distance", m_minDist);
    form->addRow("Skip diagonal ±", m_diagonal);
    auto* opts = new QHBoxLayout;
    opts->addWidget(m_negative);
    opts->addWidget(m_viewOnly);
    form->addRow(opts);
    layout->addLayout(form);

    auto* buttons = new QHBoxLayout;
    auto* pick = new QPushButton("Pick peaks");
    pick->setToolTip("Automatic peak picking on the 2D data (one undo step)");
    auto* del = new QPushButton("Delete selected");
    auto* clear = new QPushButton("Delete all");
    buttons->addWidget(pick);
    buttons->addWidget(del);
    buttons->addWidget(clear);
    layout->addLayout(buttons);

    // ---- table
    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels({"x", "y", "Intensity", "Label", "Comment"});
    QHeaderView* hh = m_table->horizontalHeader();
    for (int c : {ColX, ColY, ColIntensity}) hh->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(ColLabel, QHeaderView::Interactive);
    hh->setSectionResizeMode(ColComment, QHeaderView::Stretch);
    m_table->setColumnWidth(ColLabel, 80);
    m_table->verticalHeader()->setVisible(false);
    setMinimumWidth(340);
    m_table->verticalHeader()->setDefaultSectionSize(22);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setSortingEnabled(true);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                             QAbstractItemView::AnyKeyPressed);
    m_table->setToolTip("Double-click a label or comment to edit it; double-click a position to "
                        "centre the view on the peak.");
    layout->addWidget(m_table, 1);

    auto* io = new QHBoxLayout;
    m_count = new QLabel;
    auto* exp = new QPushButton("Export CSV…");
    auto* imp = new QPushButton("Import CSV…");
    io->addWidget(m_count, 1);
    io->addWidget(imp);
    io->addWidget(exp);
    layout->addLayout(io);

    connect(pick, &QPushButton::clicked, this, &PeakTable::pickNow);
    connect(del, &QPushButton::clicked, this, [this] {
        QList<ObjectId> ids;
        for (const auto& r : m_table->selectionModel()->selectedRows())
            ids << m_table->item(r.row(), ColX)->data(kIdRole).toULongLong();
        m_editor->removePeaks(ids);
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        QList<ObjectId> ids;
        for (const Peak& p : m_model->peaks()) ids << p.id;
        m_editor->removePeaks(ids, "Delete all peaks");
    });
    connect(exp, &QPushButton::clicked, this, [this] {
        const QString fn = QFileDialog::getSaveFileName(this, "Export peak list", "peaks.csv",
                                                        "CSV files (*.csv)");
        QString err;
        if (!fn.isEmpty() && !exportCsv(fn, &err)) QMessageBox::warning(this, "Export peaks", err);
    });
    connect(imp, &QPushButton::clicked, this, [this] {
        const QString fn = QFileDialog::getOpenFileName(this, "Import peak list", QString(),
                                                        "CSV files (*.csv);;All files (*)");
        QString err;
        if (!fn.isEmpty() && !importCsv(fn, &err)) QMessageBox::warning(this, "Import peaks", err);
    });
    connect(m_table, &QTableWidget::cellChanged, this, &PeakTable::onCellChanged);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        if (m_updating) return;
        const auto rows = m_table->selectionModel()->selectedRows();
        if (rows.size() == 1)
            m_controller->setSelection(
                {ObjectKind::Peak, m_table->item(rows[0].row(), ColX)->data(kIdRole).toULongLong()});
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (col <= ColIntensity) centreOn(m_table->item(row, ColX)->data(kIdRole).toULongLong());
    });

    // Picking adds hundreds of peaks at once: rebuild once per event-loop pass.
    auto scheduleRebuild = [this] {
        if (m_rebuildPending) return;
        m_rebuildPending = true;
        QTimer::singleShot(0, this, [this] {
            m_rebuildPending = false;
            rebuild();
        });
    };
    connect(m_model, &PlotModel::peakAdded, this, scheduleRebuild);
    connect(m_model, &PlotModel::peakRemoved, this, scheduleRebuild);

    // Keyboard: Delete removes the selected peaks, Ctrl+C copies them as TSV.
    auto* delKey = new QShortcut(QKeySequence::Delete, m_table, nullptr, nullptr, Qt::WidgetShortcut);
    connect(delKey, &QShortcut::activated, this, [this] {
        QList<ObjectId> ids;
        for (const auto& r : m_table->selectionModel()->selectedRows())
            ids << m_table->item(r.row(), ColX)->data(kIdRole).toULongLong();
        m_editor->removePeaks(ids);
    });
    auto* copy = new QShortcut(QKeySequence::Copy, m_table, nullptr, nullptr, Qt::WidgetShortcut);
    connect(copy, &QShortcut::activated, this, [this] {
        QStringList lines{"x\ty\tintensity\tlabel\tcomment"};
        QList<int> rows;
        for (const auto& r : m_table->selectionModel()->selectedRows()) rows << r.row();
        if (rows.isEmpty())
            for (int r = 0; r < m_table->rowCount(); ++r) rows << r;  // nothing selected: all
        std::sort(rows.begin(), rows.end());
        for (int r : rows) {
            const Peak* p = m_model->peak(m_table->item(r, ColX)->data(kIdRole).toULongLong());
            if (!p) continue;
            QString comment = p->comment;
            comment.replace('\n', ' ');
            lines << QString("%1\t%2\t%3\t%4\t%5").arg(p->x, 0, 'g', 10).arg(p->y, 0, 'g', 10)
                         .arg(p->intensity, 0, 'g', 8).arg(p->label, comment);
        }
        QGuiApplication::clipboard()->setText(lines.join('\n') + '\n');
    });
    connect(m_model, &PlotModel::peakChanged, this, &PeakTable::refreshRow);
    connect(m_controller, &InteractionController::selectionChanged, this,
            &PeakTable::syncSelectionFromController);
    rebuild();
}

void PeakTable::rebuild()
{
    m_updating = true;
    m_table->setSortingEnabled(false);
    m_table->setRowCount(int(m_model->peaks().size()));
    int row = 0;
    for (const Peak& p : m_model->peaks()) {
        auto* x = new NumItem(p.x);
        x->setData(kIdRole, QVariant::fromValue<qulonglong>(p.id));
        m_table->setItem(row, ColX, x);
        m_table->setItem(row, ColY, new NumItem(p.y));
        m_table->setItem(row, ColIntensity, new NumItem(p.intensity));
        m_table->setItem(row, ColLabel, new QTableWidgetItem(p.label));
        auto* c = new QTableWidgetItem(p.comment);
        c->setToolTip(p.comment);
        m_table->setItem(row, ColComment, c);
        ++row;
    }
    m_table->setSortingEnabled(true);
    m_count->setText(QString("%1 peaks").arg(m_model->peaks().size()));
    m_updating = false;
    syncSelectionFromController(m_controller->selection());
}

int PeakTable::rowOf(ObjectId id) const
{
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (m_table->item(r, ColX) && m_table->item(r, ColX)->data(kIdRole).toULongLong() == id)
            return r;
    return -1;
}

void PeakTable::refreshRow(ObjectId id)
{
    const Peak* p = m_model->peak(id);
    const int row = rowOf(id);
    if (!p || row < 0) return;
    m_updating = true;
    m_table->setSortingEnabled(false);
    auto setNum = [&](int col, double v) {
        auto* it = new NumItem(v);
        if (col == ColX) it->setData(kIdRole, QVariant::fromValue<qulonglong>(id));
        m_table->setItem(row, col, it);
    };
    setNum(ColX, p->x);
    setNum(ColY, p->y);
    setNum(ColIntensity, p->intensity);
    m_table->item(row, ColLabel)->setText(p->label);
    m_table->item(row, ColComment)->setText(p->comment);
    m_table->item(row, ColComment)->setToolTip(p->comment);
    m_table->setSortingEnabled(true);
    m_updating = false;
}

void PeakTable::onCellChanged(int row, int col)
{
    if (m_updating || (col != ColLabel && col != ColComment)) return;
    const ObjectId id = m_table->item(row, ColX)->data(kIdRole).toULongLong();
    const Peak* cur = m_model->peak(id);
    if (!cur) return;
    Peak p = *cur;
    (col == ColLabel ? p.label : p.comment) = m_table->item(row, col)->text();
    m_editor->updatePeak(p, std::nullopt, false, col == ColLabel ? "Edit peak label" : "Edit peak comment");
}

void PeakTable::syncSelectionFromController(const ObjectRef& ref)
{
    if (m_updating) return;
    m_updating = true;
    if (ref.kind == ObjectKind::Peak) {
        const int row = rowOf(ref.id);
        if (row >= 0) {
            m_table->selectRow(row);
            m_table->scrollToItem(m_table->item(row, ColX));
        }
    } else {
        m_table->clearSelection();
    }
    m_updating = false;
}

void PeakTable::centreOn(ObjectId id)
{
    const Peak* p = m_model->peak(id);
    if (!p) return;
    ViewRange v = m_renderer->viewRange();
    const double w = v.xmax - v.xmin, h = v.ymax - v.ymin;
    v = {p->x - w / 2, p->x + w / 2, p->y - h / 2, p->y + h / 2};
    m_renderer->setViewRange(v);
    m_controller->setSelection({ObjectKind::Peak, id});
}

void PeakTable::pickNow()
{
    PeakPickOptions o;
    bool ok = false;
    const double t = m_threshold->text().toDouble(&ok);
    o.threshold = ok ? t : 0.0;
    o.minDistance = m_minDist->value();
    o.negative = m_negative->isChecked();
    o.diagonalExclusion = m_diagonal->value();
    if (m_viewOnly->isChecked()) {
        const ViewRange v = m_renderer->viewRange();
        o.restrictToRect = true;
        o.xmin = v.xmin;
        o.xmax = v.xmax;
        o.ymin = v.ymin;
        o.ymax = v.ymax;
    }
    const QList<ObjectId> targets = m_renderer->levelTargets();
    if (targets.isEmpty()) {
        QMessageBox::information(this, "Pick peaks", "Peak picking needs 2D data (a contour plot).");
        return;
    }
    m_editor->undoStack()->beginMacro("Pick peaks");
    int n = 0;
    for (ObjectId id : targets) n += int(m_editor->pickPeaks(id, o).size());
    m_editor->undoStack()->endMacro();
    m_count->setText(QString("%1 peaks (%2 new)").arg(m_model->peaks().size()).arg(n));
}

bool PeakTable::exportCsv(const QString& fileName, QString* error)
{
    const AxesInfo& a = m_model->axesInfo();
    return writePeakCsv(fileName, m_model->peaks(), a.xLabel.isEmpty() ? "x" : a.xLabel,
                        a.yLabel.isEmpty() ? "y" : a.yLabel, error);
}

bool PeakTable::importCsv(const QString& fileName, QString* error)
{
    QString err;
    std::vector<Peak> peaks = readPeakCsv(fileName, &err);
    if (!err.isEmpty()) {
        if (error) *error = err;
        return false;
    }
    m_editor->addPeaks(std::move(peaks), "Import peaks");
    return true;
}

}  // namespace gp
