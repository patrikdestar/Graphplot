// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "ModelJson.h"

#include "PlotModel.h"

#include <QJsonArray>
#include <QtEndian>
#include <cstring>

namespace gp {

namespace {

// ---- primitives --------------------------------------------------------------------

QString encodeArray(const QVector<double>& v)
{
    QByteArray raw(v.size() * qsizetype(sizeof(double)), Qt::Uninitialized);
    for (qsizetype i = 0; i < v.size(); ++i) {
        quint64 u;
        std::memcpy(&u, &v[i], 8);
        u = qToLittleEndian(u);
        std::memcpy(raw.data() + i * 8, &u, 8);
    }
    return QString::fromLatin1(raw.toBase64());
}

bool decodeArray(const QJsonValue& value, QVector<double>* out)
{
    if (value.isUndefined() || value.isNull()) {
        out->clear();
        return true;
    }
    const QByteArray raw = QByteArray::fromBase64(value.toString().toLatin1());
    if (raw.size() % 8 != 0) return false;
    out->resize(raw.size() / 8);
    for (qsizetype i = 0; i < out->size(); ++i) {
        quint64 u;
        std::memcpy(&u, raw.constData() + i * 8, 8);
        u = qFromLittleEndian(u);
        std::memcpy(&(*out)[i], &u, 8);
    }
    return true;
}

QString colorText(const QColor& c) { return c.isValid() ? c.name(QColor::HexArgb) : QString(); }
QColor colorFrom(const QJsonValue& v)
{
    const QString s = v.toString();
    return s.isEmpty() ? QColor() : QColor::fromString(s);
}

ObjectId idFrom(const QJsonValue& v) { return ObjectId(v.toString().toULongLong()); }
QString idText(ObjectId id) { return QString::number(id); }  // 64-bit ids as text (JSON doubles lose bits)

// ---- objects ------------------------------------------------------------------------

QJsonObject datasetToJson(const Dataset& d)
{
    const DatasetStyle& s = d.style;
    QJsonObject o{{"id", idText(d.id)},
                  {"name", d.name},
                  {"x", encodeArray(d.x)},
                  {"y", encodeArray(d.y)},
                  {"color", colorText(s.color)},
                  {"lineWidth", s.lineWidth},
                  {"lineStyle", lineStyleName(s.lineStyle)},
                  {"symbol", scatterShapeName(s.scatter)},
                  {"symbolSize", s.scatterSize},
                  {"step", stepModeName(s.step)},
                  {"fillColor", colorText(s.fillColor)},
                  {"fillTo", idText(s.fillTo)},
                  {"visible", s.visible},
                  {"inLegend", s.inLegend}};
    if (!d.yErrLow.isEmpty()) o["yErrLow"] = encodeArray(d.yErrLow), o["yErrHigh"] = encodeArray(d.yErrHigh);
    if (!d.xErrLow.isEmpty()) o["xErrLow"] = encodeArray(d.xErrLow), o["xErrHigh"] = encodeArray(d.xErrHigh);
    return o;
}

bool datasetFromJson(const QJsonObject& o, Dataset* d)
{
    d->id = idFrom(o["id"]);
    d->name = o["name"].toString();
    DatasetStyle& s = d->style;
    s.color = colorFrom(o["color"]);
    s.lineWidth = o["lineWidth"].toDouble();
    s.lineStyle = parseLineStyle(o["lineStyle"].toString());
    s.scatter = parseScatterShape(o["symbol"].toString());
    s.scatterSize = o["symbolSize"].toDouble();
    s.step = parseStepMode(o["step"].toString());
    s.fillColor = colorFrom(o["fillColor"]);
    s.fillTo = idFrom(o["fillTo"]);
    s.visible = o["visible"].toBool(true);
    s.inLegend = o["inLegend"].toBool(true);
    return decodeArray(o["x"], &d->x) && decodeArray(o["y"], &d->y) && d->x.size() == d->y.size() &&
           decodeArray(o["yErrLow"], &d->yErrLow) && decodeArray(o["yErrHigh"], &d->yErrHigh) &&
           decodeArray(o["xErrLow"], &d->xErrLow) && decodeArray(o["xErrHigh"], &d->xErrHigh);
}

QJsonObject matrixToJson(const Matrix& m)
{
    const ContourStyle& s = m.style;
    return {{"id", idText(m.id)},
            {"name", m.name},
            {"nx", m.nx},
            {"ny", m.ny},
            {"z", encodeArray(m.z)},
            {"extent", QJsonArray{m.x0, m.x1, m.y0, m.y1}},
            {"baseLevel", s.baseLevel},
            {"factor", s.factor},
            {"levels", s.levelCount},
            {"negative", s.showNegative},
            {"color", colorText(s.positiveColor)},
            {"negativeColor", colorText(s.negativeColor)},
            {"lineWidth", s.lineWidth},
            {"image", s.showImage},
            {"visible", s.visible},
            {"contours", s.showContours},
            {"colormap", s.colormap},
            {"colorBar", s.colorBar},
            {"clim", s.fixedRange ? QJsonValue(QJsonArray{s.vmin, s.vmax}) : QJsonValue()}};
}

bool matrixFromJson(const QJsonObject& o, Matrix* m)
{
    m->id = idFrom(o["id"]);
    m->name = o["name"].toString();
    m->nx = o["nx"].toInt();
    m->ny = o["ny"].toInt();
    const QJsonArray e = o["extent"].toArray();
    if (e.size() != 4) return false;
    m->x0 = e[0].toDouble(), m->x1 = e[1].toDouble(), m->y0 = e[2].toDouble(), m->y1 = e[3].toDouble();
    ContourStyle& s = m->style;
    s.baseLevel = o["baseLevel"].toDouble();
    s.factor = o["factor"].toDouble();
    s.levelCount = o["levels"].toInt();
    s.showNegative = o["negative"].toBool(true);
    s.positiveColor = colorFrom(o["color"]);
    s.negativeColor = colorFrom(o["negativeColor"]);
    s.lineWidth = o["lineWidth"].toDouble();
    s.showImage = o["image"].toBool();
    s.visible = o["visible"].toBool(true);
    s.showContours = o["contours"].toBool(true);
    s.colormap = o["colormap"].toString();
    s.colorBar = o["colorBar"].toBool();
    const QJsonArray clim = o["clim"].toArray();
    s.fixedRange = clim.size() == 2 && clim[1].toDouble() > clim[0].toDouble();
    if (s.fixedRange) s.vmin = clim[0].toDouble(), s.vmax = clim[1].toDouble();
    return decodeArray(o["z"], &m->z) && m->nx >= 2 && m->ny >= 2 &&
           m->z.size() == qsizetype(m->nx) * m->ny;
}

QJsonObject markerToJson(const Marker& m)
{
    return {{"id", idText(m.id)},
            {"orientation", m.orientation == Orientation::Vertical ? "v" : "h"},
            {"position", m.position},
            {"label", m.label},
            {"color", colorText(m.color)},
            {"lineStyle", lineStyleName(m.lineStyle)},
            {"lineWidth", m.lineWidth},
            {"showValue", m.showValue}};
}

Marker markerFromJson(const QJsonObject& o)
{
    Marker m;
    m.id = idFrom(o["id"]);
    m.orientation = o["orientation"].toString() == "h" ? Orientation::Horizontal : Orientation::Vertical;
    m.position = o["position"].toDouble();
    m.label = o["label"].toString();
    m.color = colorFrom(o["color"]);
    m.lineStyle = parseLineStyle(o["lineStyle"].toString());
    m.lineWidth = o["lineWidth"].toDouble(1.5);
    m.showValue = o["showValue"].toBool(true);
    return m;
}

QJsonObject annotationToJson(const Annotation& a)
{
    return {{"id", idText(a.id)}, {"x", a.x}, {"y", a.y}, {"text", a.text},
            {"color", colorText(a.color)}, {"fontSize", a.fontSize}};
}

Annotation annotationFromJson(const QJsonObject& o)
{
    Annotation a;
    a.id = idFrom(o["id"]);
    a.x = o["x"].toDouble();
    a.y = o["y"].toDouble();
    a.text = o["text"].toString();
    a.color = colorFrom(o["color"]);
    a.fontSize = o["fontSize"].toDouble();
    return a;
}

QJsonObject peakToJson(const Peak& p)
{
    return {{"id", idText(p.id)},
            {"matrix", idText(p.matrixId)},
            {"x", p.x},
            {"y", p.y},
            {"intensity", p.intensity},
            {"label", p.label},
            {"comment", p.comment},
            {"labelOffset", QJsonArray{p.labelOffset.x(), p.labelOffset.y()}}};
}

Peak peakFromJson(const QJsonObject& o)
{
    Peak p;
    p.id = idFrom(o["id"]);
    p.matrixId = idFrom(o["matrix"]);
    p.x = o["x"].toDouble();
    p.y = o["y"].toDouble();
    p.intensity = o["intensity"].toDouble();
    p.label = o["label"].toString();
    p.comment = o["comment"].toString();
    const QJsonArray off = o["labelOffset"].toArray();
    if (off.size() == 2) p.labelOffset = QPointF(off[0].toDouble(), off[1].toDouble());
    return p;
}

QJsonObject axesToJson(const AxesInfo& a)
{
    return {{"title", a.title},       {"xLabel", a.xLabel},       {"yLabel", a.yLabel},
            {"legend", a.legendVisible}, {"grid", a.gridVisible},  {"xReversed", a.xReversed},
            {"yReversed", a.yReversed}, {"diagonal", a.showDiagonal}, {"peakLabels", a.showPeakLabels},
            {"xLog", a.xLog},         {"yLog", a.yLog}};
}

AxesInfo axesFromJson(const QJsonObject& o)
{
    AxesInfo a;
    a.title = o["title"].toString();
    a.xLabel = o["xLabel"].toString();
    a.yLabel = o["yLabel"].toString();
    a.legendVisible = o["legend"].toBool(true);
    a.gridVisible = o["grid"].toBool(true);
    a.xReversed = o["xReversed"].toBool();
    a.yReversed = o["yReversed"].toBool();
    a.showDiagonal = o["diagonal"].toBool();
    a.showPeakLabels = o["peakLabels"].toBool(true);
    a.xLog = o["xLog"].toBool();
    a.yLog = o["yLog"].toBool();
    return a;
}

}  // namespace

QJsonObject modelToJson(const PlotModel& model)
{
    QJsonArray datasets, matrices, markers, annotations, peaks;
    for (const Dataset& d : model.datasets()) datasets << datasetToJson(d);
    for (const Matrix& m : model.matrices()) matrices << matrixToJson(m);
    for (const Marker& m : model.markers()) markers << markerToJson(m);
    for (const Annotation& a : model.annotations()) annotations << annotationToJson(a);
    for (const Peak& p : model.peaks()) peaks << peakToJson(p);
    const Region& r = model.region();
    return {{"datasets", datasets},
            {"matrices", matrices},
            {"markers", markers},
            {"annotations", annotations},
            {"peaks", peaks},
            {"region", r.active ? QJsonValue(QJsonArray{r.xmin, r.xmax}) : QJsonValue()},
            {"axes", axesToJson(model.axesInfo())},
            {"style", model.style().toText()}};
}

bool modelFromJson(const QJsonObject& json, PlotModel& model, QString* error)
{
    auto fail = [error](const QString& what) {
        if (error) *error = what;
        return false;
    };
    // Style and axes first, so the content is drawn with them right away.
    if (json.contains("style")) {
        Style s = model.style();
        QStringList problems;
        s.parse(json["style"].toString(), &problems);  // unknown keys (newer files) are skipped
        model.setStyle(s);
    }
    model.setAxesInfo(axesFromJson(json["axes"].toObject()));
    for (const QJsonValue& v : json["matrices"].toArray()) {
        Matrix m;
        if (!matrixFromJson(v.toObject(), &m)) return fail("invalid 2D dataset '" + m.name + "'");
        model.insertMatrix(std::move(m));
    }
    for (const QJsonValue& v : json["datasets"].toArray()) {
        Dataset d;
        if (!datasetFromJson(v.toObject(), &d)) return fail("invalid dataset '" + d.name + "'");
        model.insertDataset(std::move(d));
    }
    for (const QJsonValue& v : json["markers"].toArray()) model.insertMarker(markerFromJson(v.toObject()));
    for (const QJsonValue& v : json["annotations"].toArray())
        model.insertAnnotation(annotationFromJson(v.toObject()));
    for (const QJsonValue& v : json["peaks"].toArray()) model.insertPeak(peakFromJson(v.toObject()));
    const QJsonArray r = json["region"].toArray();
    if (r.size() == 2) model.setRegion(Region{true, r[0].toDouble(), r[1].toDouble()});
    return true;
}

}  // namespace gp
