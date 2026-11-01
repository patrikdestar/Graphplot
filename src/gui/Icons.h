// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
// Small toolbar icons drawn with QPainter, so the application needs no
// image resources and the icons stay crisp at any device pixel ratio.
#pragma once

#include <QIcon>

class QAction;
class QObject;

namespace gp::icons {

enum class Kind {
    Zoom, Pan, Region, VMarker, HMarker, Annotation, Autoscale, ResetView, Undo, Redo, Export,
    Levels, Peak
};

// The outline colour follows the application palette (dark themes get light
// icons); call refresh() after the palette changes.
QIcon make(Kind kind);
// Sets an action's icon and remembers its kind, so refresh() can redraw it.
void set(QAction* action, Kind kind);
// Redraws the icons of every action under `root` that was given one by set().
void refresh(QObject* root);

}  // namespace gp::icons
