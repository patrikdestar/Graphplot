// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "DataExport.h"

#include "PlotModel.h"
#include "Statistics.h"

#include <QLocale>
#include <QSaveFile>
#include <QTextStream>
#include <cmath>

namespace gp {

namespace {

// Shortest text that reads back to exactly the same double (0.1, not 0.10000000000000001).
QString num(double v)
{
    return std::isfinite(v) ? QString::number(v, 'g', QLocale::FloatingPointShortest) : QString();
}

QString field(QString s)
{
    if (!s.contains(',') && !s.contains('"') && !s.contains('\n')) return s;
    return '"' + s.replace('"', "\"\"") + '"';
}

bool hasErrors(const Dataset& d) { return !d.yErrLow.isEmpty() || !d.xErrLow.isEmpty(); }

}  // namespace

bool writeDataCsv(const PlotModel& model, const QString& fileName, std::optional<Region> region, QString* error)
{
    std::vector<const Dataset*> sets;
    for (const Dataset& d : model.datasets())
        if (d.style.visible) sets.push_back(&d);
    if (sets.empty()) {
        if (error) *error = "There is no visible 1D data to export";
        return false;
    }
    const double lo = region ? std::min(region->xmin, region->xmax) : -INFINITY;
    const double hi = region ? std::max(region->xmin, region->xmax) : INFINITY;
    auto inside = [&](double x) { return x >= lo && x <= hi; };
    auto name = [](const Dataset* d, size_t i) { return d->name.isEmpty() ? QString("dataset %1").arg(i + 1) : d->name; };

    QSaveFile f(fileName);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QString("Cannot write '%1': %2").arg(fileName, f.errorString());
        return false;
    }
    QTextStream out(&f);
    const AxesInfo& ax = model.axesInfo();
    const QString xName = ax.xLabel.isEmpty() ? QStringLiteral("x") : ax.xLabel;

    // Wide table only when all datasets share x and none has x error bars
    // (those need their own columns per point: the long table has them).
    bool sameX = true;
    for (const Dataset* d : sets) sameX = sameX && d->x == sets.front()->x && d->xErrLow.isEmpty();
    if (sameX) {
        // Wide: x, y1 [, y1_err_low, y1_err_high], y2, ...
        out << field(xName);
        for (size_t i = 0; i < sets.size(); ++i) {
            out << ',' << field(name(sets[i], i));
            if (!sets[i]->yErrLow.isEmpty())
                out << ',' << field(name(sets[i], i) + " err low") << ',' << field(name(sets[i], i) + " err high");
        }
        out << '\n';
        const QVector<double>& x = sets.front()->x;
        for (qsizetype k = 0; k < x.size(); ++k) {
            if (!inside(x[k])) continue;
            out << num(x[k]);
            for (const Dataset* d : sets) {
                out << ',' << num(d->y[k]);
                if (!d->yErrLow.isEmpty()) out << ',' << num(d->yErrLow[k]) << ',' << num(d->yErrHigh[k]);
            }
            out << '\n';
        }
    } else {
        // Long: dataset, x, y [, errors]
        bool errs = false;
        for (const Dataset* d : sets) errs = errs || hasErrors(*d);
        out << "dataset," << field(xName) << ',' << field(ax.yLabel.isEmpty() ? QStringLiteral("y") : ax.yLabel);
        if (errs) out << ",y err low,y err high,x err low,x err high";
        out << '\n';
        for (size_t i = 0; i < sets.size(); ++i) {
            const Dataset* d = sets[i];
            for (qsizetype k = 0; k < d->x.size(); ++k) {
                if (!inside(d->x[k])) continue;
                out << field(name(d, i)) << ',' << num(d->x[k]) << ',' << num(d->y[k]);
                if (errs) {
                    auto at = [k](const QVector<double>& v) { return v.isEmpty() ? QString() : num(v[k]); };
                    out << ',' << at(d->yErrLow) << ',' << at(d->yErrHigh) << ',' << at(d->xErrLow) << ','
                        << at(d->xErrHigh);
                }
                out << '\n';
            }
        }
    }
    out.flush();
    if (!f.commit()) {
        if (error) *error = QString("Cannot write '%1': %2").arg(fileName, f.errorString());
        return false;
    }
    return true;
}

QString statisticsTsv(const PlotModel& model, double x0, double x1)
{
    QString s = "dataset\tn\tmean\tstd\tmin\tx at min\tmax\tx at max\trms\tintegral\n";
    for (const SignalStats& st : rangeStats(model, x0, x1)) {
        s += QStringList{st.name, QString::number(st.n), num(st.mean), num(st.std), num(st.min), num(st.xAtMin),
                         num(st.max), num(st.xAtMax), num(st.rms), num(st.integral)}
                 .join('\t') +
             '\n';
    }
    return s;
}

}  // namespace gp
