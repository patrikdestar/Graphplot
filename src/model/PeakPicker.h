// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
// Peak picking on 2D grids and peak-list file I/O.
//
// Algorithm (the standard approach, as in nmrglue's find_all_thres):
//  1. candidates = grid points above the threshold (|z| for negative peaks)
//     that are strict local maxima in their 3x3 neighbourhood;
//  2. optional exclusion of points close to the diagonal (|x - y| < d);
//  3. non-maximum suppression: strongest first, reject candidates closer
//     than `minDistance` grid points to an accepted peak;
//  4. sub-grid position by parabolic interpolation along each axis.
#pragma once

#include "PlotTypes.h"

#include <optional>
#include <vector>

namespace gp {

struct PeakPickOptions {
    double threshold = 0.0;       // absolute value; <= 0 means "use the lowest contour level"
    bool positive = true;
    bool negative = false;
    int minDistance = 3;          // grid points (Chebyshev distance)
    double diagonalExclusion = 0; // skip peaks with |x - y| < this (data units); 0 = off
    bool restrictToRect = false;  // only pick inside [xmin,xmax] x [ymin,ymax]
    double xmin = 0, xmax = 0, ymin = 0, ymax = 0;
};

std::vector<Peak> pickPeaks(const Matrix& m, const PeakPickOptions& opt);

// Finds the strongest local extremum within `radius` grid points of (x, y).
// Returns nothing if the matrix is empty or (x, y) lies outside it.
std::optional<Peak> snapToPeak(const Matrix& m, double x, double y, int radius,
                               bool allowNegative = true);

// Bilinear interpolation of the matrix at (x, y); NaN outside.
double sampleMatrix(const Matrix& m, double x, double y);

// CSV with header: x,y,intensity,label,comment (RFC 4180 quoting, so labels
// and comments may contain commas, quotes and line breaks).
bool writePeakCsv(const QString& fileName, const std::vector<Peak>& peaks,
                  const QString& xName = "x", const QString& yName = "y", QString* error = nullptr);
std::vector<Peak> readPeakCsv(const QString& fileName, QString* error = nullptr);

}  // namespace gp
