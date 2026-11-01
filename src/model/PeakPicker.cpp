#include "PeakPicker.h"

#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>

namespace gp {

namespace {

struct Grid {
    const Matrix& m;
    double dx, dy;
    explicit Grid(const Matrix& mm)
        : m(mm), dx((mm.x1 - mm.x0) / std::max(1, mm.nx - 1)),
          dy((mm.y1 - mm.y0) / std::max(1, mm.ny - 1))
    {
    }
    double z(int ix, int iy) const { return m.z[qsizetype(iy) * m.nx + ix]; }
    double xAt(double ix) const { return m.x0 + ix * dx; }
    double yAt(double iy) const { return m.y0 + iy * dy; }
    double ixOf(double x) const { return dx != 0 ? (x - m.x0) / dx : 0; }
    double iyOf(double y) const { return dy != 0 ? (y - m.y0) / dy : 0; }
};

// Vertex offset of the parabola through (-1,a), (0,b), (1,c), clamped.
double parabolicOffset(double a, double b, double c)
{
    const double den = a - 2 * b + c;
    if (den == 0 || !std::isfinite(den)) return 0;
    return std::clamp(0.5 * (a - c) / den, -0.5, 0.5);
}

// s = +1 for maxima, -1 for minima (negative peaks).
bool isLocalExtremum(const Grid& g, int ix, int iy, double s)
{
    const double v = s * g.z(ix, iy);
    if (!std::isfinite(v)) return false;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            const int jx = ix + dx, jy = iy + dy;
            if (jx < 0 || jy < 0 || jx >= g.m.nx || jy >= g.m.ny) continue;
            const double w = s * g.z(jx, jy);
            // Plateaus: earlier neighbours may be equal, later ones must be lower,
            // so exactly one point of a flat top is reported.
            const bool earlier = dy < 0 || (dy == 0 && dx < 0);
            if (earlier ? w > v : w >= v) return false;
        }
    return true;
}

Peak makePeak(const Grid& g, int ix, int iy)
{
    const double b = g.z(ix, iy);
    double ox = 0, oy = 0;
    if (ix > 0 && ix < g.m.nx - 1) ox = parabolicOffset(g.z(ix - 1, iy), b, g.z(ix + 1, iy));
    if (iy > 0 && iy < g.m.ny - 1) oy = parabolicOffset(g.z(ix, iy - 1), b, g.z(ix, iy + 1));
    Peak p;
    p.matrixId = g.m.id;
    p.x = g.xAt(ix + ox);
    p.y = g.yAt(iy + oy);
    p.intensity = b;
    return p;
}

}  // namespace

std::vector<Peak> pickPeaks(const Matrix& m, const PeakPickOptions& opt)
{
    std::vector<Peak> out;
    if (m.nx < 3 || m.ny < 3 || !(opt.threshold > 0)) return out;
    const Grid g(m);

    // Restrict the scanned index window to the requested rectangle.
    int ix0 = 0, ix1 = m.nx - 1, iy0 = 0, iy1 = m.ny - 1;
    if (opt.restrictToRect) {
        const double a = g.ixOf(opt.xmin), b = g.ixOf(opt.xmax);
        const double c = g.iyOf(opt.ymin), d = g.iyOf(opt.ymax);
        ix0 = std::max(0, int(std::floor(std::min(a, b))));
        ix1 = std::min(m.nx - 1, int(std::ceil(std::max(a, b))));
        iy0 = std::max(0, int(std::floor(std::min(c, d))));
        iy1 = std::min(m.ny - 1, int(std::ceil(std::max(c, d))));
    }

    struct Cand { int ix, iy; double mag; };
    std::vector<Cand> cands;
    for (int iy = iy0; iy <= iy1; ++iy)
        for (int ix = ix0; ix <= ix1; ++ix) {
            const double v = g.z(ix, iy);
            double s = 0;
            if (opt.positive && v >= opt.threshold) s = 1;
            else if (opt.negative && -v >= opt.threshold) s = -1;
            if (s == 0 || !isLocalExtremum(g, ix, iy, s)) continue;
            if (opt.diagonalExclusion > 0 &&
                std::abs(g.xAt(ix) - g.yAt(iy)) < opt.diagonalExclusion)
                continue;
            cands.push_back({ix, iy, std::abs(v)});
        }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.mag > b.mag; });

    std::vector<Cand> accepted;
    const int dmin = std::max(0, opt.minDistance);
    for (const Cand& c : cands) {
        bool close = false;
        for (const Cand& a : accepted)
            if (std::max(std::abs(a.ix - c.ix), std::abs(a.iy - c.iy)) < dmin) {
                close = true;
                break;
            }
        if (close) continue;
        accepted.push_back(c);
        Peak p = makePeak(g, c.ix, c.iy);
        if (opt.restrictToRect &&
            (p.x < std::min(opt.xmin, opt.xmax) || p.x > std::max(opt.xmin, opt.xmax) ||
             p.y < std::min(opt.ymin, opt.ymax) || p.y > std::max(opt.ymin, opt.ymax)))
            continue;
        out.push_back(p);
    }
    return out;
}

std::optional<Peak> snapToPeak(const Matrix& m, double x, double y, int radius, bool allowNegative)
{
    if (m.nx < 2 || m.ny < 2) return std::nullopt;
    const Grid g(m);
    const int cx = int(std::lround(g.ixOf(x))), cy = int(std::lround(g.iyOf(y)));
    if (cx < 0 || cy < 0 || cx >= m.nx || cy >= m.ny) return std::nullopt;
    int bx = cx, by = cy;
    double best = -1;
    for (int iy = std::max(0, cy - radius); iy <= std::min(m.ny - 1, cy + radius); ++iy)
        for (int ix = std::max(0, cx - radius); ix <= std::min(m.nx - 1, cx + radius); ++ix) {
            const double v = g.z(ix, iy);
            const double mag = allowNegative ? std::abs(v) : v;
            if (std::isfinite(mag) && mag > best) {
                best = mag;
                bx = ix;
                by = iy;
            }
        }
    return makePeak(g, bx, by);
}

double sampleMatrix(const Matrix& m, double x, double y)
{
    if (m.nx < 2 || m.ny < 2) return std::nan("");
    const Grid g(m);
    const double fx = g.ixOf(x), fy = g.iyOf(y);
    if (fx < 0 || fy < 0 || fx > m.nx - 1 || fy > m.ny - 1) return std::nan("");
    const int ix = std::min(int(fx), m.nx - 2), iy = std::min(int(fy), m.ny - 2);
    const double tx = fx - ix, ty = fy - iy;
    return (1 - ty) * ((1 - tx) * g.z(ix, iy) + tx * g.z(ix + 1, iy)) +
           ty * ((1 - tx) * g.z(ix, iy + 1) + tx * g.z(ix + 1, iy + 1));
}

// ---- CSV ------------------------------------------------------------------------

namespace {

QString csvField(const QString& s)
{
    if (!s.contains(',') && !s.contains('"') && !s.contains('\n') && !s.contains('\r')) return s;
    QString q = s;
    q.replace('"', "\"\"");
    return '"' + q + '"';
}

// Parses RFC 4180 CSV (quoted fields may span lines).
std::vector<QStringList> parseCsv(const QString& text)
{
    std::vector<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false, fieldStarted = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == '"') {
            quoted = true;
            fieldStarted = true;
        } else if (c == ',') {
            row << field;
            field.clear();
            fieldStarted = false;
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            if (fieldStarted || !field.isEmpty() || !row.isEmpty()) {
                row << field;
                rows.push_back(row);
            }
            row.clear();
            field.clear();
            fieldStarted = false;
        } else {
            field += c;
            fieldStarted = true;
        }
    }
    if (fieldStarted || !field.isEmpty() || !row.isEmpty()) {
        row << field;
        rows.push_back(row);
    }
    return rows;
}

}  // namespace

bool writePeakCsv(const QString& fileName, const std::vector<Peak>& peaks, const QString& xName,
                  const QString& yName, QString* error)
{
    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) *error = QString("Cannot write '%1': %2").arg(fileName, f.errorString());
        return false;
    }
    QTextStream s(&f);
    s << csvField(xName) << ',' << csvField(yName) << ",intensity,label,comment\n";
    for (const Peak& p : peaks)
        s << QString::number(p.x, 'g', 10) << ',' << QString::number(p.y, 'g', 10) << ','
          << QString::number(p.intensity, 'g', 8) << ',' << csvField(p.label) << ','
          << csvField(p.comment) << '\n';
    return true;
}

std::vector<Peak> readPeakCsv(const QString& fileName, QString* error)
{
    std::vector<Peak> out;
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QString("Cannot open '%1': %2").arg(fileName, f.errorString());
        return out;
    }
    const auto rows = parseCsv(QString::fromUtf8(f.readAll()));
    for (size_t r = 0; r < rows.size(); ++r) {
        const QStringList& row = rows[r];
        if (row.size() < 2) continue;
        bool okx = false, oky = false;
        Peak p;
        p.x = row[0].trimmed().toDouble(&okx);
        p.y = row[1].trimmed().toDouble(&oky);
        if (!okx || !oky) {
            if (r == 0) continue;  // header
            if (error) *error = QString("Row %1: x/y are not numbers").arg(r + 1);
            return {};
        }
        if (row.size() > 2) p.intensity = row[2].trimmed().toDouble();
        if (row.size() > 3) p.label = row[3];
        if (row.size() > 4) p.comment = row[4];
        out.push_back(p);
    }
    return out;
}

}  // namespace gp
