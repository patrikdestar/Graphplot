// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
// Style: every visual setting of a figure, loadable from / savable to a
// plain-text style file. Nothing about the look of a figure is hard-coded
// in the renderer; it all comes from here.
//
// File format ("*.gpstyle", similar to matplotlib's matplotlibrc):
//
//     # comment
//     inherit: science            # optional: start from a built-in style or another file
//     font.size: 11
//     xtick.direction: in
//     axes.prop_cycle: #1f77b4, #ff7f0e, #2ca02c
//
// Unknown keys are reported (with line numbers) but do not abort loading.
#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

namespace gp {

struct TickStyle {
    QString direction = "in";  // in | out | inout
    double majorSize = 5;      // pixels
    double minorSize = 3;
    double width = 1.0;
    bool minorVisible = false;
    bool mirror = true;        // ticks on the opposite side (top / right) too
    double labelSize = 9;      // pt
    QColor labelColor = QColor(0x52, 0x51, 0x4e);
    int count = 5;             // approximate number of major ticks
    bool operator==(const TickStyle&) const = default;
};

struct Style {
    // fonts
    QString fontFamily = "sans-serif";  // comma-separated preference list; generic: sans-serif, serif, monospace
    double fontSize = 10;

    // figure / axes frame
    QColor figureFace = Qt::white;
    double figurePadding = 6;           // empty border around everything (px)
    QColor axesFace = Qt::white;
    QColor axesEdge = QColor(0x52, 0x51, 0x4e);
    double axesLineWidth = 1.0;
    double labelSize = 10;
    QColor labelColor = QColor(0x0b, 0x0b, 0x0b);
    double labelPad = 4;
    double titleSize = 11;
    bool titleBold = true;
    QColor titleColor = QColor(0x0b, 0x0b, 0x0b);
    bool unicodeMinus = true;           // "−" instead of "-" in tick labels
    QString formatter = "auto";         // auto | plain | sci (×10ⁿ powers)
    bool mathText = true;               // interpret $...$ and **bold** / *italic* in texts

    TickStyle xtick;
    TickStyle ytick;

    // grid
    bool grid = true;
    QColor gridColor = QColor(0xe4, 0xe3, 0xde);
    double gridWidth = 1.0;
    QString gridStyle = "-";
    bool minorGrid = false;
    QColor minorGridColor = QColor(0xf0, 0xef, 0xec);

    // data defaults
    QList<QColor> palette;              // colour cycle for new datasets
    double lineWidth = 1.5;
    double markerSize = 6;
    double errorbarCapSize = 3;

    // legend
    bool legendFrame = true;
    QString legendLoc = "upper right";  // upper/lower/center + left/right/center, or "best"
    double legendFontSize = 9;
    QColor legendFace = QColor(255, 255, 255, 230);
    QColor legendEdge = QColor(0xe4, 0xe3, 0xde);

    // texts outside / inside the frame (rich text, e.g. "**CMS** *Preliminary*")
    QString labelTopLeft;
    QString labelTopRight;
    QString labelInside;
    QString labelInsideLoc = "upper left";
    double labelInsideSize = 12;

    // contours (2D data)
    double contourLineWidth = 1.0;
    QColor contourNegativeColor = QColor(0xe3, 0x49, 0x48);
    int contourLevels = 12;
    double contourFactor = 1.4;

    // peaks
    QColor peakColor = QColor(0x0b, 0x0b, 0x0b);
    double peakSize = 7;
    double peakLineWidth = 1.2;
    double peakLabelSize = 9;
    QColor peakLabelColor = QColor(0x0b, 0x0b, 0x0b);

    // markers / annotations defaults
    double annotationSize = 10;
    QColor annotationColor = QColor(0x0b, 0x0b, 0x0b);

    bool operator==(const Style&) const = default;

    QColor paletteColor(int i) const;

    // ---- key/value access (used by files, the style editor and Python) ----
    struct KeyInfo {
        QString key;
        QString description;
    };
    static const QList<KeyInfo>& keys();          // all keys, in file order
    QString value(const QString& key) const;       // empty if the key is unknown
    // Sets one key from its textual value; returns false (and *error) on failure.
    bool set(const QString& key, const QString& value, QString* error = nullptr);

    // Parses style text on top of *this. Errors (unknown keys, bad values)
    // are collected in *errors as "line N: message"; loading continues.
    bool parse(const QString& text, QStringList* errors = nullptr, const QString& baseDir = {});
    QString toText(const QString& header = {}) const;  // complete, commented style file
};

// Built-in styles: "default", "science", "nmr", "presentation".
QStringList builtinStyleNames();
bool builtinStyle(const QString& name, Style* out);
// name of a built-in style or a path to a *.gpstyle file
bool loadStyle(const QString& nameOrPath, Style* out, QStringList* errors = nullptr);
bool saveStyle(const QString& fileName, const Style& s, QString* error = nullptr);

// Global default used for new figures: built-in "default", overridden by
// the user's default style file if it exists (see userDefaultStylePath()).
const Style& defaultStyle();
void setDefaultStyle(const Style& s);
// $GRAPHPLOT_STYLE if set, else <config dir>/Graphplot/default.gpstyle
QString userDefaultStylePath();
// Re-reads the user default file (called once at start-up).
void reloadDefaultStyle(QStringList* errors = nullptr);

}  // namespace gp
