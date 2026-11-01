// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
// StyleEditor: dialog listing every style key with its value and
// description. Values are edited in place; "Apply"/"OK" push one undoable
// "Edit style" command. Invalid values are highlighted and not applied.
#pragma once

#include "model/Style.h"

#include <QDialog>
#include <functional>

class QTableWidget;
class QLineEdit;
class QLabel;

namespace gp {

class StyleEditor : public QDialog {
    Q_OBJECT
public:
    // `apply` is called with the edited style on Apply / OK (the figure
    // applies it to all of its panels as one undo step).
    StyleEditor(const Style& initial, std::function<void(const Style&)> apply,
                QWidget* parent = nullptr);

private:
    void load(const Style& s);
    bool collect(Style* out);  // false if a value is invalid
    void apply();

    Style m_base;  // keys not shown in the table keep these values
    std::function<void(const Style&)> m_apply;
    QTableWidget* m_table = nullptr;
    QLineEdit* m_filter = nullptr;
    QLabel* m_status = nullptr;
};

}  // namespace gp
