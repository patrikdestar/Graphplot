// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
//
// Hover explanations (tooltips) for every button, field and menu entry.
//
// All texts live in one table (Explanations.cpp), keyed by where a control
// appears ("Dataset/Line width", "action/Export…"), so the wording is easy to
// review and edit in one place. Controls that set their own tooltip in code
// keep it. Whether tooltips are shown at all is a user setting
// (View > Appearance > Show explanations on hover, see Appearance).
#pragma once

class QWidget;

namespace gp {

// Gives every control under `root` (and its menus and actions) its
// explanation. Safe to call again after more controls were added.
void applyExplanations(QWidget* root);

}  // namespace gp
