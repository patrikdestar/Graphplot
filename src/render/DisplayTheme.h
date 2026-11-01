// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
//
// Display themes (light / dark variants) for the whole application.
//
// A theme has two parts:
//  * the widget palette (menus, side panels, dialogs), applied by the GUI;
//  * plot colours (backgrounds, frame, text, grid, legend), applied by every
//    PlotRenderer on screen *on top of* the figure's own style.
//
// The plot part never changes the figure's style itself: undo, style files
// and Python see the real style, and exports use it too (unless
// "themeExports" is on), so a dark screen still gives publication-ready
// light PDFs.
#pragma once

#include <QColor>
#include <QObject>
#include <QPalette>
#include <QString>
#include <QStringList>

namespace gp {

struct Style;

struct ThemeColors {
    QColor window, base, alternateBase, text, mutedText, button, highlight, highlightedText, link;
    // Plot colours.
    QColor figureFace, axesFace, foreground, axesEdge, grid, minorGrid, legendFace, legendEdge;
};

struct ThemeDef {
    QString id;          // "system", "light", "dark", ...
    QString name;        // shown in menus
    QString description;
    bool dark = false;
    bool usesPalette = true;  // false: keep the platform's own look ("system")
    ThemeColors c;
};

class DisplayTheme : public QObject {
    Q_OBJECT
public:
    static DisplayTheme* instance();

    static const QList<ThemeDef>& themes();
    static const ThemeDef* find(const QString& id);

    const ThemeDef& current() const { return m_current; }
    // Switches the theme; unknown ids are ignored (returns false).
    bool setTheme(const QString& id);

    // Plot colours follow the theme (on by default). Off: plots keep their
    // own style even in a dark theme.
    bool themePlots() const { return m_themePlots; }
    void setThemePlots(bool on);
    // Exports (PNG/SVG/PDF/clipboard) use the theme too (off by default:
    // exports use the figure's style, i.e. usually light).
    bool themeExports() const { return m_themeExports; }
    void setThemeExports(bool on);

    // True when plots on screen (or in exports, with exporting = true) are drawn
    // with theme colours.
    bool plotOverlayActive(bool exporting) const;
    // Applies the theme's plot colours to a style (screen display only).
    void applyTo(Style& s) const;
    // Very dark data colours stay visible on a dark background: near-black
    // greys (e.g. 'k' lines) become the theme's text colour, dark saturated
    // colours (navy) get lighter with the same hue; others are unchanged.
    QColor ink(const QColor& c) const;

    QPalette palette() const;  // widget palette of the current theme

signals:
    void changed();  // theme or options changed: re-apply colours

private:
    DisplayTheme();
    ThemeDef m_current;
    bool m_themePlots = true;
    bool m_themeExports = false;
};

}  // namespace gp
