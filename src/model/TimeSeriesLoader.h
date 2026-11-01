// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
//
// Loader for sensor logs stored as CSV / text tables: one time column plus
// any number of signal columns (e.g. an IMU file with ax, ay, az, gx, gy, gz).
//
// Time column: the first column whose header is time / t / timestamp /
// stamp / datetime / sec / seconds (case-insensitive, units in brackets are
// ignored), otherwise the first column. Accepted time values:
//   * ISO 8601 date-times ("2024-05-02T14:03:27.250", "2024-05-02 14:03:27")
//   * Unix epoch numbers in s, ms, us or ns (detected from the magnitude of
//     the whole range)
//   * relative times; a unit in the header ("time_ms", "t [us]") is honoured
//   * plain seconds (relative time)
// Empty or non-numeric cells become NaN (drawn as gaps). Rows are sorted
// by time.
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <vector>

namespace gp {

struct TimeSeriesTable {
    QString name;                      // file base name
    QString timeColumn;
    bool absolute = false;             // true: time is Unix epoch seconds
    QVector<double> time;              // seconds (epoch seconds when absolute)
    QStringList columns;               // signal names
    std::vector<QVector<double>> values;  // one vector per signal, same length as time
    QString error;                     // empty on success
};

TimeSeriesTable loadTimeSeriesCsv(const QString& fileName);

// Parses one time cell; returns NaN if it is not a time. `absolute` is set
// for ISO date-times. Numbers are returned unscaled (see normalizeTimes).
double parseTimeCell(const QString& cell, bool* isDateTime);

// Converts raw numeric times to seconds. With unitScale > 0 (seconds per
// unit, from the header) the times are scaled by it. Otherwise
// values whose whole range looks like Unix epoch timestamps in s / ms / us /
// ns are scaled to seconds; returns true if they are epochs.
bool normalizeTimes(QVector<double>& t, double unitScale = 0);
// Seconds per unit named in a time column header ("time_ms", "t [us]"),
// 0 if none.
double headerUnitScale(const QString& header);

}  // namespace gp
