// Plain value types describing everything that can live on a plot.
// These types depend only on QtCore/QtGui value classes (QString, QColor,
// QVector) so the data model stays independent of widgets and rendering.
#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace gp {

using ObjectId = quint64;
constexpr ObjectId kInvalidId = 0;

enum class LineStyle { Solid, Dash, Dot, DashDot, None };
enum class ScatterShape { None, Circle, Square, Triangle, Cross, Plus, Diamond };
enum class Orientation { Vertical, Horizontal };

// Kind of an interactive plot object; used by selection and hit-testing.
enum class ObjectKind { None, Dataset, Marker, Annotation, Region };

struct ObjectRef {
    ObjectKind kind = ObjectKind::None;
    ObjectId id = kInvalidId;
    bool isValid() const { return kind != ObjectKind::None; }
    bool operator==(const ObjectRef&) const = default;
};

struct DatasetStyle {
    QColor color;  // invalid = "use next palette colour" (see PlotEditor::addDataset)
    double lineWidth = 1.5;
    LineStyle lineStyle = LineStyle::Solid;
    ScatterShape scatter = ScatterShape::None;
    double scatterSize = 6.0;
    bool visible = true;
    bool operator==(const DatasetStyle&) const = default;
};

// QVector is implicitly shared (copy-on-write), so copying a Dataset - e.g.
// into an undo command - is O(1) until one of the copies is modified.
struct Dataset {
    ObjectId id = kInvalidId;
    QString name;
    QVector<double> x;
    QVector<double> y;
    DatasetStyle style;
};

struct Marker {
    ObjectId id = kInvalidId;
    Orientation orientation = Orientation::Vertical;
    double position = 0.0;  // x for vertical, y for horizontal markers (data units)
    QString label;
    QColor color = QColor(0xe3, 0x49, 0x48);
    LineStyle lineStyle = LineStyle::Dash;
    double lineWidth = 1.5;
    bool showValue = true;  // show the numeric coordinate next to the label
    bool operator==(const Marker&) const = default;
};

struct Annotation {
    ObjectId id = kInvalidId;
    double x = 0.0;  // anchor in data coordinates
    double y = 0.0;
    QString text;
    QColor color = QColor(0x0b, 0x0b, 0x0b);
    double fontSize = 10.0;  // points
    bool operator==(const Annotation&) const = default;
};

// Region selected along the X axis, in data coordinates.
struct Region {
    bool active = false;
    double xmin = 0.0;
    double xmax = 0.0;
    bool operator==(const Region&) const = default;
};

struct AxesInfo {
    QString title;
    QString xLabel;
    QString yLabel;
    bool legendVisible = true;
    bool gridVisible = true;
    bool operator==(const AxesInfo&) const = default;
};

// Default categorical palette (validated for colour-vision deficiency
// separation on a white background). Assigned in fixed order.
QColor paletteColor(int index);

// Parses "blue", "#ff0000", "r", "tab:orange"-like names; returns invalid QColor on failure.
QColor parseColor(const QString& name);

// Parses matplotlib-like line style strings: "-", "--", ":", "-.", "none".
LineStyle parseLineStyle(const QString& s, bool* ok = nullptr);
QString lineStyleName(LineStyle s);

ScatterShape parseScatterShape(const QString& s, bool* ok = nullptr);
QString scatterShapeName(ScatterShape s);

}  // namespace gp
