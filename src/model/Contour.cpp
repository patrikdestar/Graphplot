#include "Contour.h"

#include <algorithm>
#include <cmath>

namespace gp {

std::vector<double> contourLevels(const ContourStyle& s)
{
    std::vector<double> levels;
    if (!(s.baseLevel > 0) || s.levelCount <= 0) return levels;
    const double f = s.factor > 1.0 ? s.factor : 1.0001;
    double l = s.baseLevel;
    for (int k = 0; k < s.levelCount; ++k, l *= f) {
        levels.push_back(l);
        if (s.showNegative) levels.push_back(-l);
    }
    return levels;
}

double defaultBaseLevel(const Matrix& m, double factor, int count)
{
    double maxAbs = 0.0;
    for (double v : m.z)
        if (std::isfinite(v)) maxAbs = std::max(maxAbs, std::abs(v));
    if (maxAbs == 0.0) return 1.0;
    const double f = factor > 1.0 ? factor : 1.4;
    return 0.95 * maxAbs / std::pow(f, std::max(0, count - 1));
}

void ContourGrid::setMatrix(const Matrix& m)
{
    m_nx = m.nx;
    m_ny = m.ny;
    m_z = m.z;
    if (m_nx < 2 || m_ny < 2 || m_z.size() < qsizetype(m_nx) * m_ny) {
        m_nx = m_ny = 0;
        m_cellMin.clear();
        m_cellMax.clear();
        return;
    }
    m_x0 = m.x0;
    m_y0 = m.y0;
    m_dx = (m.x1 - m.x0) / (m_nx - 1);
    m_dy = (m.y1 - m.y0) / (m_ny - 1);
    const qsizetype cells = qsizetype(m_nx - 1) * (m_ny - 1);
    m_cellMin.resize(cells);
    m_cellMax.resize(cells);
    const double* z = m_z.constData();
    for (int iy = 0; iy < m_ny - 1; ++iy) {
        for (int ix = 0; ix < m_nx - 1; ++ix) {
            const double a = z[iy * m_nx + ix], b = z[iy * m_nx + ix + 1];
            const double c = z[(iy + 1) * m_nx + ix + 1], d = z[(iy + 1) * m_nx + ix];
            double lo = std::min(std::min(a, b), std::min(c, d));
            double hi = std::max(std::max(a, b), std::max(c, d));
            if (!std::isfinite(lo) || !std::isfinite(hi)) lo = hi = std::nan("");  // skipped
            // Stored as float to halve memory; round outwards so no level that
            // really crosses the cell is skipped by the fast reject.
            float flo = float(lo), fhi = float(hi);
            if (double(flo) > lo) flo = std::nextafter(flo, -INFINITY);
            if (double(fhi) < hi) fhi = std::nextafter(fhi, INFINITY);
            m_cellMin[qsizetype(iy) * (m_nx - 1) + ix] = flo;
            m_cellMax[qsizetype(iy) * (m_nx - 1) + ix] = fhi;
        }
    }
}

std::vector<ContourLines> ContourGrid::compute(const std::vector<double>& levelsIn) const
{
    std::vector<ContourLines> out;
    if (isEmpty() || levelsIn.empty()) return out;

    // Work on sorted levels so each cell can find its relevant levels with a
    // binary search; map back to output slots afterwards.
    std::vector<int> order(levelsIn.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return levelsIn[a] < levelsIn[b]; });
    std::vector<double> sorted(levelsIn.size());
    for (size_t i = 0; i < order.size(); ++i) sorted[i] = levelsIn[order[i]];

    out.resize(levelsIn.size());
    for (size_t i = 0; i < levelsIn.size(); ++i) {
        out[i].level = levelsIn[i];
        out[i].negative = levelsIn[i] < 0;
    }

    const double* z = m_z.constData();
    const int cw = m_nx - 1;
    for (int iy = 0; iy < m_ny - 1; ++iy) {
        const double y = m_y0 + iy * m_dy;
        for (int ix = 0; ix < cw; ++ix) {
            const float cmin = m_cellMin[qsizetype(iy) * cw + ix];
            const float cmax = m_cellMax[qsizetype(iy) * cw + ix];
            if (!(cmax >= cmin)) continue;  // NaN cell
            // Only levels inside [cmin, cmax] can cross this cell. (cmin/cmax
            // are floats; the exact corner test below decides precisely.)
            auto first = std::lower_bound(sorted.begin(), sorted.end(), double(cmin));
            if (first == sorted.end() || *first > double(cmax)) continue;
            // Corner values: 0=(ix,iy) 1=(ix+1,iy) 2=(ix+1,iy+1) 3=(ix,iy+1)
            const double v0 = z[iy * m_nx + ix], v1 = z[iy * m_nx + ix + 1];
            const double v2 = z[(iy + 1) * m_nx + ix + 1], v3 = z[(iy + 1) * m_nx + ix];
            const double x = m_x0 + ix * m_dx;
            for (auto it = first; it != sorted.end() && *it <= double(cmax); ++it) {
                const double L = *it;
                const int idx = int((v0 > L) ? 1 : 0) | ((v1 > L) ? 2 : 0) | ((v2 > L) ? 4 : 0) |
                                ((v3 > L) ? 8 : 0);
                if (idx == 0 || idx == 15) continue;
                // Edge crossing points (linear interpolation).
                auto lerp = [L](double a, double b) { return (L - a) / (b - a); };
                auto e0 = [&] { return QPointF(x + lerp(v0, v1) * m_dx, y); };           // bottom
                auto e1 = [&] { return QPointF(x + m_dx, y + lerp(v1, v2) * m_dy); };    // right
                auto e2 = [&] { return QPointF(x + lerp(v3, v2) * m_dx, y + m_dy); };    // top
                auto e3 = [&] { return QPointF(x, y + lerp(v0, v3) * m_dy); };           // left
                QVector<QLineF>& seg = out[order[size_t(it - sorted.begin())]].segments;
                switch (idx) {
                case 1: case 14: seg.push_back({e3(), e0()}); break;
                case 2: case 13: seg.push_back({e0(), e1()}); break;
                case 3: case 12: seg.push_back({e3(), e1()}); break;
                case 4: case 11: seg.push_back({e1(), e2()}); break;
                case 6: case 9: seg.push_back({e0(), e2()}); break;
                case 7: case 8: seg.push_back({e3(), e2()}); break;
                case 5: case 10: {
                    // Saddle: disambiguate with the cell-centre average.
                    const bool centreHigh = 0.25 * (v0 + v1 + v2 + v3) > L;
                    if ((idx == 5) == centreHigh) {
                        seg.push_back({e3(), e2()});
                        seg.push_back({e0(), e1()});
                    } else {
                        seg.push_back({e3(), e0()});
                        seg.push_back({e1(), e2()});
                    }
                    break;
                }
                default: break;
                }
            }
        }
    }
    return out;
}

}  // namespace gp
