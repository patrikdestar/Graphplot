// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
// Small toolbar icons drawn with QPainter, so the application needs no
// image resources and the icons stay crisp at any device pixel ratio.
#pragma once

#include <QIcon>

namespace gp::icons {

enum class Kind {
    Zoom, Pan, Region, VMarker, HMarker, Annotation, Autoscale, ResetView, Undo, Redo, Export,
    Levels, Peak
};

QIcon make(Kind kind);

}  // namespace gp::icons
