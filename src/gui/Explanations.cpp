// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "Explanations.h"

#include "PeakTable.h"

#include <QAbstractButton>
#include <QAction>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QLabel>
#include <QLayout>
#include <QMenu>

namespace gp {

namespace {

// Key: "<group>/<label or button text>" for panel controls ("<group>" is the
// title of the enclosing group box, "Peaks" for the peak list, empty for
// loose buttons), "action/<menu or toolbar text>" for actions.
const QHash<QString, QString>& table()
{
    static const QHash<QString, QString> t = {
        // ---- Axes (Properties) ------------------------------------------------------------
        {"Axes/Title", "Title shown above the plot. Supports $math$, **bold** and *italic*."},
        {"Axes/X label", "Label of the X axis. Supports $math$ (e.g. $E_T$ [GeV]), **bold** and *italic*."},
        {"Axes/Y label", "Label of the Y axis. Supports $math$, **bold** and *italic*."},
        {"Axes/X range", "Visible X limits (from, to). They follow zoom and pan; type a value to set the view.\n"
                         "Previous / next view: Alt+Left / Alt+Right."},
        {"Axes/Y range", "Visible Y limits (from, to). They follow zoom and pan; type a value to set the view."},
        {"Axes/Legend", "Show the legend. Only labelled data appear in it."},
        {"Axes/Grid", "Show grid lines at the major ticks."},
        {"Axes/Log X", "Logarithmic X axis (only positive values can be shown)."},
        {"Axes/Log Y", "Logarithmic Y axis (only positive values can be shown)."},
        {"Axes/Reverse Y", "High values at the bottom."},
        {"/Remove selected", "Delete the object selected in the list above (Del). Undo with Ctrl+Z."},

        // ---- Dataset ----------------------------------------------------------------------
        {"Dataset/Name", "Name shown in the legend and in the object list."},
        {"Dataset/Colour", "Line and symbol colour. Click to choose."},
        {"Dataset/Line width", "Line width in pixels."},
        {"Dataset/Line style", "Solid, dashed, dotted or dash-dot. 'None' draws the symbols only."},
        {"Dataset/Symbol", "Symbol drawn at every data point."},
        {"Dataset/Symbol size", "Symbol size in pixels."},
        {"Dataset/Drawing", "How points are joined: straight lines, or steps (histogram style)."},
        {"Dataset/Area", "Fill the area under the curve (down to y = 0) with the chosen colour."},
        {"Dataset/Visible", "Show or hide this dataset. Hidden data is kept."},
        {"Dataset/In legend", "List this dataset in the legend."},

        // ---- Marker -----------------------------------------------------------------------
        {"Marker/Orientation", "Vertical (marks an x position) or horizontal (marks a y position)."},
        {"Marker/Label", "Text shown next to the marker. Supports $math$."},
        {"Marker/Position", "Coordinate of the marker. You can also drag it in the plot."},
        {"Marker/Colour", "Marker line colour. Click to choose."},
        {"Marker/Line style", "Solid, dashed, dotted or dash-dot."},
        {"Marker/Line width", "Line width in pixels."},
        {"Marker/Show coordinate", "Show the marker's position next to its label."},

        // ---- Annotation -------------------------------------------------------------------
        {"Annotation/Text", "Annotation text. Supports $math$, **bold** and *italic*; \\n starts a new line."},
        {"Annotation/X", "Horizontal position in data coordinates (or drag the text in the plot)."},
        {"Annotation/Y", "Vertical position in data coordinates (or drag the text in the plot)."},
        {"Annotation/Colour", "Text colour. Until you choose one it follows the style."},
        {"Annotation/Font size", "Text size in points. Until you change it, it follows the style."},

        // ---- Selected region --------------------------------------------------------------
        {"Selected region/X min", "Start of the selected X region. Drag its edges in the plot or type here."},
        {"Selected region/X max", "End of the selected X region. Drag its edges in the plot or type here."},
        {"Selected region/Width", "Width of the selected region."},
        {"Selected region/Clear selection", "Remove the region selection (undo with Ctrl+Z)."},
        {"Selected region/Copy statistics",
         "Copy the statistics table (n, mean, sd, min, max, rms, integral per dataset) to the clipboard,\n"
         "tab-separated, ready to paste into a spreadsheet."},

        // ---- Peak -------------------------------------------------------------------------
        {"Peak/Label", "Assignment shown on the plot, e.g. H3/C5. Supports $math$."},
        {"Peak/x", "Peak position along X. Drag the peak in the plot or type here."},
        {"Peak/y", "Peak position along Y. Drag the peak in the plot or type here."},
        {"Peak/Intensity", "Spectrum value at the peak position."},

        // ---- 2D data ----------------------------------------------------------------------
        {"2D data (contours)/Name", "Name shown in the legend and in the object list."},
        {"2D data (contours)/Level factor",
         "Ratio between neighbouring contour levels (levels are base, base×factor, base×factor², ...)."},
        {"2D data (contours)/Levels", "Number of contour levels above the base level."},
        {"2D data (contours)/Positive colour", "Colour of the positive contours."},
        {"2D data (contours)/Negative colour", "Colour of the negative contours."},
        {"2D data (contours)/Line width", "Contour line width in pixels."},
        {"2D data (contours)/Negative levels", "Also draw contours for negative values (phase-sensitive spectra)."},
        {"2D data (contours)/Colour-map image", "Show the intensities as a colour image under the contours."},
        {"2D data (contours)/Visible", "Show or hide this 2D dataset."},

        // ---- Peaks list -------------------------------------------------------------------
        {"Peaks/Threshold", "Minimum peak height. Empty: the current lowest contour level (what you see is what is picked)."},
        {"Peaks/Min. distance", "Minimum separation between two peaks, in data points."},
        {"Peaks/Skip diagonal ±", "Skip peaks closer than this to the diagonal (|x − y|), to pick only cross peaks."},
        {"2D data (contours)/Base level", "Lowest positive contour level. Alt+wheel over the plot scrolls it; * and / halve or double it."},
        {"Axes/Reverse X", "High values on the left (NMR ppm convention)."},
        {"Axes/Diagonal", "Dashed y = x line (homonuclear 2D spectra)."},
        {"Rows/New row", "Add an empty row; then drag signals onto it."},
        {"Peaks/Negative peaks too", "Also pick negative peaks (minima)."},
        {"Peaks/Visible area only", "Pick only inside the visible area; otherwise the whole spectrum."},
        {"Peaks/Delete selected", "Delete the peaks selected in the table (Del)."},
        {"Peaks/Delete all", "Delete every peak (one undo step)."},
        {"Peaks/Export CSV…", "Save the peak list (position, intensity, label, comment) as a CSV file."},
        {"Peaks/Import CSV…", "Add peaks from a CSV file: x, y and optionally intensity, label, comment."},

        // ---- Timeline dock ----------------------------------------------------------------
        {"Rows/Remove empty row", "Remove the selected row if it holds no signals (move them to another row first)."},
        {"View/Auto-fit Y to the visible window", "Each row fits its Y axis to the data inside the time window."},
        {"View/Clock time (instead of seconds)",
         "Label the time axis with clock times (needs absolute timestamps in the data)."},
        {"View/Cursor", "Time under the cursor line (move the mouse over the rows)."},
        {"Time markers/Add at cursor (M)", "Add a time marker at the cursor line, through all rows (key M, or Ctrl+click a row)."},
        {"Time markers/Delete", "Delete the selected time markers."},

        // ---- Menu and toolbar actions -----------------------------------------------------
        {"action/Open data…", "Open a text table: x followed by one or more y columns (CSV, TSV, spaces)."},
        {"action/Open 2D matrix…", "Open a text grid of z values (one row per y) as a contour plot."},
        {"action/Open time series (CSV)…", "Open sensor logs (time + signal columns) as stacked timeline rows."},
        {"action/Open Bruker 2D spectrum…", "Open a processed TopSpin 2D spectrum (experiment or pdata folder)."},
        {"action/Open figure…", "Open a saved figure (.gpfig) with all its data, peaks, markers and settings."},
        {"action/Save figure", "Save everything in this window (data, peaks with labels and comments, markers,\n"
                               "annotations, region, panels, styles, timeline) as a .gpfig file to continue later."},
        {"action/Save figure as…", "Save the figure under a new name."},
        {"action/Export data (CSV)…", "Save the visible 1D data of the current panel as a CSV table\n"
                                      "(one column per dataset when they share x, otherwise one row per point)."},
        {"action/Export region data (CSV)…", "Like Export data, but only the points inside the selected X region."},
        {"action/Export…", "Save the figure as PNG, SVG or PDF (100 figure pixels per inch)."},
        {"action/Copy figure to clipboard", "Copy the figure as an image, ready to paste into a document."},
        {"action/Close", "Close this window."},
        {"action/Undo", "Undo the last change (mouse, panels or Python)."},
        {"action/Redo", "Redo the change that was undone."},
        {"action/Delete selected", "Delete the selected object."},
        {"action/Clear region selection", "Remove the X region selection."},
        {"action/Autoscale X", "Fit the X axis to the data; Y stays."},
        {"action/Autoscale Y", "Fit the Y axis to the data in the visible X range."},
        {"action/Set current view as home", "Make the current view the one Reset view (Home) returns to."},
        {"action/Previous view", "Go back to the previous zoom / pan state."},
        {"action/Next view", "Go forward again after Previous view."},
        {"action/Lower contour levels ÷2", "Halve all contour levels to show weaker features."},
        {"action/Raise contour levels ×2", "Double all contour levels to hide weaker features and noise."},
        {"action/Show peak labels", "Show the peak labels (assignments) on the plot."},
        {"action/Edit style…", "Edit every style setting of this figure (fonts, colours, ticks, ...)."},
        {"action/Load style file…", "Apply a .gpstyle file to this figure."},
        {"action/Save style as…", "Save this figure's style as a .gpstyle file to reuse later."},
        {"action/Use as my default style", "New figures start with this figure's style, also in later sessions."},
        {"action/Default", "Built-in style: clean screen style with a light grid."},
        {"action/Science", "Built-in style: publication / HEP look, closed frame with inward ticks."},
        {"action/Nmr", "Built-in style: TopSpin-like spectrum display."},
        {"action/Presentation", "Built-in style: large fonts and thick lines for slides."},
        {"action/Add time marker at cursor", "Add a time marker at the cursor line, through all rows."},
        {"action/Previous time marker", "Centre the time window on the previous time marker."},
        {"action/Next time marker", "Centre the time window on the next time marker."},
        {"action/Show whole recording", "Show the complete time range in the window."},
        {"action/Auto-fit Y to window", "Each row fits its Y axis to the data inside the time window."},
        {"action/Clock time", "Label the time axis with clock times instead of seconds."},
        {"action/About Graphplot", "Version, author and licence."},
        {"action/Properties", "Show or hide the Properties panel (objects and their settings)."},
        {"action/Peaks", "Show or hide the peak list."},
        {"action/Timeline", "Show or hide the Timeline panel (rows, navigator, time markers)."},
        {"action/Main", "Show or hide the toolbar."},
    };
    return t;
}

QString clean(QString text)
{
    text.remove('&');
    return text.trimmed();
}

// The group a control belongs to: its group box title, or "Peaks" inside the
// peak list, or empty.
QString contextOf(const QWidget* w)
{
    for (const QWidget* p = w; p; p = p->parentWidget()) {
        if (auto* g = qobject_cast<const QGroupBox*>(p)) return g->title();
        if (qobject_cast<const PeakTable*>(p)) return "Peaks";
    }
    return {};
}

void setIfMissing(QWidget* w, const QString& tip)
{
    if (w && w->toolTip().isEmpty()) w->setToolTip(tip);
}

void explainLayoutItem(QLayoutItem* item, const QString& tip)
{
    if (!item) return;
    if (QWidget* w = item->widget()) {
        setIfMissing(w, tip);
        for (QWidget* c : w->findChildren<QWidget*>()) setIfMissing(c, tip);
    } else if (QLayout* l = item->layout()) {
        for (int i = 0; i < l->count(); ++i) explainLayoutItem(l->itemAt(i), tip);
    }
}

}  // namespace

void applyExplanations(QWidget* root)
{
    const auto& t = table();
    // Form rows: "<group>/<row label>" explains the label and every control
    // of the row that has no explanation of its own.
    for (QFormLayout* form : root->findChildren<QFormLayout*>()) {
        const QString group = contextOf(form->parentWidget());
        for (int r = 0; r < form->rowCount(); ++r) {
            auto* label = qobject_cast<QLabel*>(form->itemAt(r, QFormLayout::LabelRole)
                                                    ? form->itemAt(r, QFormLayout::LabelRole)->widget()
                                                    : nullptr);
            if (!label) continue;
            const QString tip = t.value(group + "/" + clean(label->text()));
            if (tip.isEmpty()) continue;
            setIfMissing(label, tip);
            explainLayoutItem(form->itemAt(r, QFormLayout::FieldRole), tip);
        }
    }
    // Buttons and check boxes, by their text.
    for (QAbstractButton* b : root->findChildren<QAbstractButton*>()) {
        if (!b->toolTip().isEmpty() || b->text().isEmpty()) continue;
        const QString tip = t.value(contextOf(b) + "/" + clean(b->text()));
        if (!tip.isEmpty()) b->setToolTip(tip);
    }
    // Actions (menus and toolbar): the explanation plus the shortcut.
    for (QAction* a : root->findChildren<QAction*>()) {
        if (a->isSeparator() || a->text().isEmpty()) continue;
        const QString text = clean(a->text());
        if (a->toolTip() != text && !a->toolTip().isEmpty()) continue;  // set in code
        QString tip = t.value("action/" + text);
        if (tip.isEmpty()) continue;
        if (!a->shortcut().isEmpty())
            tip += QString(" (%1)").arg(a->shortcut().toString(QKeySequence::NativeText));
        a->setToolTip(tip);
        a->setStatusTip(tip);
    }
    // Menus show their entries' explanations on hover too.
    for (QMenu* m : root->findChildren<QMenu*>()) m->setToolTipsVisible(true);
}

}  // namespace gp
