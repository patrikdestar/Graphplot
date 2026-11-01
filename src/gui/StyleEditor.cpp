#include "StyleEditor.h"


#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace gp {

StyleEditor::StyleEditor(const Style& initial, std::function<void(const Style&)> apply, QWidget* parent)
    : QDialog(parent), m_base(initial), m_apply(std::move(apply))
{
    setWindowTitle("Edit style");
    resize(760, 640);
    auto* layout = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText("Filter keys (e.g. tick, legend, font)…");
    m_filter->setClearButtonEnabled(true);
    auto* base = new QComboBox;
    base->addItem("Start from…");
    base->addItems(builtinStyleNames());
    top->addWidget(m_filter, 1);
    top->addWidget(base);
    layout->addLayout(top);

    m_table = new QTableWidget(0, 3);
    m_table->setHorizontalHeaderLabels({"Key", "Value", "Description"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setColumnWidth(1, 220);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(22);
    layout->addWidget(m_table, 1);

    m_status = new QLabel(
        "Edit values, then Apply. Colours: names or #rrggbb. Texts may use $math$, **bold**, *italic*.");
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply |
                                         QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        Style s;
        if (collect(&s)) {
            m_apply(s);
            accept();
        }
    });
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &StyleEditor::apply);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& f) {
        for (int r = 0; r < m_table->rowCount(); ++r) {
            const bool show = f.isEmpty() || m_table->item(r, 0)->text().contains(f, Qt::CaseInsensitive) ||
                              m_table->item(r, 2)->text().contains(f, Qt::CaseInsensitive);
            m_table->setRowHidden(r, !show);
        }
    });
    connect(base, &QComboBox::activated, this, [this, base](int i) {
        Style s;
        if (i > 0 && builtinStyle(base->itemText(i), &s)) load(s);
        base->setCurrentIndex(0);
    });
    load(initial);
}

void StyleEditor::load(const Style& s)
{
    const auto& keys = Style::keys();
    m_table->setRowCount(int(keys.size()));
    for (int r = 0; r < keys.size(); ++r) {
        auto* k = new QTableWidgetItem(keys[r].key);
        k->setFlags(k->flags() & ~Qt::ItemIsEditable);
        auto* d = new QTableWidgetItem(keys[r].description);
        d->setFlags(d->flags() & ~Qt::ItemIsEditable);
        d->setForeground(QColor(0x52, 0x51, 0x4e));
        m_table->setItem(r, 0, k);
        m_table->setItem(r, 1, new QTableWidgetItem(s.value(keys[r].key)));
        m_table->setItem(r, 2, d);
    }
    m_filter->textChanged(m_filter->text());
}

bool StyleEditor::collect(Style* out)
{
    Style s = m_base;
    QStringList errors;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QString err;
        QTableWidgetItem* v = m_table->item(r, 1);
        if (!s.set(m_table->item(r, 0)->text(), v->text(), &err)) {
            v->setBackground(QColor(255, 220, 220));
            errors << m_table->item(r, 0)->text() + ": " + err;
        } else {
            v->setBackground(Qt::NoBrush);
        }
    }
    if (!errors.isEmpty()) {
        for (QString& e : errors) e = e.toHtmlEscaped();
        m_status->setText("<span style='color:#c0392b'>" + errors.join("<br>") + "</span>");
        return false;
    }
    *out = s;
    return true;
}

void StyleEditor::apply()
{
    Style s;
    if (collect(&s)) m_apply(s);
}

}  // namespace gp
