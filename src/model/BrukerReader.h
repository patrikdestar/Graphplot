// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
// Reader for Bruker TopSpin processed 2D spectra (pdata/<n>/2rr).
//
// File layout (as implemented by TopSpin and documented by nmrglue):
//  * procs  - parameters of the direct dimension F2 (x axis)
//  * proc2s - parameters of the indirect dimension F1 (y axis)
//  * 2rr    - real/real data, int32 (DTYPP = 0) or float64 (DTYPP = 2),
//             byte order BYTORDP (0 = little, 1 = big endian), stored in
//             submatrix tiles of XDIM(F1) x XDIM(F2) points, tiles row-major.
//  * value  = stored * 2^NC_proc
//  * ppm of point i = OFFSET - i * SW_p / (SF * SI)    (point 0 = left/high ppm)
#pragma once

#include "PlotTypes.h"

#include <QMap>
#include <QString>

namespace gp {

// Parses a JCAMP-DX style Bruker parameter file ("##$KEY= value").
// Returns an empty map and sets *error if the file cannot be read.
QMap<QString, QString> readBrukerParameters(const QString& fileName, QString* error = nullptr);

struct BrukerSpectrum {
    Matrix matrix;        // x = F2 (direct), y = F1 (indirect), coordinates in ppm
    QString nucleusF2;    // e.g. "1H"
    QString nucleusF1;    // e.g. "13C"
    QString title;        // first line of pdata/<n>/title, if present
    QString error;        // empty on success
};

// `path` may be the pdata/<n> directory, the 2rr file itself, or the
// experiment directory (then pdata/1 is used).
BrukerSpectrum readBruker2D(const QString& path);

// Writes a spectrum in the same format (used by tests and for round trips).
bool writeBruker2D(const QString& pdataDir, const Matrix& m, const QString& nucF2,
                   const QString& nucF1, double sfF2, double sfF1, int xdimF2, int xdimF1,
                   QString* error = nullptr);

}  // namespace gp
