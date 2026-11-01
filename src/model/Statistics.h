// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
//
// Descriptive statistics of datasets inside an X range (the region
// selection): count, mean, min / max (and where), standard deviation, RMS
// and the trapezoidal integral. Pure computation on the model.
#pragma once

#include "PlotModel.h"

#include <vector>

namespace gp {

struct SignalStats {
    ObjectId id = kInvalidId;
    QString name;
    qsizetype n = 0;          // finite points inside the range
    double mean = 0, min = 0, max = 0;
    double xAtMin = 0, xAtMax = 0;
    double std = 0;           // sample standard deviation (n - 1)
    double rms = 0;
    double integral = 0;      // trapezoid rule over the points inside the range
};

// Statistics of every visible dataset over [x0, x1] (in any order).
std::vector<SignalStats> rangeStats(const PlotModel& model, double x0, double x1);

}  // namespace gp
