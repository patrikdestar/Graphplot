// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "Statistics.h"

#include <cmath>

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
        double sum = 0, sumSq = 0;
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
            sum += y;
            sumSq += y * y;
            if (havePrev) s.integral += 0.5 * (y + py) * (x - px);
            havePrev = true;
            px = x;
            py = y;
        }
        if (s.n > 0) {
            s.mean = sum / double(s.n);
            s.rms = std::sqrt(sumSq / double(s.n));
            // Sum of squared deviations, guarded against tiny negative rounding.
            const double ss = std::max(0.0, sumSq - double(s.n) * s.mean * s.mean);
            s.std = s.n > 1 ? std::sqrt(ss / double(s.n - 1)) : 0.0;
        }
        out.push_back(s);
    }
    return out;
}

}  // namespace gp
