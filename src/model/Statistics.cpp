// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "Statistics.h"

#include <cmath>
#include <limits>

namespace gp {

std::vector<SignalStats> rangeStats(const PlotModel& model, double x0, double x1)
{
    if (x1 < x0) std::swap(x0, x1);
    std::vector<SignalStats> out;
    for (const Dataset& d : model.datasets()) {
        if (!d.style.visible) continue;
        SignalStats s;
        s.id = d.id;
        s.name = d.name;
        // Welford's running mean / variance: exact enough even for values on a
        // large offset (the naive sum of squares cancels catastrophically).
        double mean = 0, m2 = 0, meanSq = 0;
        bool havePrev = false;
        double px = 0, py = 0;
        for (qsizetype i = 0; i < d.x.size(); ++i) {
            const double x = d.x[i], y = d.y[i];
            if (!(x >= x0 && x <= x1) || !std::isfinite(y)) {
                havePrev = false;  // gaps interrupt the integral
                continue;
            }
            if (s.n == 0 || y < s.min) s.min = y, s.xAtMin = x;
            if (s.n == 0 || y > s.max) s.max = y, s.xAtMax = x;
            ++s.n;
            const double delta = y - mean;
            mean += delta / double(s.n);
            m2 += delta * (y - mean);
            meanSq += (y * y - meanSq) / double(s.n);  // running mean of y^2 (no overflow of a sum)
            // |dx|: descending axes (ppm) give a positive area for positive data.
            if (havePrev) s.integral += 0.5 * (y + py) * std::abs(x - px);
            havePrev = true;
            px = x;
            py = y;
        }
        if (s.n == 0) {
            // Nothing in the range: "no value", not a misleading 0.
            const double nan = std::numeric_limits<double>::quiet_NaN();
            s.mean = s.min = s.max = s.xAtMin = s.xAtMax = s.std = s.rms = s.integral = nan;
        } else {
            s.mean = mean;
            s.rms = std::sqrt(meanSq);
            s.std = s.n > 1 ? std::sqrt(std::max(0.0, m2) / double(s.n - 1)) : 0.0;
        }
        out.push_back(s);
    }
    return out;
}

}  // namespace gp
