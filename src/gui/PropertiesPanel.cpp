// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "PropertiesPanel.h"

#include "model/DataExport.h"
#include "render/ColorBar.h"

#include "commands/PlotEditor.h"
#include "interaction/InteractionController.h"
#include "model/Contour.h"
#include "model/PlotModel.h"
#include "model/Statistics.h"
#include "render/PlotRenderer.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QDoubleValidator>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>

namespace gp {

namespace {

// Spin boxes clamp and round what they display. To never change a value the
// user didn't touch (e.g. a level factor of 1.005 shown as 1.01), each box
// remembers the value it showed after loading, and a commit writes the box
// value back only if it differs from that.
template <typename Box, typename T>
void showValue(Box* box, T v)
{
    box->setValue(v);
    box->setProperty("gpShown", box->value());
}

template <typename Box, typename T>
T edited(const Box* box, T modelValue)
{
    const QVariant shown = box->property("gpShown");
    if (shown.isValid() && shown.value<T>() == T(box->value())) return modelValue;
    return T(box->value());
}

constexpr int kKindRole = Qt::UserRole + 1;
constexpr int kIdRole = Qt::UserRole + 2;

QIcon swatchIcon(const QColor& c, int w = 16, int h = 10)
{
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(c.darker(130), 1));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(0.5, 0.5, w - 1, h - 1), 2, 2);
    return QIcon(pm);
}

// Spin box for data coordinates: keeps full double precision (a plain
// QDoubleSpinBox rounds to its decimals, so editing only a label would
// silently move an object at 5e-7 to 0) and shows values compactly.
class CoordSpinBox : public QDoubleSpinBox {
public:
    CoordSpinBox()
    {
        setDecimals(17);
        setRange(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
    }
    QString textFromValue(double v) const override { return QString::number(v, 'g', 12); }
    double valueFromText(const QString& text) const override { return text.toDouble(); }
    QValidator::State validate(QString& text, int&) const override
    {
        bool ok = false;
        text.toDouble(&ok);
        if (ok) return QValidator::Acceptable;
        // Allow partial input such as "-", "1e", "2.5e-".
        static const QRegularExpression partial(R"(^[+-]?(\d*\.?\d*)([eE][+-]?\d*)?$)");
        return partial.match(text).hasMatch() ? QValidator::Intermediate : QValidator::Invalid;
    }
};

QDoubleSpinBox* makeCoordSpin()
{
    auto* s = new CoordSpinBox;
    s->setKeyboardTracking(false);  // commit on Enter / focus-out / arrow steps
    s->setSingleStep(0.1);
    s->setMinimumWidth(110);
    return s;
}

QComboBox* makeLineStyleCombo(bool allowNone)
{
    auto* c = new QComboBox;
    c->addItem("Solid", int(LineStyle::Solid));
    c->addItem("Dashed", int(LineStyle::Dash));
    c->addItem("Dotted", int(LineStyle::Dot));
    c->addItem("Dash-dot", int(LineStyle::DashDot));
    if (allowNone) c->addItem("None", int(LineStyle::None));
    return c;
}

void setComboData(QComboBox* c, int value)
{
    const int i = c->findData(value);
    if (i >= 0) c->setCurrentIndex(i);
}

QString fmt(double v) { return QString::number(v, 'g', 6); }

}  // namespace

// ---- ColorButton ---------------------------------------------------------------

ColorButton::ColorButton(QWidget* parent) : QToolButton(parent)
{
    setIconSize(QSize(28, 14));
    connect(this, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_color, this, "Choose colour",
                                                QColorDialog::ShowAlphaChannel);
        if (c.isValid() && c != m_color) {
            setColor(c);
            emit colorPicked(c);
        }
    });
}

void ColorButton::setColor(const QColor& c)
{
    m_color = c;
    setIcon(swatchIcon(c, 28, 14));
    setToolTip(c.name(QColor::HexArgb));
}

// ---- PropertiesPanel -----------------------------------------------------------

PropertiesPanel::PropertiesPanel(PlotEditor* editor, InteractionController* controller,
                                 QWidget* parent)
    : QWidget(parent), m_editor(editor), m_model(editor->model()), m_controller(controller)
{
    buildUi();
    rebuildTree();
    refreshAxes();
    refreshLimits();
    // The panel is rebuilt whenever the current figure panel changes, so the
    // controller's renderer is this panel's renderer for its whole life.
    connect(m_controller->renderer(), &PlotRenderer::viewChanged, this, &PropertiesPanel::refreshLimits);

    auto structural = [this] { rebuildTree(); };
    auto changed = [this](ObjectKind kind) {
        return [this, kind](ObjectId id) {
            refreshTreeItem({kind, id});
            if (m_current == ObjectRef{kind, id}) refreshCurrentPage();
        };
    };
    connect(m_model, &PlotModel::datasetAdded, this, structural);
    connect(m_model, &PlotModel::datasetRemoved, this, structural);
    connect(m_model, &PlotModel::matrixAdded, this, structural);
    connect(m_model, &PlotModel::matrixRemoved, this, structural);
    connect(m_model, &PlotModel::matrixAppearanceChanged, this, changed(ObjectKind::Matrix));
    connect(m_model, &PlotModel::matrixDataChanged, this, changed(ObjectKind::Matrix));
    connect(m_model, &PlotModel::markerAdded, this, structural);
    connect(m_model, &PlotModel::markerRemoved, this, structural);
    connect(m_model, &PlotModel::annotationAdded, this, structural);
    connect(m_model, &PlotModel::annotationRemoved, this, structural);
    connect(m_model, &PlotModel::datasetAppearanceChanged, this, changed(ObjectKind::Dataset));
    connect(m_model, &PlotModel::datasetDataChanged, this, changed(ObjectKind::Dataset));
    connect(m_model, &PlotModel::markerChanged, this, changed(ObjectKind::Marker));
    connect(m_model, &PlotModel::peakChanged, this, changed(ObjectKind::Peak));
    connect(m_model, &PlotModel::annotationChanged, this, changed(ObjectKind::Annotation));
    connect(m_model, &PlotModel::regionChanged, this, [this] {
        rebuildTree();
        if (m_current.kind == ObjectKind::Region) refreshCurrentPage();
    });
    connect(m_model, &PlotModel::axesInfoChanged, this, &PropertiesPanel::refreshAxes);
    connect(m_model, &PlotModel::styleChanged, this, structural);  // e.g. annotation.color swatches

    connect(m_controller, &InteractionController::selectionChanged, this,
            [this](ObjectRef ref) { showObject(ref); });
}

void PropertiesPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    // Axes group
    auto* axesBox = new QGroupBox("Axes");
    auto* axesForm = new QFormLayout(axesBox);
    m_title = new QLineEdit;
    m_xLabel = new QLineEdit;
    m_yLabel = new QLineEdit;
    m_legend = new QCheckBox("Legend");
    m_grid = new QCheckBox("Grid");
    auto* checks = new QHBoxLayout;
    checks->addWidget(m_legend);
    checks->addWidget(m_grid);
    m_xLog = new QCheckBox("Log X");
    m_yLog = new QCheckBox("Log Y");
    checks->addWidget(m_xLog);
    checks->addWidget(m_yLog);
    checks->addStretch();
    m_xRev = new QCheckBox("Reverse X");
    m_yRev = new QCheckBox("Reverse Y");
    m_diag = new QCheckBox("Diagonal");
    m_xRev->setToolTip("High values on the left (NMR ppm convention)");
    m_diag->setToolTip("Dashed y = x line (homonuclear 2D spectra)");
    auto* checks2 = new QHBoxLayout;
    checks2->addWidget(m_xRev);
    checks2->addWidget(m_yRev);
    checks2->addWidget(m_diag);
    checks2->addStretch();
    axesForm->addRow("Title", m_title);
    axesForm->addRow("X label", m_xLabel);
    axesForm->addRow("Y label", m_yLabel);
    axesForm->addRow(checks);
    axesForm->addRow(checks2);
    // Typed view limits (follow zoom / pan; editing one sets the view).
    const char* limitNames[] = {"xMin", "xMax", "yMin", "yMax"};
    int li = 0;
    for (QDoubleSpinBox** f : {&m_xMin, &m_xMax, &m_yMin, &m_yMax}) {
        *f = makeCoordSpin();
        (*f)->setObjectName(limitNames[li++]);
        (*f)->setMinimumWidth(80);
        connect(*f, &QDoubleSpinBox::valueChanged, this, &PropertiesPanel::commitLimits);
    }
    auto* xr = new QHBoxLayout;
    xr->addWidget(m_xMin);
    xr->addWidget(m_xMax);
    auto* yr = new QHBoxLayout;
    yr->addWidget(m_yMin);
    yr->addWidget(m_yMax);
    axesForm->addRow("X range", xr);
    axesForm->addRow("Y range", yr);
    for (QLineEdit* e : {m_title, m_xLabel, m_yLabel})
        connect(e, &QLineEdit::editingFinished, this, &PropertiesPanel::commitAxes);
    connect(m_legend, &QCheckBox::toggled, this, &PropertiesPanel::commitAxes);
    for (QCheckBox* c : {m_grid, m_xRev, m_yRev, m_diag, m_xLog, m_yLog})
        connect(c, &QCheckBox::toggled, this, &PropertiesPanel::commitAxes);
    layout->addWidget(axesBox);

    // Object tree
    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setMinimumHeight(160);
    m_datasetsRoot = new QTreeWidgetItem(m_tree, {"Datasets"});
    m_matricesRoot = new QTreeWidgetItem(m_tree, {"2D data"});
    m_markersRoot = new QTreeWidgetItem(m_tree, {"Markers"});
    m_annotationsRoot = new QTreeWidgetItem(m_tree, {"Annotations"});
    for (auto* root : {m_datasetsRoot, m_matricesRoot, m_markersRoot, m_annotationsRoot}) {
        root->setFlags(Qt::ItemIsEnabled);
        QFont f = root->font(0);
        f.setBold(true);
        root->setFont(0, f);
        root->setExpanded(true);
    }
    layout->addWidget(m_tree, 1);

    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* item, QTreeWidgetItem*) {
                if (m_updatingTree) return;
                ObjectRef ref;
                if (item && item->data(0, kKindRole).isValid())
                    ref = {ObjectKind(item->data(0, kKindRole).toInt()),
                           item->data(0, kIdRole).toULongLong()};
                m_controller->setSelection(ref);
                showObject(ref);
            });
    // Checkbox on dataset items toggles visibility (undoable).
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int) {
        if (m_updatingTree) return;
        const ObjectKind kind = ObjectKind(item->data(0, kKindRole).toInt());
        const ObjectId id = item->data(0, kIdRole).toULongLong();
        if (kind == ObjectKind::Matrix) {
            if (const Matrix* m = m_model->matrix(id)) {
                ContourStyle s = m->style;
                s.visible = item->checkState(0) == Qt::Checked;
                m_editor->setMatrixAppearance(id, m->name, s);
            }
            return;
        }
        if (kind != ObjectKind::Dataset) return;
        if (const Dataset* d = m_model->dataset(id)) {
            DatasetStyle s = d->style;
            s.visible = item->checkState(0) == Qt::Checked;
            m_editor->setDatasetAppearance(id, d->name, s);
        }
    });

    m_removeButton = new QPushButton("Remove selected");
    m_removeButton->setEnabled(false);
    connect(m_removeButton, &QPushButton::clicked, this, [this] {
        const ObjectRef ref = m_current;
        m_controller->setSelection({});
        m_editor->removeObject(ref);
    });
    layout->addWidget(m_removeButton);

    // Property pages
    m_stack = new QStackedWidget;
    m_emptyPage = new QLabel("Select an object to edit its properties.");
    static_cast<QLabel*>(m_emptyPage)->setWordWrap(true);
    static_cast<QLabel*>(m_emptyPage)->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    // Muted text from the palette, so it stays readable in every theme.
    static_cast<QLabel*>(m_emptyPage)->setForegroundRole(QPalette::PlaceholderText);
    static_cast<QLabel*>(m_emptyPage)->setMargin(6);
    m_datasetPage = buildDatasetPage();
    m_markerPage = buildMarkerPage();
    m_annotationPage = buildAnnotationPage();
    m_regionPage = buildRegionPage();
    m_matrixPage = buildMatrixPage();
    m_peakPage = buildPeakPage();
    for (QWidget* w : {m_emptyPage, m_datasetPage, m_markerPage, m_annotationPage, m_regionPage,
                       m_matrixPage, m_peakPage})
        m_stack->addWidget(w);
    layout->addWidget(m_stack);
    setMinimumWidth(250);
}

QWidget* PropertiesPanel::buildDatasetPage()
{
    auto* box = new QGroupBox("Dataset");
    auto* form = new QFormLayout(box);
    m_dsName = new QLineEdit;
    m_dsColor = new ColorButton;
    m_dsWidth = new QDoubleSpinBox;
    m_dsWidth->setRange(0.01, 100);
    m_dsWidth->setSingleStep(0.5);
    m_dsWidth->setKeyboardTracking(false);
    m_dsLineStyle = makeLineStyleCombo(true);
    m_dsScatter = new QComboBox;
    for (ScatterShape s : {ScatterShape::None, ScatterShape::Circle, ScatterShape::Square,
                           ScatterShape::Triangle, ScatterShape::TriangleDown, ScatterShape::TriangleLeft,
                           ScatterShape::TriangleRight, ScatterShape::Diamond, ScatterShape::Pentagon,
                           ScatterShape::Hexagon, ScatterShape::Star, ScatterShape::Point, ScatterShape::Cross,
                           ScatterShape::Plus}) {
        QString name = scatterShapeName(s);
        name[0] = name[0].toUpper();
        m_dsScatter->addItem(name, int(s));
    }
    m_dsScatterSize = new QDoubleSpinBox;
    m_dsScatterSize->setRange(0.5, 200);
    m_dsScatterSize->setKeyboardTracking(false);
    m_dsVisible = new QCheckBox("Visible");
    m_dsLegend = new QCheckBox("In legend");
    m_dsStep = new QComboBox;
    for (StepMode sm : {StepMode::None, StepMode::Pre, StepMode::Post, StepMode::Mid})
        m_dsStep->addItem(sm == StepMode::None ? "No steps" : "Steps (" + stepModeName(sm) + ")", int(sm));
    m_dsFill = new QCheckBox("Fill");
    m_dsFillColor = new ColorButton;
    auto* fillRow = new QHBoxLayout;
    fillRow->addWidget(m_dsFill);
    fillRow->addWidget(m_dsFillColor);
    fillRow->addStretch();
    m_dsInfo = new QLabel;
    m_dsInfo->setForegroundRole(QPalette::PlaceholderText);
    form->addRow("Name", m_dsName);
    form->addRow("Colour", m_dsColor);
    form->addRow("Line width", m_dsWidth);
    form->addRow("Line style", m_dsLineStyle);
    form->addRow("Symbol", m_dsScatter);
    form->addRow("Symbol size", m_dsScatterSize);
    form->addRow("Drawing", m_dsStep);
    form->addRow("Area", fillRow);
    auto* visRow = new QHBoxLayout;
    visRow->addWidget(m_dsVisible);
    visRow->addWidget(m_dsLegend);
    visRow->addStretch();
    form->addRow(visRow);
    form->addRow(m_dsInfo);

    connect(m_dsName, &QLineEdit::editingFinished, this, [this] { commitDataset(false); });
    connect(m_dsColor, &ColorButton::colorPicked, this, [this] { commitDataset(false); });
    connect(m_dsWidth, &QDoubleSpinBox::valueChanged, this, [this] { commitDataset(true); });
    connect(m_dsLineStyle, &QComboBox::currentIndexChanged, this, [this] { commitDataset(false); });
    connect(m_dsScatter, &QComboBox::currentIndexChanged, this, [this] { commitDataset(false); });
    connect(m_dsScatterSize, &QDoubleSpinBox::valueChanged, this, [this] { commitDataset(true); });
    connect(m_dsVisible, &QCheckBox::toggled, this, [this] { commitDataset(false); });
    connect(m_dsLegend, &QCheckBox::toggled, this, [this] { commitDataset(false); });
    connect(m_dsStep, &QComboBox::currentIndexChanged, this, [this] { commitDataset(false); });
    connect(m_dsFill, &QCheckBox::toggled, this, [this](bool on) {
        if (on && !m_dsFillColor->color().isValid()) {
            QColor c = m_dsColor->color();
            c.setAlphaF(0.3);
            m_dsFillColor->setColor(c);
        }
        commitDataset(false);
    });
    connect(m_dsFillColor, &ColorButton::colorPicked, this, [this] {
        m_dsFill->setChecked(true);
        commitDataset(false);
    });
    return box;
}

QWidget* PropertiesPanel::buildMarkerPage()
{
    auto* box = new QGroupBox("Marker");
    auto* form = new QFormLayout(box);
    m_mkOrientation = new QLabel;
    m_mkLabel = new QLineEdit;
    m_mkLabel->setObjectName("markerLabel");
    m_mkPosition = makeCoordSpin();
    m_mkColor = new ColorButton;
    m_mkLineStyle = makeLineStyleCombo(false);
    m_mkWidth = new QDoubleSpinBox;
    m_mkWidth->setRange(0.1, 50);
    m_mkWidth->setSingleStep(0.5);
    m_mkWidth->setKeyboardTracking(false);
    m_mkShowValue = new QCheckBox("Show coordinate");
    form->addRow("Orientation", m_mkOrientation);
    form->addRow("Label", m_mkLabel);
    form->addRow("Position", m_mkPosition);
    form->addRow("Colour", m_mkColor);
    form->addRow("Line style", m_mkLineStyle);
    form->addRow("Line width", m_mkWidth);
    form->addRow(m_mkShowValue);

    connect(m_mkLabel, &QLineEdit::editingFinished, this, [this] { commitMarker(false); });
    connect(m_mkPosition, &QDoubleSpinBox::valueChanged, this, [this] { commitMarker(true); });
    connect(m_mkColor, &ColorButton::colorPicked, this, [this] { commitMarker(false); });
    connect(m_mkLineStyle, &QComboBox::currentIndexChanged, this, [this] { commitMarker(false); });
    connect(m_mkWidth, &QDoubleSpinBox::valueChanged, this, [this] { commitMarker(true); });
    connect(m_mkShowValue, &QCheckBox::toggled, this, [this] { commitMarker(false); });
    return box;
}

QWidget* PropertiesPanel::buildAnnotationPage()
{
    auto* box = new QGroupBox("Annotation");
    auto* form = new QFormLayout(box);
    m_anText = new QLineEdit;
    m_anText->setPlaceholderText("Use \\n for a line break");
    m_anX = makeCoordSpin();
    m_anY = makeCoordSpin();
    m_anColor = new ColorButton;
    m_anFontSize = new QDoubleSpinBox;
    m_anFontSize->setRange(1, 400);
    m_anFontSize->setKeyboardTracking(false);
    form->addRow("Text", m_anText);
    form->addRow("X", m_anX);
    form->addRow("Y", m_anY);
    form->addRow("Colour", m_anColor);
    form->addRow("Font size", m_anFontSize);

    connect(m_anText, &QLineEdit::editingFinished, this, [this] { commitAnnotation(false); });
    connect(m_anX, &QDoubleSpinBox::valueChanged, this, [this] { commitAnnotation(true); });
    connect(m_anY, &QDoubleSpinBox::valueChanged, this, [this] { commitAnnotation(true); });
    connect(m_anColor, &ColorButton::colorPicked, this, [this] { commitAnnotation(false); });
    connect(m_anFontSize, &QDoubleSpinBox::valueChanged, this, [this] { commitAnnotation(true); });
    return box;
}

QWidget* PropertiesPanel::buildRegionPage()
{
    auto* box = new QGroupBox("Selected region");
    auto* form = new QFormLayout(box);
    m_rgMin = makeCoordSpin();
    m_rgMax = makeCoordSpin();
    m_rgWidth = new QLabel;
    auto* clear = new QPushButton("Clear selection");
    form->addRow("X min", m_rgMin);
    form->addRow("X max", m_rgMax);
    form->addRow("Width", m_rgWidth);
    // Statistics of every visible dataset inside the region.
    m_rgStats = new QLabel;
    m_rgStats->setTextFormat(Qt::RichText);
    m_rgStats->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_rgStats->setWordWrap(true);
    form->addRow(m_rgStats);
    auto* copy = new QPushButton("Copy statistics");
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(copy);
    buttons->addWidget(clear);
    form->addRow(buttons);
    connect(copy, &QPushButton::clicked, this, [this] {
        const Region r = m_model->region();
        if (r.active) QGuiApplication::clipboard()->setText(statisticsTsv(*m_model, r.xmin, r.xmax));
    });
    connect(m_rgMin, &QDoubleSpinBox::valueChanged, this, &PropertiesPanel::commitRegion);
    connect(m_rgMax, &QDoubleSpinBox::valueChanged, this, &PropertiesPanel::commitRegion);
    connect(clear, &QPushButton::clicked, this, [this] { m_editor->clearRegion(); });
    return box;
}

QWidget* PropertiesPanel::buildPeakPage()
{
    auto* box = new QGroupBox("Peak");
    auto* form = new QFormLayout(box);
    m_pkLabel = new QLineEdit;
    m_pkLabel->setPlaceholderText("e.g. H3/C5  (shown on the plot)");
    m_pkX = makeCoordSpin();
    m_pkY = makeCoordSpin();
    m_pkIntensity = new QLabel;
    m_pkComment = new QPlainTextEdit;
    m_pkComment->setToolTip("Longer notes about the peak, kept off the plot (exported in the CSV).");
    m_pkComment->setPlaceholderText("Notes (not shown on the plot; exported with the peak list)");
    m_pkComment->setMinimumHeight(110);
    form->addRow("Label", m_pkLabel);
    form->addRow("x", m_pkX);
    form->addRow("y", m_pkY);
    form->addRow("Intensity", m_pkIntensity);
    form->addRow(new QLabel("Comment"));
    form->addRow(m_pkComment);
    connect(m_pkLabel, &QLineEdit::editingFinished, this,
            [this] { commitPeak(false, "Edit peak label"); });
    connect(m_pkX, &QDoubleSpinBox::valueChanged, this, [this] { commitPeak(true, "Move peak"); });
    connect(m_pkY, &QDoubleSpinBox::valueChanged, this, [this] { commitPeak(true, "Move peak"); });
    // Typing merges into one undo step per pause (see kMergeWindowMs).
    connect(m_pkComment, &QPlainTextEdit::textChanged, this,
            [this] { commitPeak(true, "Edit peak comment"); });
    return box;
}

void PropertiesPanel::commitPeak(bool mergeable, const QString& text)
{
    const Peak* cur = m_model->peak(m_current.id);
    if (m_current.kind != ObjectKind::Peak || !cur) return;
    Peak p = *cur;
    p.label = m_pkLabel->text();
    p.x = m_pkX->value();
    p.y = m_pkY->value();
    p.comment = m_pkComment->toPlainText();
    m_editor->updatePeak(p, std::nullopt, mergeable, text);
}

QWidget* PropertiesPanel::buildMatrixPage()
{
    auto* box = new QGroupBox("2D data (contours)");
    auto* form = new QFormLayout(box);
    m_mxName = new QLineEdit;
    m_mxBase = new QLineEdit;
    auto* validator = new QDoubleValidator(m_mxBase);
    validator->setBottom(0);
    validator->setNotation(QDoubleValidator::ScientificNotation);
    validator->setLocale(QLocale::c());  // "1.5" everywhere (read back with toDouble)
    m_mxBase->setValidator(validator);
    m_mxBase->setToolTip("Lowest positive contour level. Scroll the wheel with Alt held "
                         "(or with 'Levels' enabled) to change it; '*' halves it, '/' doubles it.");
    auto* lower = new QToolButton;
    lower->setText("÷2");
    lower->setToolTip("Lower all levels (show weaker features), key '*'");
    auto* higher = new QToolButton;
    higher->setText("×2");
    higher->setToolTip("Raise all levels, key '/'");
    auto* baseRow = new QHBoxLayout;
    baseRow->addWidget(m_mxBase, 1);
    baseRow->addWidget(lower);
    baseRow->addWidget(higher);
    m_mxFactor = new QDoubleSpinBox;
    m_mxFactor->setRange(1.001, 100);
    m_mxFactor->setSingleStep(0.05);
    m_mxFactor->setDecimals(3);
    m_mxFactor->setKeyboardTracking(false);
    m_mxCount = new QSpinBox;
    m_mxCount->setRange(1, 1000);
    m_mxCount->setKeyboardTracking(false);
    m_mxPosColor = new ColorButton;
    m_mxNegColor = new ColorButton;
    m_mxNegative = new QCheckBox("Negative levels");
    m_mxNegative->setObjectName("matrixNegative");
    m_mxWidth = new QDoubleSpinBox;
    m_mxWidth->setRange(0.01, 50);
    m_mxWidth->setSingleStep(0.25);
    m_mxWidth->setKeyboardTracking(false);
    m_mxImage = new QCheckBox("Colour-map image");
    m_mxContours = new QCheckBox("Contour lines");
    m_mxColorBar = new QCheckBox("Colour bar");
    m_mxColormap = new QComboBox;
    for (const QString& name : colormapNames()) m_mxColormap->addItem(name);
    m_mxVisible = new QCheckBox("Visible");
    m_mxInfo = new QLabel;
    m_mxInfo->setWordWrap(true);
    m_mxInfo->setForegroundRole(QPalette::PlaceholderText);
    form->addRow("Name", m_mxName);
    form->addRow("Base level", baseRow);
    form->addRow("Level factor", m_mxFactor);
    form->addRow("Levels", m_mxCount);
    form->addRow("Positive colour", m_mxPosColor);
    form->addRow("Negative colour", m_mxNegColor);
    form->addRow("Line width", m_mxWidth);
    form->addRow(m_mxNegative);
    form->addRow(m_mxImage);
    form->addRow("Colour map", m_mxColormap);
    form->addRow(m_mxColorBar);
    form->addRow(m_mxContours);
    form->addRow(m_mxVisible);
    form->addRow(m_mxInfo);

    connect(m_mxName, &QLineEdit::editingFinished, this, [this] { commitMatrix(false); });
    connect(m_mxBase, &QLineEdit::editingFinished, this, [this] { commitMatrix(false); });
    connect(lower, &QToolButton::clicked, this,
            [this] { m_controller->scaleContourLevels(0.5); });
    connect(higher, &QToolButton::clicked, this,
            [this] { m_controller->scaleContourLevels(2.0); });
    connect(m_mxFactor, &QDoubleSpinBox::valueChanged, this, [this] { commitMatrix(true); });
    connect(m_mxCount, &QSpinBox::valueChanged, this, [this] { commitMatrix(true); });
    connect(m_mxPosColor, &ColorButton::colorPicked, this, [this] { commitMatrix(false); });
    connect(m_mxNegColor, &ColorButton::colorPicked, this, [this] { commitMatrix(false); });
    connect(m_mxWidth, &QDoubleSpinBox::valueChanged, this, [this] { commitMatrix(true); });
    connect(m_mxNegative, &QCheckBox::toggled, this, [this] { commitMatrix(false); });
    connect(m_mxImage, &QCheckBox::toggled, this, [this] { commitMatrix(false); });
    connect(m_mxContours, &QCheckBox::toggled, this, [this] { commitMatrix(false); });
    connect(m_mxColorBar, &QCheckBox::toggled, this, [this] { commitMatrix(false); });
    connect(m_mxColormap, &QComboBox::activated, this, [this] { commitMatrix(false); });
    connect(m_mxVisible, &QCheckBox::toggled, this, [this] { commitMatrix(false); });
    return box;
}

// ---- tree ------------------------------------------------------------------------

void PropertiesPanel::rebuildTree()
{
    m_updatingTree = true;
    for (auto* root : {m_datasetsRoot, m_matricesRoot, m_markersRoot, m_annotationsRoot})
        qDeleteAll(root->takeChildren());
    if (m_regionItem) {
        delete m_regionItem;
        m_regionItem = nullptr;
    }
    auto addItem = [](QTreeWidgetItem* root, ObjectKind kind, ObjectId id) {
        auto* item = new QTreeWidgetItem(root);
        item->setData(0, kKindRole, int(kind));
        item->setData(0, kIdRole, QVariant::fromValue<qulonglong>(id));
        return item;
    };
    for (const auto& d : m_model->datasets()) {
        auto* item = addItem(m_datasetsRoot, ObjectKind::Dataset, d.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    }
    for (const auto& m : m_model->matrices()) {
        auto* item = addItem(m_matricesRoot, ObjectKind::Matrix, m.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    }
    m_matricesRoot->setHidden(m_model->matrices().empty());
    for (const auto& m : m_model->markers()) addItem(m_markersRoot, ObjectKind::Marker, m.id);
    for (const auto& a : m_model->annotations())
        addItem(m_annotationsRoot, ObjectKind::Annotation, a.id);
    if (m_model->region().active) {
        m_regionItem = new QTreeWidgetItem(m_tree);
        m_regionItem->setData(0, kKindRole, int(ObjectKind::Region));
        m_regionItem->setData(0, kIdRole, QVariant::fromValue<qulonglong>(0));
        QFont f = m_regionItem->font(0);
        f.setBold(true);
        m_regionItem->setFont(0, f);
    }
    for (const auto& d : m_model->datasets()) refreshTreeItem({ObjectKind::Dataset, d.id});
    for (const auto& m : m_model->matrices()) refreshTreeItem({ObjectKind::Matrix, m.id});
    for (const auto& m : m_model->markers()) refreshTreeItem({ObjectKind::Marker, m.id});
    for (const auto& a : m_model->annotations()) refreshTreeItem({ObjectKind::Annotation, a.id});
    if (m_regionItem) refreshTreeItem({ObjectKind::Region, 0});
    m_updatingTree = false;

    // Keep showing the current object if it still exists.
    showObject(m_controller->selection());
}

QTreeWidgetItem* PropertiesPanel::findItem(const ObjectRef& ref) const
{
    if (ref.kind == ObjectKind::Region) return m_regionItem;
    QTreeWidgetItem* root = ref.kind == ObjectKind::Dataset    ? m_datasetsRoot
                            : ref.kind == ObjectKind::Matrix   ? m_matricesRoot
                            : ref.kind == ObjectKind::Marker   ? m_markersRoot
                            : ref.kind == ObjectKind::Annotation ? m_annotationsRoot
                                                                 : nullptr;
    if (!root) return nullptr;
    for (int i = 0; i < root->childCount(); ++i)
        if (root->child(i)->data(0, kIdRole).toULongLong() == ref.id) return root->child(i);
    return nullptr;
}

void PropertiesPanel::refreshTreeItem(const ObjectRef& ref)
{
    QTreeWidgetItem* item = findItem(ref);
    if (!item) return;
    const bool wasUpdating = m_updatingTree;
    m_updatingTree = true;
    switch (ref.kind) {
    case ObjectKind::Dataset:
        if (const Dataset* d = m_model->dataset(ref.id)) {
            item->setText(0, d->name);
            item->setIcon(0, swatchIcon(d->style.color));
            item->setCheckState(0, d->style.visible ? Qt::Checked : Qt::Unchecked);
            item->setToolTip(0, QString("%1 points").arg(d->x.size()));
        }
        break;
    case ObjectKind::Marker:
        if (const Marker* m = m_model->marker(ref.id)) {
            const QString axis = m->orientation == Orientation::Vertical ? "x" : "y";
            item->setText(0, QString("%1  (%2 = %3)")
                                 .arg(m->label.isEmpty() ? "Marker" : m->label, axis,
                                      fmt(m->position)));
            item->setIcon(0, swatchIcon(m->color));
        }
        break;
    case ObjectKind::Annotation:
        if (const Annotation* a = m_model->annotation(ref.id)) {
            QString t = a->text;
            t.replace('\n', ' ');
            item->setText(0, t.isEmpty() ? "(empty)" : t);
            // Unset colour = the style's annotation colour. Swatches always
            // show the stored colours (as exported), like datasets and markers.
            item->setIcon(0, swatchIcon(a->color.isValid() ? a->color : m_model->style().annotationColor));
        }
        break;
    case ObjectKind::Region: {
        const Region& r = m_model->region();
        item->setText(0, QString("Region  [%1, %2]").arg(fmt(r.xmin), fmt(r.xmax)));
        break;
    }
    case ObjectKind::Matrix:
        if (const Matrix* m = m_model->matrix(ref.id)) {
            item->setText(0, QString("%1  (base %2)").arg(m->name, fmt(m->style.baseLevel)));
            item->setIcon(0, swatchIcon(m->style.positiveColor));
            item->setCheckState(0, m->style.visible ? Qt::Checked : Qt::Unchecked);
            item->setToolTip(0, QString("%1 x %2 grid").arg(m->nx).arg(m->ny));
        }
        break;
    case ObjectKind::None:
    case ObjectKind::Peak: break;  // peaks are listed in the peak table
    }
    m_updatingTree = wasUpdating;
}

void PropertiesPanel::showObject(const ObjectRef& refIn)
{
    ObjectRef ref = refIn;
    // Validate (the object may have vanished).
    if ((ref.kind == ObjectKind::Dataset && !m_model->dataset(ref.id)) ||
        (ref.kind == ObjectKind::Marker && !m_model->marker(ref.id)) ||
        (ref.kind == ObjectKind::Annotation && !m_model->annotation(ref.id)) ||
        (ref.kind == ObjectKind::Region && !m_model->region().active) ||
        (ref.kind == ObjectKind::Matrix && !m_model->matrix(ref.id)) ||
        (ref.kind == ObjectKind::Peak && !m_model->peak(ref.id)))
        ref = {};
    m_current = ref;

    {
        const bool was = m_updatingTree;
        m_updatingTree = true;
        QTreeWidgetItem* item = findItem(ref);
        m_tree->setCurrentItem(item);
        if (!item) m_tree->clearSelection();
        m_updatingTree = was;
    }
    m_removeButton->setEnabled(ref.isValid());
    switch (ref.kind) {
    case ObjectKind::Dataset: m_stack->setCurrentWidget(m_datasetPage); break;
    case ObjectKind::Marker: m_stack->setCurrentWidget(m_markerPage); break;
    case ObjectKind::Annotation: m_stack->setCurrentWidget(m_annotationPage); break;
    case ObjectKind::Region: m_stack->setCurrentWidget(m_regionPage); break;
    case ObjectKind::Matrix: m_stack->setCurrentWidget(m_matrixPage); break;
    case ObjectKind::Peak: m_stack->setCurrentWidget(m_peakPage); break;
    case ObjectKind::None: m_stack->setCurrentWidget(m_emptyPage); break;
    }
    refreshCurrentPage();
}

void PropertiesPanel::beginEdit(const ObjectRef& ref)
{
    showObject(ref);
    QLineEdit* field = ref.kind == ObjectKind::Annotation ? m_anText
                       : ref.kind == ObjectKind::Marker   ? m_mkLabel
                       : ref.kind == ObjectKind::Dataset  ? m_dsName
                       : ref.kind == ObjectKind::Matrix   ? m_mxBase
                       : ref.kind == ObjectKind::Peak     ? m_pkLabel
                                                          : nullptr;
    if (field) {
        field->setFocus(Qt::OtherFocusReason);
        field->selectAll();
    }
}

void PropertiesPanel::refreshCurrentPage()
{
    switch (m_current.kind) {
    case ObjectKind::Dataset: {
        const Dataset* d = m_model->dataset(m_current.id);
        if (!d) return;
        const QSignalBlocker b1(m_dsName), b2(m_dsWidth), b3(m_dsLineStyle), b4(m_dsScatter),
            b5(m_dsScatterSize), b6(m_dsVisible);
        if (!m_dsName->hasFocus()) m_dsName->setText(d->name);
        m_dsColor->setColor(d->style.color);
        showValue(m_dsWidth, d->style.lineWidth);
        setComboData(m_dsLineStyle, int(d->style.lineStyle));
        setComboData(m_dsScatter, int(d->style.scatter));
        showValue(m_dsScatterSize, d->style.scatterSize);
        m_dsVisible->setChecked(d->style.visible);
        const QSignalBlocker b7(m_dsLegend), b8(m_dsStep), b9(m_dsFill);
        m_dsLegend->setChecked(d->style.inLegend);
        setComboData(m_dsStep, int(d->style.step));
        m_dsFill->setChecked(d->style.fillColor.isValid());
        if (d->style.fillColor.isValid()) m_dsFillColor->setColor(d->style.fillColor);
        double xmin = 0, xmax = 0;
        if (!d->x.isEmpty()) {
            xmin = *std::min_element(d->x.begin(), d->x.end());
            xmax = *std::max_element(d->x.begin(), d->x.end());
        }
        m_dsInfo->setText(QString("%1 points, x ∈ [%2, %3]")
                              .arg(d->x.size())
                              .arg(fmt(xmin), fmt(xmax)));
        break;
    }
    case ObjectKind::Marker: {
        const Marker* m = m_model->marker(m_current.id);
        if (!m) return;
        const QSignalBlocker b1(m_mkLabel), b2(m_mkPosition), b3(m_mkLineStyle), b4(m_mkWidth),
            b5(m_mkShowValue);
        m_mkOrientation->setText(m->orientation == Orientation::Vertical ? "Vertical (x)"
                                                                         : "Horizontal (y)");
        if (!m_mkLabel->hasFocus()) m_mkLabel->setText(m->label);
        m_mkPosition->setValue(m->position);
        m_mkColor->setColor(m->color);
        setComboData(m_mkLineStyle, int(m->lineStyle));
        showValue(m_mkWidth, m->lineWidth);
        m_mkShowValue->setChecked(m->showValue);
        break;
    }
    case ObjectKind::Annotation: {
        const Annotation* a = m_model->annotation(m_current.id);
        if (!a) return;
        const QSignalBlocker b1(m_anText), b2(m_anX), b3(m_anY), b4(m_anFontSize);
        if (!m_anText->hasFocus()) {
            QString t = a->text;
            m_anText->setText(t.replace('\n', "\\n"));
        }
        m_anX->setValue(a->x);
        m_anY->setValue(a->y);
        // Unset values follow the style; show the effective ones.
        const Style& st = m_model->style();
        m_anColor->setColor(a->color.isValid() ? a->color : st.annotationColor);
        showValue(m_anFontSize, a->fontSize > 0 ? a->fontSize : st.annotationSize);
        break;
    }
    case ObjectKind::Region: {
        const Region& r = m_model->region();
        const QSignalBlocker b1(m_rgMin), b2(m_rgMax);
        m_rgMin->setValue(r.xmin);
        m_rgMax->setValue(r.xmax);
        m_rgWidth->setText(fmt(r.xmax - r.xmin));
        QString html = "<table cellspacing='0' cellpadding='1'>";
        for (const SignalStats& s : rangeStats(*m_model, r.xmin, r.xmax)) {
            html += QString("<tr><td colspan='2'><b>%1</b> (%2 points)</td></tr>")
                        .arg(s.name.toHtmlEscaped())
                        .arg(s.n);
            if (s.n == 0) continue;
            auto row = [&](const QString& k, const QString& v) {
                html += QString("<tr><td>&nbsp;&nbsp;%1&nbsp;</td><td>%2</td></tr>").arg(k, v);
            };
            row("mean ± sd", fmt(s.mean) + " ± " + fmt(s.std));
            row("min", fmt(s.min) + " @ " + fmt(s.xAtMin));
            row("max", fmt(s.max) + " @ " + fmt(s.xAtMax));
            row("rms", fmt(s.rms));
            row("integral", fmt(s.integral));
        }
        m_rgStats->setText(html + "</table>");
        break;
    }
    case ObjectKind::Peak: {
        const Peak* p = m_model->peak(m_current.id);
        if (!p) return;
        const QSignalBlocker b1(m_pkLabel), b2(m_pkX), b3(m_pkY), b4(m_pkComment);
        if (!m_pkLabel->hasFocus()) m_pkLabel->setText(p->label);
        m_pkX->setValue(p->x);
        m_pkY->setValue(p->y);
        m_pkIntensity->setText(QString::number(p->intensity, 'g', 6));
        if (!m_pkComment->hasFocus() || m_pkComment->toPlainText() != p->comment) {
            if (m_pkComment->toPlainText() != p->comment) m_pkComment->setPlainText(p->comment);
        }
        break;
    }
    case ObjectKind::Matrix: {
        const Matrix* m = m_model->matrix(m_current.id);
        if (!m) return;
        const QSignalBlocker b1(m_mxName), b2(m_mxBase), b3(m_mxFactor), b4(m_mxCount),
            b5(m_mxNegative), b6(m_mxWidth), b7(m_mxImage), b8(m_mxVisible), b9(m_mxContours),
            b10(m_mxColorBar), b11(m_mxColormap);
        if (!m_mxName->hasFocus()) m_mxName->setText(m->name);
        if (!m_mxBase->hasFocus()) m_mxBase->setText(QString::number(m->style.baseLevel, 'g', 6));
        showValue(m_mxFactor, m->style.factor);
        showValue(m_mxCount, m->style.levelCount);
        m_mxPosColor->setColor(m->style.positiveColor);
        m_mxNegColor->setColor(m->style.negativeColor);
        m_mxNegative->setChecked(m->style.showNegative);
        showValue(m_mxWidth, m->style.lineWidth);
        m_mxImage->setChecked(m->style.showImage);
        m_mxContours->setChecked(m->style.showContours);
        m_mxColorBar->setChecked(m->style.colorBar);
        // "viridis_r" is shown as "viridis" (the reversal is kept unless another map is chosen).
        const int cm = m_mxColormap->findText(m->style.colormap.isEmpty() ? QString("auto")
                                                                          : QString(m->style.colormap).remove("_r"));
        m_mxColormap->setCurrentIndex(std::max(0, cm));
        m_mxVisible->setChecked(m->style.visible);
        const std::vector<double> lv = contourLevels(m->style);
        double top = m->style.baseLevel;
        for (double l : lv) top = std::max(top, l);
        m_mxInfo->setText(QString("%1 × %2 grid\nx ∈ [%3, %4], y ∈ [%5, %6]\nlevels %7 … %8")
                              .arg(m->nx).arg(m->ny)
                              .arg(fmt(m->x0), fmt(m->x1), fmt(m->y0), fmt(m->y1))
                              .arg(fmt(m->style.baseLevel), fmt(top)));
        break;
    }
    case ObjectKind::None: break;
    }
}

void PropertiesPanel::refreshAxes()
{
    const AxesInfo& a = m_model->axesInfo();
    const QSignalBlocker b1(m_title), b2(m_xLabel), b3(m_yLabel), b4(m_legend), b5(m_grid),
        b6(m_xRev), b7(m_yRev), b8(m_diag);
    m_title->setText(a.title);
    m_xLabel->setText(a.xLabel);
    m_yLabel->setText(a.yLabel);
    m_legend->setChecked(a.legendVisible);
    m_grid->setChecked(a.gridVisible);
    m_xRev->setChecked(a.xReversed);
    m_yRev->setChecked(a.yReversed);
    m_diag->setChecked(a.showDiagonal);
    const QSignalBlocker b9(m_xLog), b10(m_yLog);
    m_xLog->setChecked(a.xLog);
    m_yLog->setChecked(a.yLog);
}

// ---- commits -------------------------------------------------------------------------

void PropertiesPanel::commitDataset(bool mergeable)
{
    const Dataset* d = m_model->dataset(m_current.id);
    if (m_current.kind != ObjectKind::Dataset || !d) return;
    DatasetStyle s = d->style;
    s.color = m_dsColor->color();
    s.lineWidth = edited(m_dsWidth, d->style.lineWidth);
    s.lineStyle = LineStyle(m_dsLineStyle->currentData().toInt());
    s.scatter = ScatterShape(m_dsScatter->currentData().toInt());
    s.scatterSize = edited(m_dsScatterSize, d->style.scatterSize);
    s.visible = m_dsVisible->isChecked();
    s.inLegend = m_dsLegend->isChecked();
    s.step = StepMode(m_dsStep->currentData().toInt());
    s.fillColor = m_dsFill->isChecked() ? m_dsFillColor->color() : QColor();
    m_editor->setDatasetAppearance(d->id, m_dsName->text(), s, mergeable);
}

void PropertiesPanel::commitMarker(bool mergeable)
{
    const Marker* cur = m_model->marker(m_current.id);
    if (m_current.kind != ObjectKind::Marker || !cur) return;
    Marker m = *cur;
    m.label = m_mkLabel->text();
    m.position = m_mkPosition->value();
    m.color = m_mkColor->color();
    m.lineStyle = LineStyle(m_mkLineStyle->currentData().toInt());
    m.lineWidth = edited(m_mkWidth, cur->lineWidth);
    m.showValue = m_mkShowValue->isChecked();
    m_editor->updateMarker(m, std::nullopt, mergeable);
}

void PropertiesPanel::commitAnnotation(bool mergeable)
{
    const Annotation* cur = m_model->annotation(m_current.id);
    if (m_current.kind != ObjectKind::Annotation || !cur) return;
    Annotation a = *cur;
    a.text = m_anText->text().replace("\\n", "\n");
    a.x = m_anX->value();
    a.y = m_anY->value();
    // Colour / size stay "follow the style" unless the user changed them.
    const Style& st = m_model->style();
    if (cur->color.isValid() || m_anColor->color() != st.annotationColor) a.color = m_anColor->color();
    const double size = edited(m_anFontSize, cur->fontSize > 0 ? cur->fontSize : st.annotationSize);
    if (cur->fontSize > 0 || size != st.annotationSize) a.fontSize = size;
    m_editor->updateAnnotation(a, std::nullopt, mergeable);
}

void PropertiesPanel::commitRegion()
{
    if (m_current.kind != ObjectKind::Region) return;
    m_editor->setRegion(Region{true, m_rgMin->value(), m_rgMax->value()});
}

void PropertiesPanel::commitMatrix(bool mergeable)
{
    const Matrix* m = m_model->matrix(m_current.id);
    if (m_current.kind != ObjectKind::Matrix || !m) return;
    ContourStyle s = m->style;
    bool ok = false;
    const double base = m_mxBase->text().toDouble(&ok);
    if (ok && base > 0) s.baseLevel = base;
    s.factor = edited(m_mxFactor, m->style.factor);
    s.levelCount = edited(m_mxCount, m->style.levelCount);
    s.positiveColor = m_mxPosColor->color();
    s.negativeColor = m_mxNegColor->color();
    s.showNegative = m_mxNegative->isChecked();
    s.lineWidth = edited(m_mxWidth, m->style.lineWidth);
    s.showImage = m_mxImage->isChecked();
    s.showContours = m_mxContours->isChecked();
    if (!s.showContours) s.showImage = true;  // never invisible
    s.colorBar = m_mxColorBar->isChecked();
    const QString cmap = m_mxColormap->currentText();
    // Keep a reversed ("_r") map unless another one was chosen.
    if (cmap != QString(s.colormap).remove("_r") && !(cmap == "auto" && s.colormap.isEmpty()))
        s.colormap = cmap == "auto" ? QString() : cmap;
    s.visible = m_mxVisible->isChecked();
    m_editor->setMatrixAppearance(m->id, m_mxName->text(), s, mergeable);
}

void PropertiesPanel::refreshLimits()
{
    const ViewRange v = m_controller->renderer()->viewRange();
    const QSignalBlocker b1(m_xMin), b2(m_xMax), b3(m_yMin), b4(m_yMax);
    m_xMin->setValue(v.xmin);
    m_xMax->setValue(v.xmax);
    m_yMin->setValue(v.ymin);
    m_yMax->setValue(v.ymax);
}

void PropertiesPanel::commitLimits()
{
    // Each field commits on its own. When the edited bound crosses the other
    // one (e.g. typing 20 as X min while the view is 0..10), keep the current
    // width and move the other bound along, so any range can be typed.
    const ViewRange old = m_controller->renderer()->viewRange();
    ViewRange v{m_xMin->value(), m_xMax->value(), m_yMin->value(), m_yMax->value()};
    auto fix = [](double& lo, double& hi, double oldLo, double oldHi) {
        if (hi > lo) return;
        const double w = oldHi > oldLo ? oldHi - oldLo : 1.0;
        if (lo != oldLo) hi = lo + w;  // the minimum was edited
        else lo = hi - w;              // the maximum was edited
    };
    fix(v.xmin, v.xmax, old.xmin, old.xmax);
    fix(v.ymin, v.ymax, old.ymin, old.ymax);
    if (v.xmax > v.xmin && v.ymax > v.ymin) m_controller->renderer()->setViewRange(v);
    refreshLimits();
}

void PropertiesPanel::commitAxes()
{
    AxesInfo a = m_model->axesInfo();
    a.title = m_title->text();
    a.xLabel = m_xLabel->text();
    a.yLabel = m_yLabel->text();
    a.legendVisible = m_legend->isChecked();
    a.gridVisible = m_grid->isChecked();
    a.xReversed = m_xRev->isChecked();
    a.yReversed = m_yRev->isChecked();
    a.showDiagonal = m_diag->isChecked();
    a.xLog = m_xLog->isChecked();
    a.yLog = m_yLog->isChecked();
    m_editor->setAxesInfo(a);
}

}  // namespace gp
