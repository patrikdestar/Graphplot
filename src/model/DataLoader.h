// Reads simple column-oriented text files (CSV, TSV, whitespace separated).
// The first column is X; each further column becomes one dataset. An
// optional non-numeric first line supplies dataset names. Lines starting
// with '#' or '%' are comments.
#pragma once

#include "PlotTypes.h"

#include <QString>
#include <vector>

namespace gp {

struct LoadResult {
    std::vector<Dataset> datasets;  // ids unassigned, styles default
    QString error;                  // empty on success
};

LoadResult loadColumnFile(const QString& fileName);

}  // namespace gp
