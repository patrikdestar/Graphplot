// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
// Contour line extraction (marching squares) for regular 2D grids.
//
// Pure computation on value types - no rendering or GUI dependencies - so it
// can be unit tested and reused (e.g. returned to Python).
#pragma once

#include "PlotTypes.h"

#include <QLineF>
#include <vector>

namespace gp {

struct ContourLines {
    double level = 0.0;
    bool negative = false;
    QVector<QLineF> segments;  // in data coordinates
};

// Levels implied by a contour style: base * factor^k (k = 0..count-1) and,
// if enabled, the mirrored negative levels. Sorted by absolute value.
std::vector<double> contourLevels(const ContourStyle& style);

// Sensible default base level: the highest level lands just below max |z|.
double defaultBaseLevel(const Matrix& m, double factor, int count);

// Pre-processes a grid once (per-cell min/max) so that recomputing contours
// for new levels - e.g. while the user scrolls the levels - only visits
// cells whose value range actually contains a level.
class ContourGrid {
public:
    void setMatrix(const Matrix& m);
    bool isEmpty() const { return m_nx < 2 || m_ny < 2; }
    std::vector<ContourLines> compute(const std::vector<double>& levels) const;

private:
    int m_nx = 0, m_ny = 0;
    double m_x0 = 0, m_dx = 1, m_y0 = 0, m_dy = 1;
    QVector<double> m_z;
    QVector<float> m_cellMin, m_cellMax;  // (nx-1)*(ny-1)
};

}  // namespace gp
