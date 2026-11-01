// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "TimeSeriesLoader.h"

#include "DataLoader.h"  // detectSeparator / splitFields

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace gp {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

bool isTimeHeader(QString h)
{
    h = h.toLower();
    static const QRegularExpression units(R"([\[\(].*[\]\)])");
    h.remove(units);
    static const QRegularExpression unitSuffix(R"(_(ns|us|ms|s|sec)$)");
    h = h.trimmed().remove(unitSuffix);
    static const QStringList names = {"time", "t", "timestamp", "stamp", "datetime", "date_time", "date time",
                                      "sec", "secs", "seconds", "time_s", "timestamp_s", "ts"};
    return names.contains(h);
}

}  // namespace

double parseTimeCell(const QString& cellIn, bool* isDateTime)
{
    if (isDateTime) *isDateTime = false;
    const QString cell = cellIn.trimmed();
    if (cell.isEmpty()) return kNaN;
    bool ok = false;
    const double v = cell.toDouble(&ok);
    if (ok) return v;
    // ISO 8601 (with 'T' or a space between date and time).
    QString iso = cell;
    if (iso.size() > 10 && iso[10] == ' ') iso[10] = 'T';
    QDateTime dt = QDateTime::fromString(iso, Qt::ISODateWithMs);
    if (!dt.isValid()) dt = QDateTime::fromString(iso, Qt::ISODate);
    if (!dt.isValid()) return kNaN;
    if (isDateTime) *isDateTime = true;
    return double(dt.toMSecsSinceEpoch()) / 1000.0;
}

bool normalizeTimes(QVector<double>& t, double unitScale)
{
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (double v : t)
        if (std::isfinite(v)) lo = std::min(lo, v), hi = std::max(hi, v);
    if (unitScale > 0) {
        // The header named the unit: scale to seconds; epoch if the whole
        // range lies after ~1973.
        if (unitScale != 1.0)
            for (double& v : t) v *= unitScale;
        return hi >= lo && lo * unitScale >= 1e8;
    }
    if (!(hi >= lo)) return false;
    // Unix epochs (dates between ~1973 and ~2286) in s, ms, us or ns. Both the
    // first and the last time must be in that range: a relative counter
    // (e.g. microseconds since boot, 0 ... 6e8) starts near zero.
    double scale = 0;
    if (hi > 1e17) scale = 1e-9;       // ns
    else if (hi > 1e14) scale = 1e-6;  // us
    else if (hi > 1e11) scale = 1e-3;  // ms
    else if (hi > 1e8) scale = 1.0;    // s
    if (scale == 0 || lo * scale < 1e8) return false;
    if (scale != 1.0)
        for (double& v : t) v *= scale;
    return true;
}

// Seconds per unit named in a time header ("time_ms", "t [us]", "stamp (ns)"),
// or 0 when no unit is given.
double headerUnitScale(const QString& header)
{
    static const QRegularExpression re(R"((?:[_\s\[\(])(ns|us|µs|ms|s|sec|min|h)\]?\)?\s*$)",
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(header.trimmed());
    if (!m.hasMatch()) return 0;
    const QString u = m.captured(1).toLower();
    if (u == "ns") return 1e-9;
    if (u == "us" || u == QStringLiteral("µs")) return 1e-6;
    if (u == "ms") return 1e-3;
    if (u == "min") return 60;
    if (u == "h") return 3600;
    return 1;
}

namespace {

// Space-separated rows split "2024-01-01 12:00:00" in two: join a date field
// with a following clock-time field. Returns the index of the joined field.
int joinDateTime(QStringList& parts)
{
    static const QRegularExpression date(R"(^\d{4}-\d{2}-\d{2}$)");
    static const QRegularExpression clock(R"(^\d{1,2}:\d{2}(:\d{2}([.,]\d+)?)?([zZ]|[+-]\d{2}:?\d{2})?$)");
    for (int i = 0; i + 1 < parts.size(); ++i)
        if (date.match(parts[i]).hasMatch() && clock.match(parts[i + 1]).hasMatch()) {
            parts[i] += 'T' + parts[i + 1];
            parts.removeAt(i + 1);
            return i;
        }
    return -1;
}

bool isHeaderRow(const QStringList& parts)
{
    // A header has at least one cell that is neither a number nor a time.
    for (const QString& p : parts) {
        bool dt = false;
        if (!p.isEmpty() && std::isnan(parseTimeCell(p, &dt)) && p.compare("nan", Qt::CaseInsensitive) != 0)
            return true;
    }
    return false;
}

}  // namespace

TimeSeriesTable loadTimeSeriesCsv(const QString& fileName)
{
    TimeSeriesTable out;
    out.name = QFileInfo(fileName).completeBaseName();
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        out.error = QString("Cannot open '%1': %2").arg(fileName, f.errorString());
        return out;
    }
    // Rows are parsed into numbers as they are read (memory ~ the numbers,
    // not the text). The column layout is fixed by the header / first row.
    QTextStream in(&f);
    QChar sep;
    QString headerLine;
    QStringList header;
    int ncols = 0, tcol = 0, joinedAt = -1;
    QVector<double> t;
    std::vector<QVector<double>> vals;  // one per non-time column
    bool anyDateTime = false;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith('%')) continue;
        if (sep.isNull()) sep = detectSeparator(line);
        QStringList parts = splitFields(line, sep);
        if (sep == ' ') {
            const int j = joinDateTime(parts);
            if (ncols == 0) joinedAt = j;
        }
        if (ncols == 0 && headerLine.isEmpty() && isHeaderRow(parts)) {
            headerLine = line;
            continue;
        }
        if (ncols == 0) {  // first data row: fix the layout
            if (!headerLine.isEmpty()) {
                // The header names a joined "date time" pair with two words.
                QStringList h = splitHeader(headerLine, sep, parts.size());
                if (h.size() == parts.size() + 1 && joinedAt >= 0) {
                    h[joinedAt] += ' ' + h[joinedAt + 1];
                    h.removeAt(joinedAt + 1);
                }
                header = h;
            }
            ncols = int(std::max<qsizetype>(header.size(), parts.size()));
            if (ncols < 2) {
                out.error = "Expected a time column and at least one signal column";
                return out;
            }
            for (int c = int(header.size()); c < ncols; ++c)
                header << (c == 0 ? "time" : QString("col%1").arg(c));
            for (int c = 0; c < ncols; ++c)
                if (isTimeHeader(header[c])) {
                    tcol = c;
                    break;
                }
            vals.assign(size_t(ncols - 1), {});
        }
        bool dt = false;
        t.push_back(tcol < parts.size() ? parseTimeCell(parts[tcol], &dt) : kNaN);
        anyDateTime |= dt;
        for (int c = 0, k = 0; c < ncols; ++c) {
            if (c == tcol) continue;
            bool ok = false;
            const double v = c < parts.size() ? parts[c].toDouble(&ok) : kNaN;
            vals[size_t(k++)].push_back(ok ? v : kNaN);
        }
    }
    if (t.isEmpty()) {
        out.error = "The file contains no data rows";
        return out;
    }
    out.timeColumn = header[tcol];
    out.absolute = anyDateTime || normalizeTimes(t, headerUnitScale(out.timeColumn));

    // Drop rows without a valid time, then sort by time (stable).
    const qsizetype n = t.size();
    std::vector<qsizetype> order;
    order.reserve(size_t(n));
    for (qsizetype r = 0; r < n; ++r)
        if (std::isfinite(t[r])) order.push_back(r);
    std::stable_sort(order.begin(), order.end(), [&](qsizetype a, qsizetype b) { return t[a] < t[b]; });
    if (order.empty()) {
        out.error = QString("No valid time values in column '%1'").arg(out.timeColumn);
        return out;
    }
    const bool sorted = qsizetype(order.size()) == n &&
                        std::is_sorted(order.begin(), order.end());  // the common case: no copy
    auto reorder = [&](QVector<double>& v) {
        if (sorted) return;
        QVector<double> r;
        r.reserve(qsizetype(order.size()));
        for (qsizetype i : order) r.push_back(v[i]);
        v = std::move(r);
    };
    reorder(t);
    out.time = std::move(t);
    for (int c = 0, k = 0; c < ncols; ++c) {
        if (c == tcol) continue;
        QVector<double>& v = vals[size_t(k++)];
        reorder(v);
        // Skip columns that are entirely non-numeric (e.g. text labels).
        if (std::any_of(v.begin(), v.end(), [](double x) { return std::isfinite(x); })) {
            out.columns << header[c];
            out.values.push_back(std::move(v));
        }
    }
    if (out.columns.isEmpty()) out.error = "No numeric signal columns found";
    return out;
}

}  // namespace gp
