// StyleEditor: dialog listing every style key with its value and
// description. Values are edited in place; "Apply"/"OK" push one undoable
// "Edit style" command. Invalid values are highlighted and not applied.
#pragma once

#include "model/Style.h"

#include <QDialog>

class QTableWidget;
class QLineEdit;
class QLabel;

namespace gp {

class PlotEditor;

class StyleEditor : public QDialog {
    Q_OBJECT
public:
    StyleEditor(PlotEditor* editor, QWidget* parent = nullptr);

private:
    void load(const Style& s);
    bool collect(Style* out);  // false if a value is invalid
    void apply();

    PlotEditor* m_editor;
    QTableWidget* m_table = nullptr;
    QLineEdit* m_filter = nullptr;
    QLabel* m_status = nullptr;
};

}  // namespace gp
