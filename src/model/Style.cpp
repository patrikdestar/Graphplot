// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "Style.h"

#include "PlotTypes.h"

#include <QDir>
#include <cmath>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QTextStream>
#include <functional>

namespace gp {

namespace {

// ---- value conversion helpers --------------------------------------------------

QString colorText(const QColor& c)
{
    return c.alpha() == 255 ? c.name() : c.name(QColor::HexArgb);
}

bool toColor(const QString& v, QColor* out, QString* err)
{
    const QColor c = parseColor(v);
    if (!c.isValid()) {
        if (err) *err = QString("'%1' is not a colour").arg(v);
        return false;
    }
    *out = c;
    return true;
}

// Finite numbers in [lo, hi] only: style values end up in int conversions
// and allocations (e.g. contour levels), so "inf" or 1e20 must be rejected.
bool toDouble(const QString& v, double* out, QString* err, double lo = -1e6, double hi = 1e6)
{
    bool ok = false;
    const double d = v.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(d) || d < lo || d > hi) {
        if (err) *err = QString("'%1' is not a number in [%2, %3]").arg(v).arg(lo).arg(hi);
        return false;
    }
    *out = d;
    return true;
}

bool toBool(const QString& v, bool* out, QString* err)
{
    const QString s = v.trimmed().toLower();
    if (s == "true" || s == "yes" || s == "on" || s == "1") return *out = true, true;
    if (s == "false" || s == "no" || s == "off" || s == "0") return *out = false, true;
    if (err) *err = QString("'%1' is not true/false").arg(v);
    return false;
}

bool toChoice(const QString& v, const QStringList& choices, QString* out, QString* err)
{
    const QString s = v.trimmed().toLower();
    if (!choices.contains(s)) {
        if (err) *err = QString("'%1' must be one of: %2").arg(v, choices.join(", "));
        return false;
    }
    *out = s;
    return true;
}

QString b(bool v) { return v ? "true" : "false"; }
QString n(double v) { return QString::number(v, 'g', 6); }

struct Entry {
    QString key;
    QString description;
    std::function<QString(const Style&)> get;
    std::function<bool(Style&, const QString&, QString*)> set;
};

// Builders for common field types (member pointers keep this table compact).
template <typename T>
Entry dbl(const char* key, const char* desc, T Style::*f, double lo = -1e6)
{
    return {key, desc, [f](const Style& s) { return n(s.*f); },
            [f, lo](Style& s, const QString& v, QString* e) {
                double d;
                if (!toDouble(v, &d, e, lo)) return false;
                s.*f = T(d);
                return true;
            }};
}
Entry boo(const char* key, const char* desc, bool Style::*f)
{
    return {key, desc, [f](const Style& s) { return b(s.*f); },
            [f](Style& s, const QString& v, QString* e) { return toBool(v, &(s.*f), e); }};
}
Entry col(const char* key, const char* desc, QColor Style::*f)
{
    return {key, desc, [f](const Style& s) { return colorText(s.*f); },
            [f](Style& s, const QString& v, QString* e) { return toColor(v, &(s.*f), e); }};
}
Entry str(const char* key, const char* desc, QString Style::*f)
{
    return {key, desc, [f](const Style& s) { return s.*f; },
            [f](Style& s, const QString& v, QString*) {
                s.*f = v;
                return true;
            }};
}
Entry choice(const char* key, const char* desc, QString Style::*f, QStringList choices)
{
    return {key, desc + QString(" (%1)").arg(choices.join(" | ")),
            [f](const Style& s) { return s.*f; },
            [f, choices](Style& s, const QString& v, QString* e) { return toChoice(v, choices, &(s.*f), e); }};
}

// Tick entries for xtick.* / ytick.*
void tickEntries(QList<Entry>& t, const QString& axis, TickStyle Style::*ts, const char* mirrorName)
{
    auto k = [&](const char* s) { return axis + "." + s; };
    auto mk = [&](const QString& key, const QString& desc, auto get, auto set) {
        t.push_back({key, desc, get, set});
    };
    mk(k("direction"), "tick direction (in | out | inout)",
       [ts](const Style& s) { return (s.*ts).direction; },
       [ts](Style& s, const QString& v, QString* e) {
           return toChoice(v, {"in", "out", "inout"}, &(s.*ts).direction, e);
       });
    auto num = [&](const char* name, const char* desc, double TickStyle::*f) {
        mk(k(name), desc, [ts, f](const Style& s) { return n((s.*ts).*f); },
           [ts, f](Style& s, const QString& v, QString* e) { return toDouble(v, &((s.*ts).*f), e, 0); });
    };
    num("major.size", "major tick length (px)", &TickStyle::majorSize);
    num("minor.size", "minor tick length (px)", &TickStyle::minorSize);
    num("width", "tick line width (px)", &TickStyle::width);
    mk(k("minor.visible"), "show minor ticks",
       [ts](const Style& s) { return b((s.*ts).minorVisible); },
       [ts](Style& s, const QString& v, QString* e) { return toBool(v, &(s.*ts).minorVisible, e); });
    mk(axis + "." + mirrorName, QString("ticks also on the %1 side").arg(mirrorName),
       [ts](const Style& s) { return b((s.*ts).mirror); },
       [ts](Style& s, const QString& v, QString* e) { return toBool(v, &(s.*ts).mirror, e); });
    num("labelsize", "tick label font size (pt)", &TickStyle::labelSize);
    mk(k("labelcolor"), "tick label colour",
       [ts](const Style& s) { return colorText((s.*ts).labelColor); },
       [ts](Style& s, const QString& v, QString* e) { return toColor(v, &(s.*ts).labelColor, e); });
    mk(k("count"), "approximate number of major ticks",
       [ts](const Style& s) { return QString::number((s.*ts).count); },
       [ts](Style& s, const QString& v, QString* e) {
           double d;
           if (!toDouble(v, &d, e, 1, 50)) return false;
           (s.*ts).count = int(d);
           return true;
       });
}

const QList<Entry>& table()
{
    static const QList<Entry> t = [] {
        QList<Entry> t;
        t << str("font.family", "font preference list; generic names: sans-serif, serif, monospace",
                 &Style::fontFamily)
          << dbl("font.size", "base font size (pt)", &Style::fontSize, 1)
          << col("figure.facecolor", "background outside the axes", &Style::figureFace)
          << dbl("figure.padding", "empty border around the figure (px)", &Style::figurePadding, 0)
          << col("axes.facecolor", "background inside the axes", &Style::axesFace)
          << col("axes.edgecolor", "frame and tick colour", &Style::axesEdge)
          << dbl("axes.linewidth", "frame line width (px)", &Style::axesLineWidth, 0)
          << dbl("axes.labelsize", "axis label font size (pt)", &Style::labelSize, 1)
          << col("axes.labelcolor", "axis label colour", &Style::labelColor)
          << dbl("axes.labelpad", "space between tick labels and axis label (px)", &Style::labelPad, 0)
          << dbl("axes.titlesize", "title font size (pt)", &Style::titleSize, 1)
          << boo("axes.titlebold", "bold title", &Style::titleBold)
          << col("axes.titlecolor", "title colour", &Style::titleColor)
          << boo("axes.unicode_minus", "use a real minus sign in tick labels", &Style::unicodeMinus)
          << choice("axes.formatter", "tick label number format", &Style::formatter,
                    {"auto", "plain", "sci"})
          << boo("text.math", "interpret $math$, **bold** and *italic* in texts", &Style::mathText);
        tickEntries(t, "xtick", &Style::xtick, "top");
        tickEntries(t, "ytick", &Style::ytick, "right");
        t << boo("axes.grid", "show the major grid", &Style::grid)
          << col("grid.color", "grid colour", &Style::gridColor)
          << dbl("grid.linewidth", "grid line width (px)", &Style::gridWidth, 0)
          << choice("grid.linestyle", "grid line style", &Style::gridStyle, {"-", "--", ":", "-."})
          << boo("grid.minor", "show the minor grid", &Style::minorGrid)
          << col("grid.minor.color", "minor grid colour", &Style::minorGridColor);
        t << Entry{"axes.prop_cycle", "comma-separated colour cycle for new datasets",
                   [](const Style& s) {
                       QStringList l;
                       for (const QColor& c : s.palette) l << colorText(c);
                       return l.join(", ");
                   },
                   [](Style& s, const QString& v, QString* e) {
                       QList<QColor> cols;
                       for (const QString& part : v.split(',', Qt::SkipEmptyParts)) {
                           QColor c;
                           if (!toColor(part.trimmed(), &c, e)) return false;
                           cols << c;
                       }
                       if (cols.isEmpty()) {
                           if (e) *e = "the colour cycle needs at least one colour";
                           return false;
                       }
                       s.palette = cols;
                       return true;
                   }};
        t << dbl("lines.linewidth", "default line width (px)", &Style::lineWidth, 0)
          << dbl("lines.markersize", "default symbol size (px)", &Style::markerSize, 0)
          << dbl("errorbar.capsize", "error bar cap width (px)", &Style::errorbarCapSize, 0)
          << boo("legend.frameon", "draw a frame around the legend", &Style::legendFrame)
          << choice("legend.loc", "legend position", &Style::legendLoc,
                    {"upper right", "upper left", "lower right", "lower left", "upper center",
                     "lower center", "center left", "center right", "center"})
          << dbl("legend.fontsize", "legend font size (pt)", &Style::legendFontSize, 1)
          << col("legend.facecolor", "legend background", &Style::legendFace)
          << col("legend.edgecolor", "legend frame colour", &Style::legendEdge)
          << str("label.top.left", "text above the frame, left (e.g. **CMS** *Preliminary*)",
                 &Style::labelTopLeft)
          << str("label.top.right", "text above the frame, right (e.g. 138 fb$^{-1}$ (13 TeV))",
                 &Style::labelTopRight)
          << str("label.inside", "text inside the frame", &Style::labelInside)
          << choice("label.inside.loc", "position of label.inside", &Style::labelInsideLoc,
                    {"upper left", "upper right", "lower left", "lower right"})
          << dbl("label.inside.size", "font size of label.inside (pt)", &Style::labelInsideSize, 1)
          << dbl("contour.linewidth", "contour line width (px)", &Style::contourLineWidth, 0)
          << col("contour.negativecolor", "colour of negative contours", &Style::contourNegativeColor)
          << Entry{"contour.levels", "number of contour levels for new 2D data",
                   [](const Style& s) { return QString::number(s.contourLevels); },
                   [](Style& s, const QString& v, QString* e) {
                       double d;
                       if (!toDouble(v, &d, e, 1, 200)) return false;
                       s.contourLevels = int(d);
                       return true;
                   }}
          << dbl("contour.factor", "ratio between successive contour levels", &Style::contourFactor, 1.0001)
          << col("peak.color", "peak cross colour", &Style::peakColor)
          << dbl("peak.size", "peak cross half-size (px)", &Style::peakSize, 0)
          << dbl("peak.linewidth", "peak cross line width (px)", &Style::peakLineWidth, 0)
          << dbl("peak.labelsize", "peak label font size (pt)", &Style::peakLabelSize, 1)
          << col("peak.labelcolor", "peak label colour", &Style::peakLabelColor)
          << dbl("annotation.size", "default annotation font size (pt)", &Style::annotationSize, 1)
          << col("annotation.color", "default annotation colour", &Style::annotationColor);
        return t;
    }();
    return t;
}

const Entry* entry(const QString& key)
{
    for (const Entry& e : table())
        if (e.key == key) return &e;
    return nullptr;
}

QList<QColor> defaultPalette()
{
    QList<QColor> l;
    for (int i = 0; i < 8; ++i) l << gp::paletteColor(i);
    return l;
}

}  // namespace

QColor Style::paletteColor(int i) const
{
    const QList<QColor>& p = palette.isEmpty() ? defaultPalette() : palette;
    return p[((i % p.size()) + p.size()) % p.size()];
}

const QList<Style::KeyInfo>& Style::keys()
{
    static const QList<KeyInfo> k = [] {
        QList<KeyInfo> l;
        for (const Entry& e : table()) l << KeyInfo{e.key, e.description};
        return l;
    }();
    return k;
}

QString Style::value(const QString& key) const
{
    const Entry* e = entry(key);
    if (!e) return QString();  // null: unknown key
    const QString v = e->get(*this);
    return v.isNull() ? QStringLiteral("") : v;  // known keys are never null
}

bool Style::set(const QString& key, const QString& value, QString* error)
{
    const Entry* e = entry(key.trimmed());
    if (!e) {
        if (error) *error = QString("unknown style key '%1'").arg(key);
        return false;
    }
    return e->set(*this, value.trimmed(), error);
}

bool Style::parse(const QString& text, QStringList* errors, const QString& baseDir)
{
    bool ok = true;
    int lineNo = 0;
    // Keys already set in this file: an 'inherit' further down replaces the
    // whole style, so they are applied again on top of it.
    QList<QPair<QString, QString>> setHere;
    for (QString line : text.split('\n')) {
        ++lineNo;
        // Strip comments: '#' starts a comment unless it begins a colour (#rrggbb)
        // directly after the separator or a comma.
        // Inside a quoted value, "" is a literal quote.
        bool inQuotes = false;
        for (qsizetype i = 0; i < line.size(); ++i) {
            if (line[i] == '"') {
                if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') ++i;
                else inQuotes = !inQuotes;
                continue;
            }
            if (line[i] != '#' || inQuotes) continue;
            qsizetype j = i - 1;
            while (j >= 0 && line[j].isSpace()) --j;
            const bool colourStart = j >= 0 && (line[j] == ':' || line[j] == ',');
            if (!colourStart) {
                line.truncate(i);
                break;
            }
        }
        line = line.trimmed();
        if (line.isEmpty()) continue;
        const qsizetype colon = line.indexOf(':');
        if (colon < 0) {
            if (errors) *errors << QString("line %1: expected 'key: value'").arg(lineNo);
            ok = false;
            continue;
        }
        const QString key = line.left(colon).trimmed();
        QString value = line.mid(colon + 1).trimmed();
        if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"'))
            value = value.mid(1, value.size() - 2).replace("\"\"", "\"");
        if (key == "inherit") {
            Style base;
            QString path = value;
            if (!builtinStyleNames().contains(value) && QFileInfo(value).isRelative() && !baseDir.isEmpty())
                path = QDir(baseDir).filePath(value);
            if (!loadStyle(path, &base, errors)) {
                if (errors) *errors << QString("line %1: cannot inherit '%2'").arg(lineNo).arg(value);
                ok = false;
                continue;
            }
            *this = base;
            for (const auto& [k, v] : setHere) set(k, v, nullptr);
            continue;
        }
        QString err;
        if (!set(key, value, &err)) {
            if (errors) *errors << QString("line %1: %2").arg(lineNo).arg(err);
            ok = false;
        } else {
            setHere << qMakePair(key, value);
        }
    }
    return ok;
}

QString Style::toText(const QString& header) const
{
    QString out;
    QTextStream s(&out);
    s << "# Graphplot style file\n";
    if (!header.isEmpty()) s << "# " << header << "\n";
    s << "# Format: 'key: value'. Lines starting with '#' are comments.\n"
         "# 'inherit: <built-in name or file>' may be used as the first line.\n\n";
    QString lastGroup;
    for (const Entry& e : table()) {
        const QString group = e.key.section('.', 0, 0);
        if (group != lastGroup && !lastGroup.isEmpty()) s << "\n";
        lastGroup = group;
        QString v = e.get(*this);
        // Quote values that would otherwise be ambiguous: empty, with a quote,
        // or with a '#' that is not part of colours ("#1 #2" would be cut at
        // the second '#'). Quotes inside are doubled.
        static const QRegularExpression colours(R"(^#[0-9a-fA-F]{3,8}(\s*,\s*#[0-9a-fA-F]{3,8})*$)");
        if (v.isEmpty() || v.contains('"') || (v.contains('#') && !colours.match(v).hasMatch()) ||
            v != v.trimmed())
            v = '"' + QString(v).replace("\"", "\"\"") + '"';
        const QString kv = e.key + ": " + v;
        s << kv.leftJustified(44) << " # " << e.description << "\n";
    }
    return out;
}

// ---- built-ins / files / global default ---------------------------------------------

QStringList builtinStyleNames() { return {"default", "science", "nmr", "presentation"}; }

bool builtinStyle(const QString& name, Style* out)
{
    Style s;
    s.palette = defaultPalette();
    if (name == "default") {
        *out = s;
        return true;
    }
    if (name == "science") {
        // Publication style in the spirit of ROOT / mplhep (HEP) plots:
        // closed frame with inward ticks on all four sides, minor ticks,
        // no grid, Helvetica-like font, larger labels, frameless legend.
        s.fontFamily = "Helvetica, Arial, Nimbus Sans, FreeSans, Liberation Sans, sans-serif";
        s.fontSize = 12;
        s.figurePadding = 14;
        s.axesEdge = Qt::black;
        s.axesLineWidth = 1.2;
        s.labelSize = 14;
        s.labelColor = Qt::black;
        s.labelPad = 6;
        s.titleSize = 14;
        s.titleColor = Qt::black;
        s.formatter = "sci";
        for (TickStyle* t : {&s.xtick, &s.ytick}) {
            t->direction = "in";
            t->majorSize = 10;
            t->minorSize = 5;
            t->width = 1.2;
            t->minorVisible = true;
            t->mirror = true;
            t->labelSize = 12;
            t->labelColor = Qt::black;
        }
        s.grid = false;
        s.legendFrame = false;
        s.legendFontSize = 12;
        s.lineWidth = 2.0;
        s.markerSize = 7;
        s.errorbarCapSize = 0;  // HEP convention: no caps
        s.palette = {QColor("#5790fc"), QColor("#f89c20"), QColor("#e42536"), QColor("#964a8b"),
                     QColor("#9c9ca1"), QColor("#7a21dd")};  // CMS-recommended 6-colour cycle
        s.peakColor = Qt::black;
        s.peakLabelSize = 11;
        s.annotationSize = 12;
    } else if (name == "nmr") {
        // TopSpin-like spectrum display: no grid, outward ticks, thin contours.
        s.grid = false;
        s.axesEdge = QColor(0x33, 0x33, 0x33);
        for (TickStyle* t : {&s.xtick, &s.ytick}) {
            t->direction = "out";
            t->minorVisible = true;
            t->mirror = false;
            t->count = 8;
        }
        s.contourLineWidth = 0.8;
        s.contourLevels = 16;
        s.contourFactor = 1.3;
        s.legendFrame = false;
    } else if (name == "presentation") {
        s.fontSize = 14;
        s.labelSize = 18;
        s.titleSize = 20;
        s.axesLineWidth = 2;
        for (TickStyle* t : {&s.xtick, &s.ytick}) {
            t->labelSize = 15;
            t->width = 2;
            t->majorSize = 8;
        }
        s.lineWidth = 3;
        s.markerSize = 9;
        s.legendFontSize = 15;
        s.peakLabelSize = 14;
        s.annotationSize = 16;
    } else {
        return false;
    }
    *out = s;
    return true;
}

bool loadStyle(const QString& nameOrPath, Style* out, QStringList* errors)
{
    if (builtinStyle(nameOrPath, out)) return true;
    // 'inherit:' chains are followed recursively; a file that (indirectly)
    // inherits itself would recurse forever and crash at start-up.
    static thread_local int depth = 0;
    if (depth >= 8) {
        if (errors) *errors << QString("'%1': inherit chain too deep (cycle?)").arg(nameOrPath);
        return false;
    }
    struct DepthGuard {
        DepthGuard() { ++depth; }
        ~DepthGuard() { --depth; }
    } guard;
    QFile f(nameOrPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errors) *errors << QString("cannot open style '%1'").arg(nameOrPath);
        return false;
    }
    Style s;
    builtinStyle("default", &s);
    QStringList errs;
    s.parse(QString::fromUtf8(f.readAll()), &errs, QFileInfo(nameOrPath).absolutePath());
    if (errors)
        for (const QString& e : errs) *errors << QFileInfo(nameOrPath).fileName() + ": " + e;
    *out = s;
    return true;  // partially valid files still load; problems are reported
}

bool saveStyle(const QString& fileName, const Style& s, QString* error)
{
    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) *error = QString("Cannot write '%1': %2").arg(fileName, f.errorString());
        return false;
    }
    f.write(s.toText().toUtf8());
    return true;
}

namespace {
Style& globalDefault()
{
    static Style s = [] {
        Style d;
        builtinStyle("default", &d);
        return d;
    }();
    return s;
}
}  // namespace

const Style& defaultStyle() { return globalDefault(); }
void setDefaultStyle(const Style& s) { globalDefault() = s; }

QString userDefaultStylePath()
{
    const QString env = qEnvironmentVariable("GRAPHPLOT_STYLE");
    if (!env.isEmpty()) return env;
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath("Graphplot/default.gpstyle");
}

void reloadDefaultStyle(QStringList* errors)
{
    Style s;
    builtinStyle("default", &s);
    const QString path = userDefaultStylePath();
    if (QFileInfo::exists(path) || builtinStyleNames().contains(path)) loadStyle(path, &s, errors);
    setDefaultStyle(s);
}

}  // namespace gp
