// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
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

// ---- helpers shared by the text loaders --------------------------------------
// Field separator of a delimited line: tab, then ';' (files with decimal
// commas use it), then ','; otherwise runs of whitespace (returned as ' ').
QChar detectSeparator(const QString& line);
// Splits on `sep` keeping empty fields (",," is a missing value); quotes are
// stripped. With ' ' runs of whitespace separate fields.
QStringList splitFields(const QString& line, QChar sep);
// Number or NaN for empty / "nan" cells; sets *ok = false for other text.
double parseCell(const QString& cell, bool* ok);

struct MatrixLoadResult {
    Matrix matrix;   // id unassigned
    QString error;
};

// Reads a 2D grid: each line is one row (constant y), each column one x
// position. The extent defaults to the index range; it can be given with a
// comment line "# extent: x0 x1 y0 y1".
MatrixLoadResult loadMatrixFile(const QString& fileName);

}  // namespace gp
