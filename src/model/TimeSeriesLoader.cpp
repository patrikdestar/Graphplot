// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "TimeSeriesLoader.h"

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

QStringList splitLine(const QString& line, QChar sep)
{
    if (sep == ' ') return line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    QStringList parts = line.split(sep);
    for (QString& p : parts) {
        p = p.trimmed();
        if (p.size() >= 2 && p.startsWith('"') && p.endsWith('"')) p = p.mid(1, p.size() - 2);
    }
    return parts;
}

QChar detectSeparator(const QString& line)
{
    for (QChar c : {QChar(','), QChar(';'), QChar('\t')})
        if (line.contains(c)) return c;
    return ' ';
}

bool isTimeHeader(QString h)
{
    h = h.toLower();
    h.remove(QRegularExpression(R"([\[\(].*[\]\)])"));
    h = h.trimmed();
    static const QStringList names = {"time", "t", "timestamp", "stamp", "datetime", "date_time",
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

bool normalizeTimes(QVector<double>& t)
{
    double maxAbs = 0;
    for (double v : t)
        if (std::isfinite(v)) maxAbs = std::max(maxAbs, std::abs(v));
    // Unix epoch ranges for dates between ~1973 and ~2286.
    double scale = 0;
    if (maxAbs > 1e17) scale = 1e-9;       // ns
    else if (maxAbs > 1e14) scale = 1e-6;  // us
    else if (maxAbs > 1e11) scale = 1e-3;  // ms
    else if (maxAbs > 1e8) scale = 1.0;    // s
    if (scale == 0) return false;
    if (scale != 1.0)
        for (double& v : t) v *= scale;
    return true;
}

TimeSeriesTable loadTimeSeriesCsv(const QString& fileName)
{
    TimeSeriesTable out;
    out.name = QFileInfo(fileName).completeBaseName();
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        out.error = QString("Cannot open '%1': %2").arg(fileName, f.errorString());
        return out;
    }
    QTextStream in(&f);
    QStringList header;
    QChar sep;
    std::vector<QStringList> rows;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith('%')) continue;
        if (sep.isNull()) sep = detectSeparator(line);
        const QStringList parts = splitLine(line, sep);
        if (header.isEmpty() && rows.empty()) {
            // A header line has at least one cell that is neither a number nor a time.
            bool anyText = false;
            for (const QString& p : parts) {
                bool dt = false;
                if (!p.isEmpty() && std::isnan(parseTimeCell(p, &dt)) &&
                    p.compare("nan", Qt::CaseInsensitive) != 0)
                    anyText = true;
            }
            if (anyText) {
                header = parts;
                continue;
            }
        }
        rows.push_back(parts);
    }
    if (rows.empty()) {
        out.error = "The file contains no data rows";
        return out;
    }
    const int ncols = int(std::max<qsizetype>(header.size(), rows.front().size()));
    if (ncols < 2) {
        out.error = "Expected a time column and at least one signal column";
        return out;
    }
    for (int c = header.size(); c < ncols; ++c) header << (c == 0 ? "time" : QString("col%1").arg(c));

    int tcol = 0;
    for (int c = 0; c < header.size(); ++c)
        if (isTimeHeader(header[c])) {
            tcol = c;
            break;
        }
    out.timeColumn = header[tcol];

    const qsizetype n = qsizetype(rows.size());
    QVector<double> t(n);
    std::vector<QVector<double>> vals(size_t(ncols - 1), QVector<double>(n, kNaN));
    bool anyDateTime = false;
    for (qsizetype r = 0; r < n; ++r) {
        const QStringList& row = rows[size_t(r)];
        bool dt = false;
        t[r] = tcol < row.size() ? parseTimeCell(row[tcol], &dt) : kNaN;
        anyDateTime |= dt;
        int k = 0;
        for (int c = 0; c < ncols; ++c) {
            if (c == tcol) continue;
            bool ok = false;
            const double v = c < row.size() ? row[c].toDouble(&ok) : kNaN;
            vals[size_t(k++)][r] = ok ? v : kNaN;
        }
    }
    out.absolute = anyDateTime || normalizeTimes(t);

    // Drop rows without a valid time, then sort by time (stable).
    std::vector<qsizetype> order;
    order.reserve(size_t(n));
    for (qsizetype r = 0; r < n; ++r)
        if (std::isfinite(t[r])) order.push_back(r);
    std::stable_sort(order.begin(), order.end(), [&](qsizetype a, qsizetype b) { return t[a] < t[b]; });
    if (order.empty()) {
        out.error = QString("No valid time values in column '%1'").arg(out.timeColumn);
        return out;
    }
    out.time.reserve(qsizetype(order.size()));
    for (qsizetype r : order) out.time.push_back(t[r]);
    for (int c = 0, k = 0; c < ncols; ++c) {
        if (c == tcol) continue;
        QVector<double> v;
        v.reserve(qsizetype(order.size()));
        for (qsizetype r : order) v.push_back(vals[size_t(k)][r]);
        // Skip columns that are entirely non-numeric (e.g. text labels).
        const bool anyFinite = std::any_of(v.begin(), v.end(), [](double x) { return std::isfinite(x); });
        if (anyFinite) {
            out.columns << header[c];
            out.values.push_back(std::move(v));
        }
        ++k;
    }
    if (out.columns.isEmpty()) out.error = "No numeric signal columns found";
    return out;
}

}  // namespace gp
