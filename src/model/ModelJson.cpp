// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "ModelJson.h"

#include "PlotModel.h"

#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
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

// Scalars: JSON has no NaN / infinity (QJsonDocument writes them as null),
// so non-finite values are stored as the strings "nan", "inf", "-inf".
QJsonValue jnum(double v)
{
    if (std::isfinite(v)) return v;
    return std::isnan(v) ? QStringLiteral("nan") : v > 0 ? QStringLiteral("inf") : QStringLiteral("-inf");
}

double jdbl(const QJsonValue& v, double fallback = 0.0)
{
    if (v.isString()) {
        const QString s = v.toString();
        if (s == "nan") return std::numeric_limits<double>::quiet_NaN();
        if (s == "inf") return std::numeric_limits<double>::infinity();
        if (s == "-inf") return -std::numeric_limits<double>::infinity();
    }
    return v.toDouble(fallback);
}

QString colorText(const QColor& c) { return c.isValid() ? c.name(QColor::HexArgb) : QString(); }
QColor colorFrom(const QJsonValue& v)
{
    const QString s = v.toString();
    return s.isEmpty() ? QColor() : colorFromString(s);
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
                  {"markerEdge", colorText(s.markerEdge)},
                  {"markerFace", colorText(s.markerFace)},
                  {"exactColor", s.exactColor},
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
    s.markerEdge = colorFrom(o["markerEdge"]);
    s.markerFace = colorFrom(o["markerFace"]);
    s.exactColor = o["exactColor"].toBool();
    s.step = parseStepMode(o["step"].toString());
    s.fillColor = colorFrom(o["fillColor"]);
    s.fillTo = idFrom(o["fillTo"]);
    s.visible = o["visible"].toBool(true);
    s.inLegend = o["inLegend"].toBool(true);
    if (!(decodeArray(o["x"], &d->x) && decodeArray(o["y"], &d->y) && d->x.size() == d->y.size() &&
          decodeArray(o["yErrLow"], &d->yErrLow) && decodeArray(o["yErrHigh"], &d->yErrHigh) &&
          decodeArray(o["xErrLow"], &d->xErrLow) && decodeArray(o["xErrHigh"], &d->xErrHigh)))
        return false;
    // Error bars come in (low, high) pairs of the data's length, or not at all.
    for (auto [lo, hi] : {std::pair{&d->yErrLow, &d->yErrHigh}, std::pair{&d->xErrLow, &d->xErrHigh}})
        if (lo->isEmpty() != hi->isEmpty() || (!lo->isEmpty() && (lo->size() != d->x.size() || hi->size() != d->x.size())))
            return false;
    return true;
}

QJsonObject matrixToJson(const Matrix& m)
{
    const ContourStyle& s = m.style;
    return {{"id", idText(m.id)},
            {"name", m.name},
            {"nx", m.nx},
            {"ny", m.ny},
            {"z", encodeArray(m.z)},
            {"extent", QJsonArray{jnum(m.x0), jnum(m.x1), jnum(m.y0), jnum(m.y1)}},
            {"baseLevel", jnum(s.baseLevel)},
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
            {"smooth", s.smooth},
            {"filled", s.filled},
            {"negativeDashed", s.negativeDashed},
            {"labelled", s.labelled},
            {"levelValues", [&] {
                 QJsonArray a;
                 for (double l : s.levels) a << jnum(l);
                 return a;
             }()},
            {"colorBar", s.colorBar},
            {"clim", s.fixedRange ? QJsonValue(QJsonArray{jnum(s.vmin), jnum(s.vmax)}) : QJsonValue()}};
}

bool matrixFromJson(const QJsonObject& o, Matrix* m)
{
    m->id = idFrom(o["id"]);
    m->name = o["name"].toString();
    m->nx = o["nx"].toInt();
    m->ny = o["ny"].toInt();
    const QJsonArray e = o["extent"].toArray();
    if (e.size() != 4) return false;
    m->x0 = jdbl(e[0]), m->x1 = jdbl(e[1]), m->y0 = jdbl(e[2]), m->y1 = jdbl(e[3]);
    ContourStyle& s = m->style;
    s.baseLevel = jdbl(o["baseLevel"]);
    s.factor = o["factor"].toDouble();
    s.levelCount = std::clamp(o["levels"].toInt(), 0, 1000);  // a hostile value must not exhaust memory
    s.showNegative = o["negative"].toBool(true);
    s.positiveColor = colorFrom(o["color"]);
    s.negativeColor = colorFrom(o["negativeColor"]);
    s.lineWidth = o["lineWidth"].toDouble();
    s.showImage = o["image"].toBool();
    s.visible = o["visible"].toBool(true);
    s.showContours = o["contours"].toBool(true);
    s.colormap = o["colormap"].toString();
    s.smooth = o["smooth"].toBool(true);
    s.filled = o["filled"].toBool();
    s.negativeDashed = o["negativeDashed"].toBool();
    s.labelled = o["labelled"].toBool(true);
    for (const QJsonValue& l : o["levelValues"].toArray()) s.levels << jdbl(l);
    s.colorBar = o["colorBar"].toBool();
    const QJsonArray clim = o["clim"].toArray();
    s.fixedRange = clim.size() == 2 && jdbl(clim[1]) > jdbl(clim[0]);
    if (s.fixedRange) s.vmin = jdbl(clim[0]), s.vmax = jdbl(clim[1]);
    return decodeArray(o["z"], &m->z) && m->nx >= 2 && m->ny >= 2 &&
           m->z.size() == qsizetype(m->nx) * m->ny;
}

QJsonObject markerToJson(const Marker& m)
{
    return {{"id", idText(m.id)},
            {"orientation", m.orientation == Orientation::Vertical ? "v" : "h"},
            {"position", jnum(m.position)},
            {"label", m.label},
            {"color", colorText(m.color)},
            {"lineStyle", lineStyleName(m.lineStyle)},
            {"lineWidth", m.lineWidth},
            {"showValue", m.showValue},
            {"spanEnd", m.span ? QJsonValue(jnum(m.spanEnd)) : QJsonValue()},
            {"inLegend", m.inLegend}};
}

Marker markerFromJson(const QJsonObject& o)
{
    Marker m;
    m.id = idFrom(o["id"]);
    m.orientation = o["orientation"].toString() == "h" ? Orientation::Horizontal : Orientation::Vertical;
    m.position = jdbl(o["position"]);
    m.label = o["label"].toString();
    m.color = colorFrom(o["color"]);
    m.lineStyle = parseLineStyle(o["lineStyle"].toString());
    m.lineWidth = o["lineWidth"].toDouble(1.5);
    m.showValue = o["showValue"].toBool(true);
    m.span = o.contains("spanEnd") && !o["spanEnd"].isNull();
    if (m.span) m.spanEnd = jdbl(o["spanEnd"]);
    m.inLegend = o["inLegend"].toBool();
    return m;
}

QJsonObject annotationToJson(const Annotation& a)
{
    QJsonObject o{{"id", idText(a.id)}, {"x", jnum(a.x)}, {"y", jnum(a.y)}, {"text", a.text},
                  {"color", colorText(a.color)}, {"fontSize", a.fontSize}, {"alignment", a.alignment}};
    if (a.axesCoords) o["coords"] = "axes";
    if (a.arrow) o["arrowTo"] = QJsonArray{jnum(a.arrowX), jnum(a.arrowY)};
    return o;
}

Annotation annotationFromJson(const QJsonObject& o)
{
    Annotation a;
    a.id = idFrom(o["id"]);
    a.x = jdbl(o["x"]);
    a.y = jdbl(o["y"]);
    a.text = o["text"].toString();
    a.color = colorFrom(o["color"]);
    a.fontSize = o["fontSize"].toDouble();
    a.alignment = o["alignment"].toInt(a.alignment);
    a.axesCoords = o["coords"].toString() == "axes";
    const QJsonArray to = o["arrowTo"].toArray();
    a.arrow = to.size() == 2;
    if (a.arrow) a.arrowX = jdbl(to[0]), a.arrowY = jdbl(to[1]);
    return a;
}

QJsonObject peakToJson(const Peak& p)
{
    return {{"id", idText(p.id)},
            {"matrix", idText(p.matrixId)},
            {"x", jnum(p.x)},
            {"y", jnum(p.y)},
            {"intensity", jnum(p.intensity)},
            {"label", p.label},
            {"comment", p.comment},
            {"labelOffset", QJsonArray{p.labelOffset.x(), p.labelOffset.y()}}};
}

Peak peakFromJson(const QJsonObject& o)
{
    Peak p;
    p.id = idFrom(o["id"]);
    p.matrixId = idFrom(o["matrix"]);
    p.x = jdbl(o["x"]);
    p.y = jdbl(o["y"]);
    p.intensity = jdbl(o["intensity"]);
    p.label = o["label"].toString();
    p.comment = o["comment"].toString();
    const QJsonArray off = o["labelOffset"].toArray();
    if (off.size() == 2) p.labelOffset = QPointF(off[0].toDouble(), off[1].toDouble());
    return p;
}

QJsonValue ticksToJson(bool custom, const QVector<double>& pos, const QStringList& labels);

QJsonObject axesToJson(const AxesInfo& a)
{
    return {{"xTicks", ticksToJson(a.customXTicks, a.xTicks, a.xTickLabels)},
            {"yTicks", ticksToJson(a.customYTicks, a.yTicks, a.yTickLabels)},{"title", a.title}, {"suptitle", a.suptitle},       {"xLabel", a.xLabel},       {"yLabel", a.yLabel},
            {"legend", a.legendVisible}, {"grid", a.gridVisible},  {"xReversed", a.xReversed},
            {"yReversed", a.yReversed}, {"diagonal", a.showDiagonal}, {"peakLabels", a.showPeakLabels},
            {"xLog", a.xLog},         {"yLog", a.yLog}, {"xTime", a.xTime},
            {"legendLoc", a.legendLoc}, {"supXLabel", a.supXLabel}, {"supYLabel", a.supYLabel}, {"legendWithTwin", a.legendWithTwin}, {"xTickRotation", a.xTickRotation},
            {"yTickRotation", a.yTickRotation}};
}

QJsonValue ticksToJson(bool custom, const QVector<double>& pos, const QStringList& labels)
{
    if (!custom) return QJsonValue();
    QJsonArray p, l;
    for (double v : pos) p << jnum(v);
    for (const QString& t : labels) l << t;
    return QJsonObject{{"positions", p}, {"labels", l}};
}

void ticksFromJson(const QJsonValue& v, bool* custom, QVector<double>* pos, QStringList* labels)
{
    *custom = v.isObject();
    if (!*custom) return;
    for (const QJsonValue& p : v["positions"].toArray()) *pos << jdbl(p);
    for (const QJsonValue& l : v["labels"].toArray()) *labels << l.toString();
}

AxesInfo axesFromJson(const QJsonObject& o)
{
    AxesInfo a;
    a.title = o["title"].toString();
    a.suptitle = o["suptitle"].toString();
    a.xLabel = o["xLabel"].toString();
    a.yLabel = o["yLabel"].toString();
    a.legendVisible = o["legend"].toBool(true);
    a.gridVisible = o["grid"].toBool(true);
    a.xReversed = o["xReversed"].toBool();
    a.yReversed = o["yReversed"].toBool();
    a.showDiagonal = o["diagonal"].toBool();
    a.showPeakLabels = o["peakLabels"].toBool(true);
    a.xLog = o["xLog"].toBool();
    a.xTime = o["xTime"].toBool();
    a.legendLoc = o["legendLoc"].toString();
    a.supXLabel = o["supXLabel"].toString();
    a.supYLabel = o["supYLabel"].toString();
    a.legendWithTwin = o["legendWithTwin"].toBool();
    a.xTickRotation = std::clamp(o["xTickRotation"].toDouble(), -90.0, 90.0);
    a.yTickRotation = std::clamp(o["yTickRotation"].toDouble(), -90.0, 90.0);
    a.yLog = o["yLog"].toBool();
    ticksFromJson(o["xTicks"], &a.customXTicks, &a.xTicks, &a.xTickLabels);
    ticksFromJson(o["yTicks"], &a.customYTicks, &a.yTicks, &a.yTickLabels);
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
            {"region", r.active ? QJsonValue(QJsonArray{jnum(r.xmin), jnum(r.xmax)}) : QJsonValue()},
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
    // Ids must be present and unique (the model allocates them from one
    // counter for all kinds; handles and peak / fill references use them).
    QSet<ObjectId> used;
    auto fresh = [&used](ObjectId id) {
        if (id == kInvalidId || used.contains(id)) return false;
        used.insert(id);
        return true;
    };
    for (const QJsonValue& v : json["matrices"].toArray()) {
        Matrix m;
        if (!matrixFromJson(v.toObject(), &m)) return fail("invalid 2D dataset '" + m.name + "'");
        if (!fresh(m.id)) return fail("duplicate or missing id in 2D dataset '" + m.name + "'");
        model.insertMatrix(std::move(m));
    }
    for (const QJsonValue& v : json["datasets"].toArray()) {
        Dataset d;
        if (!datasetFromJson(v.toObject(), &d)) return fail("invalid dataset '" + d.name + "'");
        if (!fresh(d.id)) return fail("duplicate or missing id in dataset '" + d.name + "'");
        model.insertDataset(std::move(d));
    }
    for (const QJsonValue& v : json["markers"].toArray()) {
        Marker m = markerFromJson(v.toObject());
        if (!fresh(m.id)) return fail("duplicate or missing marker id");
        model.insertMarker(m);
    }
    for (const QJsonValue& v : json["annotations"].toArray()) {
        Annotation a = annotationFromJson(v.toObject());
        if (!fresh(a.id)) return fail("duplicate or missing annotation id");
        model.insertAnnotation(a);
    }
    for (const QJsonValue& v : json["peaks"].toArray()) {
        Peak p = peakFromJson(v.toObject());
        if (!fresh(p.id)) return fail("duplicate or missing peak id");
        model.insertPeak(p);
    }
    const QJsonArray r = json["region"].toArray();
    if (r.size() == 2) model.setRegion(Region{true, jdbl(r[0]), jdbl(r[1])});
    return true;
}

}  // namespace gp
