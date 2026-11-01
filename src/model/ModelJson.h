// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
//
// Saving and restoring the complete content of one panel (PlotModel) as
// JSON: datasets with their error bars, 2D data, markers, annotations,
// peaks (labels and comments), the region selection, axes settings and the
// style. Used by figure files (.gpfig, see gui/FigureFile).
//
// Number arrays are stored as base64-encoded little-endian float64, so values
// round-trip exactly and files stay compact; everything else is readable JSON.
#pragma once

#include <QJsonObject>
#include <QString>

namespace gp {

class PlotModel;

QJsonObject modelToJson(const PlotModel& model);

// Adds the stored content to `model` (normally empty); object ids are kept.
// Returns false and sets `error` for malformed input (the model may then hold
// part of the content).
bool modelFromJson(const QJsonObject& json, PlotModel& model, QString* error = nullptr);

}  // namespace gp
