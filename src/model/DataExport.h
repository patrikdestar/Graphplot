// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
//
// Writing the data of a panel (not the picture) as CSV, e.g. to continue in a
// spreadsheet or another program.
//
// * When every exported dataset has the same x values, one wide table is
//   written: x, then one column per dataset (plus error columns if any).
// * Otherwise a long table: dataset, x, y (and error columns), one row per
//   point - easy to filter or pivot.
// Only visible datasets are written; with a region, only points inside it.
#pragma once

#include "PlotTypes.h"

#include <QString>
#include <optional>

namespace gp {

class PlotModel;

bool writeDataCsv(const PlotModel& model, const QString& fileName, std::optional<Region> region = std::nullopt,
                  QString* error = nullptr);

// The region statistics (see Statistics.h) as tab-separated text with a
// header row, ready to paste into a spreadsheet.
QString statisticsTsv(const PlotModel& model, double x0, double x1);

}  // namespace gp
