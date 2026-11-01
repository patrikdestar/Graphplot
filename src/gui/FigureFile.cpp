// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
//
// FigureWindow::saveFigure / loadFigure: the .gpfig figure file.
//
// Layout (JSON, see docs/FILE_FORMATS.md):
//   { "format": "graphplot-figure", "version": 1, "title", "size": [w, h],
//     "current": panel index, "grid": {...} (multi-panel figures),
//     "panels": [ { "model": {...}, "view": [xmin, xmax, ymin, ymax] }, ... ],
//     "timeline": {...} (timeline figures; panels are then the rows, in order) }
#include "FigureWindow.h"

#include "Timeline.h"
#include "commands/PlotEditor.h"
#include "model/ModelJson.h"
#include "model/PlotModel.h"
#include "render/PlotRenderer.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUndoStack>

namespace gp {

namespace {

constexpr int kFormatVersion = 1;

QJsonArray viewToJson(const ViewRange& v) { return {v.xmin, v.xmax, v.ymin, v.ymax}; }

bool viewFromJson(const QJsonValue& value, ViewRange* v)
{
    const QJsonArray a = value.toArray();
    if (a.size() != 4) return false;
    *v = ViewRange{a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble()};
    return v->xmax > v->xmin && v->ymax > v->ymin;
}

QJsonArray doubles(const QList<double>& l)
{
    QJsonArray a;
    for (double d : l) a << d;
    return a;
}

QList<double> doublesFrom(const QJsonValue& v)
{
    QList<double> out;
    for (const QJsonValue& d : v.toArray()) out << d.toDouble();
    return out;
}

}  // namespace

bool FigureWindow::isEmpty() const
{
    for (const Axes& ax : m_axes) {
        const PlotModel* m = ax.model;
        if (!m->datasets().empty() || !m->matrices().empty() || !m->markers().empty() ||
            !m->annotations().empty() || !m->peaks().empty())
            return false;
    }
    return true;
}

bool FigureWindow::saveFigure(const QString& fileName, QString* error) const
{
    QJsonObject root{{"format", "graphplot-figure"},
                     {"version", kFormatVersion},
                     {"title", windowTitle()},
                     {"current", currentAxes()}};
    if (m_figureSize.isValid()) root["size"] = QJsonArray{m_figureSize.width(), m_figureSize.height()};

    // Which panels, in which order: timeline rows in display order, else all.
    QList<int> panels;
    if (m_timeline) {
        panels = m_timeline->rows();
    } else {
        for (int i = 0; i < axesCount(); ++i) panels << i;
        const GridSpec& g = m_gridSpec;
        if (axesCount() > 1 || g.rows * g.cols > 1)
            root["grid"] = QJsonObject{{"rows", g.rows},          {"cols", g.cols},
                                       {"heightRatios", doubles(g.heightRatios)},
                                       {"widthRatios", doubles(g.widthRatios)},
                                       {"hspace", g.hspace},    {"wspace", g.wspace},
                                       {"sharex", g.sharex},    {"sharey", g.sharey}};
    }
    QJsonArray panelArray;
    for (int i : panels) {
        const Axes& ax = m_axes[size_t(i)];
        panelArray << QJsonObject{{"model", modelToJson(*ax.model)},
                                  {"view", viewToJson(ax.renderer->viewRange())}};
    }
    root["panels"] = panelArray;

    if (m_timeline) {
        const Timeline* tl = m_timeline;
        QJsonArray rows, pins;
        for (int row : tl->rows())
            rows << QJsonObject{{"name", tl->rowName(row)},
                                {"height", tl->rowHeight(row)},
                                {"visible", tl->rowVisible(row)}};
        for (const TimePin& p : tl->pins())
            pins << QJsonObject{{"t", p.t}, {"label", p.label}, {"color", p.color.name(QColor::HexArgb)}};
        const auto [w0, w1] = tl->window();
        const SignalRef nav = tl->navigatorSignal();
        QJsonObject t{{"rows", rows},
                      {"pins", pins},
                      {"window", QJsonArray{w0, w1}},
                      {"autoFitY", tl->autoFitY()},
                      {"clockTime", tl->clockTime()},
                      {"navigator", QJsonArray{int(tl->rows().indexOf(nav.row)), QString::number(nav.id)}}};
        if (tl->hasAbsoluteTime()) t["origin"] = tl->timeOrigin();
        root["timeline"] = t;
    }

    QSaveFile f(fileName);  // atomic: a failed save never destroys the old file
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = QString("Cannot write '%1': %2").arg(fileName, f.errorString());
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = QString("Cannot write '%1': %2").arg(fileName, f.errorString());
        return false;
    }
    const_cast<FigureWindow*>(this)->m_figureFile = fileName;
    return true;
}

bool FigureWindow::loadFigure(const QString& fileName, QString* error)
{
    auto fail = [&](const QString& what) {
        if (error) *error = what;
        return false;
    };
    if (!isEmpty()) return fail("the window already holds data; open the figure in a new window");
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly)) return fail(QString("Cannot open '%1': %2").arg(fileName, f.errorString()));
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (doc.isNull()) return fail(QString("Not a figure file (%1)").arg(perr.errorString()));
    const QJsonObject root = doc.object();
    if (root["format"].toString() != "graphplot-figure")
        return fail("Not a Graphplot figure file (missing \"format\": \"graphplot-figure\")");
    if (root["version"].toInt() > kFormatVersion)
        return fail("The figure was saved by a newer Graphplot version");

    const QJsonArray panels = root["panels"].toArray();
    if (panels.isEmpty()) return fail("The figure file contains no panels");
    if (panels.size() > 64) return fail("The figure file has too many panels");
    // Validate every panel before changing anything, so a damaged file
    // leaves the window untouched (and reusable).
    for (qsizetype i = 0; i < panels.size(); ++i) {
        PlotModel scratch;
        QString err;
        if (!modelFromJson(panels[i].toObject()["model"].toObject(), scratch, &err))
            return fail(QString("Panel %1: %2").arg(i + 1).arg(err));
    }
    const QJsonArray size = root["size"].toArray();
    if (size.size() == 2) setFigureSize(QSize(size[0].toInt(), size[1].toInt()));
    if (!root["title"].toString().isEmpty()) setWindowTitle(root["title"].toString());

    QList<int> target;  // panel index for each stored panel
    if (root.contains("timeline")) {
        const QJsonObject t = root["timeline"].toObject();
        const QJsonArray rows = t["rows"].toArray();
        Timeline* tl = timeline();
        for (qsizetype i = 0; i < panels.size(); ++i) {
            const QJsonObject r = rows.at(i).toObject();
            const int row = tl->addRow(r["name"].toString(QString("Row %1").arg(i + 1)));
            target << row;
        }
        if (t.contains("origin")) tl->setTimeOrigin(t["origin"].toDouble());
        for (qsizetype i = 0; i < panels.size(); ++i) {
            QString err;
            if (!modelFromJson(panels[i].toObject()["model"].toObject(), *m_axes[size_t(target[i])].model, &err))
                return fail(err);
            const QJsonObject r = rows.at(i).toObject();
            tl->setRowHeight(target[i], r["height"].toDouble(1.0));
            tl->setRowVisible(target[i], r["visible"].toBool(true));
        }
        tl->setAutoFitY(t["autoFitY"].toBool(true));
        // Y ranges as saved (with auto-fit on they are recomputed anyway);
        // the shared X range comes from "window" below.
        for (qsizetype i = 0; i < panels.size(); ++i) {
            PlotRenderer* r = m_axes[size_t(target[i])].renderer;
            ViewRange v;
            if (viewFromJson(panels[i].toObject()["view"], &v)) {
                ViewRange cur = r->viewRange();
                cur.ymin = v.ymin;
                cur.ymax = v.ymax;
                r->setViewRange(cur);
            } else {
                r->autoscale(false, true);
            }
        }
        if (tl->hasAbsoluteTime()) tl->setClockTime(t["clockTime"].toBool());
        const QJsonArray nav = t["navigator"].toArray();
        const int navRow = nav.size() == 2 ? nav[0].toInt(-1) : -1;
        if (navRow >= 0 && navRow < target.size())
            tl->setNavigatorSignal({target[navRow], ObjectId(nav[1].toString().toULongLong())});
        for (const QJsonValue& v : t["pins"].toArray()) {
            const QJsonObject p = v.toObject();
            tl->addPin(p["t"].toDouble(), p["label"].toString(), QColor::fromString(p["color"].toString()));
        }
        const QJsonArray w = t["window"].toArray();
        if (w.size() == 2 && w[1].toDouble() > w[0].toDouble()) tl->setWindow(w[0].toDouble(), w[1].toDouble());
        else tl->showAll();
    } else {
        if (root.contains("grid")) {
            const QJsonObject g = root["grid"].toObject();
            GridSpec spec;
            spec.rows = std::max(1, g["rows"].toInt(1));
            spec.cols = std::max(1, g["cols"].toInt(1));
            spec.heightRatios = doublesFrom(g["heightRatios"]);
            spec.widthRatios = doublesFrom(g["widthRatios"]);
            spec.hspace = g["hspace"].toInt(-1);
            spec.wspace = g["wspace"].toInt(-1);
            spec.sharex = g["sharex"].toBool();
            spec.sharey = g["sharey"].toBool();
            setGrid(spec);
        }
        while (axesCount() < panels.size()) createPanel();  // panels hidden by a smaller grid
        for (qsizetype i = 0; i < panels.size(); ++i) {
            target << int(i);
            QString err;
            if (!modelFromJson(panels[i].toObject()["model"].toObject(), *m_axes[size_t(i)].model, &err))
                return fail(err);
        }
        for (qsizetype i = 0; i < panels.size(); ++i) {
            PlotRenderer* r = m_axes[size_t(i)].renderer;
            ViewRange v;
            if (viewFromJson(panels[i].toObject()["view"], &v)) r->setViewRange(v);
            else r->autoscale();
            r->captureHomeView();
        }
    }
    const int current = root["current"].toInt(0);
    if (current >= 0 && current < axesCount()) setCurrentAxes(current);
    m_stack->clear();  // a freshly opened figure has no history
    m_stack->setClean();
    m_figureFile = fileName;
    return true;
}

}  // namespace gp
