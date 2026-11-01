// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
// PropertiesPanel: side panel listing datasets, markers, annotations and the
// region selection, with editors for the selected object and the axes.
// All edits go through PlotEditor, so they are undoable.
#pragma once

#include "model/PlotTypes.h"

#include <QToolButton>
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QStackedWidget;
class QLineEdit;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QPlainTextEdit;

namespace gp {

class PlotEditor;
class PlotModel;
class InteractionController;

// Tool button showing a colour swatch; opens a QColorDialog on click.
class ColorButton : public QToolButton {
    Q_OBJECT
public:
    explicit ColorButton(QWidget* parent = nullptr);
    QColor color() const { return m_color; }
    void setColor(const QColor& c);
signals:
    void colorPicked(const QColor& c);

private:
    QColor m_color;
};

class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    PropertiesPanel(PlotEditor* editor, InteractionController* controller,
                    QWidget* parent = nullptr);

    // Focuses the main text field of the given object (label / annotation text).
    void beginEdit(const ObjectRef& ref);

private:
    void buildUi();
    QWidget* buildDatasetPage();
    QWidget* buildMarkerPage();
    QWidget* buildAnnotationPage();
    QWidget* buildRegionPage();
    QWidget* buildMatrixPage();
    QWidget* buildPeakPage();

    void rebuildTree();
    void refreshTreeItem(const ObjectRef& ref);
    QTreeWidgetItem* findItem(const ObjectRef& ref) const;
    void showObject(const ObjectRef& ref);
    void refreshCurrentPage();
    void refreshAxes();

    void commitDataset(bool mergeable);
    void commitMarker(bool mergeable);
    void commitAnnotation(bool mergeable);
    void commitRegion();
    void commitMatrix(bool mergeable);
    void commitPeak(bool mergeable, const QString& text);
    void commitAxes();
    void refreshLimits();
    void commitLimits();

    PlotEditor* m_editor;
    PlotModel* m_model;
    InteractionController* m_controller;
    ObjectRef m_current;
    bool m_updatingTree = false;

    QTreeWidget* m_tree = nullptr;
    QTreeWidgetItem* m_datasetsRoot = nullptr;
    QTreeWidgetItem* m_matricesRoot = nullptr;
    QTreeWidgetItem* m_markersRoot = nullptr;
    QTreeWidgetItem* m_annotationsRoot = nullptr;
    QTreeWidgetItem* m_regionItem = nullptr;
    QPushButton* m_removeButton = nullptr;
    QStackedWidget* m_stack = nullptr;
    QWidget* m_emptyPage = nullptr;
    QWidget* m_datasetPage = nullptr;
    QWidget* m_markerPage = nullptr;
    QWidget* m_annotationPage = nullptr;
    QWidget* m_regionPage = nullptr;
    QWidget* m_matrixPage = nullptr;
    QWidget* m_peakPage = nullptr;

    // axes
    QLineEdit* m_title = nullptr;
    QLineEdit* m_xLabel = nullptr;
    QLineEdit* m_yLabel = nullptr;
    QCheckBox* m_legend = nullptr;
    QCheckBox* m_grid = nullptr;
    QCheckBox* m_xRev = nullptr;
    QCheckBox* m_yRev = nullptr;
    QCheckBox* m_diag = nullptr;
    QCheckBox* m_xLog = nullptr;
    QCheckBox* m_yLog = nullptr;
    QDoubleSpinBox* m_xMin = nullptr;
    QDoubleSpinBox* m_xMax = nullptr;
    QDoubleSpinBox* m_yMin = nullptr;
    QDoubleSpinBox* m_yMax = nullptr;

    // dataset
    QLineEdit* m_dsName = nullptr;
    ColorButton* m_dsColor = nullptr;
    QDoubleSpinBox* m_dsWidth = nullptr;
    QComboBox* m_dsLineStyle = nullptr;
    QComboBox* m_dsScatter = nullptr;
    QDoubleSpinBox* m_dsScatterSize = nullptr;
    QCheckBox* m_dsVisible = nullptr;
    QComboBox* m_dsStep = nullptr;
    QCheckBox* m_dsFill = nullptr;
    ColorButton* m_dsFillColor = nullptr;
    QCheckBox* m_dsLegend = nullptr;
    QLabel* m_dsInfo = nullptr;

    // 2D data / contours
    QLineEdit* m_mxName = nullptr;
    QLineEdit* m_mxBase = nullptr;
    QDoubleSpinBox* m_mxFactor = nullptr;
    QSpinBox* m_mxCount = nullptr;
    ColorButton* m_mxPosColor = nullptr;
    ColorButton* m_mxNegColor = nullptr;
    QCheckBox* m_mxNegative = nullptr;
    QDoubleSpinBox* m_mxWidth = nullptr;
    QCheckBox* m_mxImage = nullptr;
    QCheckBox* m_mxContours = nullptr;
    QCheckBox* m_mxColorBar = nullptr;
    QComboBox* m_mxColormap = nullptr;
    QCheckBox* m_mxVisible = nullptr;
    QLabel* m_mxInfo = nullptr;

    // peak
    QLineEdit* m_pkLabel = nullptr;
    QDoubleSpinBox* m_pkX = nullptr;
    QDoubleSpinBox* m_pkY = nullptr;
    QLabel* m_pkIntensity = nullptr;
    QPlainTextEdit* m_pkComment = nullptr;

    // marker
    QLabel* m_mkOrientation = nullptr;
    QLineEdit* m_mkLabel = nullptr;
    QDoubleSpinBox* m_mkPosition = nullptr;
    ColorButton* m_mkColor = nullptr;
    QComboBox* m_mkLineStyle = nullptr;
    QDoubleSpinBox* m_mkWidth = nullptr;
    QCheckBox* m_mkShowValue = nullptr;

    // annotation
    QLineEdit* m_anText = nullptr;
    QDoubleSpinBox* m_anX = nullptr;
    QDoubleSpinBox* m_anY = nullptr;
    ColorButton* m_anColor = nullptr;
    QDoubleSpinBox* m_anFontSize = nullptr;

    // region
    QDoubleSpinBox* m_rgMin = nullptr;
    QDoubleSpinBox* m_rgMax = nullptr;
    QLabel* m_rgWidth = nullptr;
    QLabel* m_rgStats = nullptr;
};

}  // namespace gp
