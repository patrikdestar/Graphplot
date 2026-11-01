// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "DisplayTheme.h"

#include "model/Style.h"

namespace gp {

namespace {

// One dark theme from a handful of base colours; the rest is derived.
ThemeDef makeDark(const QString& id, const QString& name, const QString& description, QColor window,
                  QColor base, QColor text, QColor highlight, QColor grid)
{
    ThemeDef t;
    t.id = id;
    t.name = name;
    t.description = description;
    t.dark = true;
    ThemeColors& c = t.c;
    c.window = window;
    c.base = base;
    c.alternateBase = base.lighter(115);
    c.text = text;
    c.mutedText = QColor::fromRgbF(text.redF() * 0.65 + window.redF() * 0.35,
                                   text.greenF() * 0.65 + window.greenF() * 0.35,
                                   text.blueF() * 0.65 + window.blueF() * 0.35);
    c.button = window.lighter(125);
    c.highlight = highlight;
    c.highlightedText = highlight.lightnessF() > 0.6 ? QColor(Qt::black) : QColor(Qt::white);
    c.link = highlight.lighter(130);
    c.figureFace = window;
    c.axesFace = base;
    c.foreground = text;
    c.axesEdge = c.mutedText;
    c.grid = grid;
    c.minorGrid = QColor::fromRgbF((grid.redF() + base.redF()) / 2, (grid.greenF() + base.greenF()) / 2,
                                   (grid.blueF() + base.blueF()) / 2);
    c.legendFace = QColor(base.red(), base.green(), base.blue(), 230);
    c.legendEdge = grid;
    return t;
}

QList<ThemeDef> buildThemes()
{
    QList<ThemeDef> out;
    ThemeDef system;
    system.id = "system";
    system.name = "System";
    system.description = "The operating system's own look; plots keep their style colours.";
    system.usesPalette = false;
    out << system;

    ThemeDef light;
    light.id = "light";
    light.name = "Light";
    light.description = "Light widgets on every platform; plots keep their style colours.";
    ThemeColors& c = light.c;
    c.window = QColor(0xf3, 0xf3, 0xf1);
    c.base = Qt::white;
    c.alternateBase = QColor(0xf7, 0xf7, 0xf5);
    c.text = QColor(0x1b, 0x1b, 0x1b);
    c.mutedText = QColor(0x6b, 0x6a, 0x66);
    c.button = QColor(0xfa, 0xfa, 0xf8);
    c.highlight = QColor(0x2a, 0x78, 0xd6);
    c.highlightedText = Qt::white;
    c.link = QColor(0x1d, 0x5f, 0xb0);
    out << light;

    out << makeDark("dark", "Dark", "Neutral dark grey, easy on the eyes for long sessions.",
                    QColor(0x1e, 0x1f, 0x22), QColor(0x26, 0x27, 0x2b), QColor(0xdf, 0xe1, 0xe5),
                    QColor(0x3d, 0x7a, 0xd6), QColor(0x3a, 0x3c, 0x41));
    out << makeDark("dim", "Dim", "Softer, lighter dark grey with less contrast.",
                    QColor(0x2f, 0x31, 0x36), QColor(0x36, 0x39, 0x3f), QColor(0xc7, 0xc9, 0xcc),
                    QColor(0x5b, 0x8d, 0xd9), QColor(0x4a, 0x4d, 0x54));
    out << makeDark("midnight", "Midnight", "Deep blue-black background with cool, blue-tinted text.",
                    QColor(0x0f, 0x16, 0x26), QColor(0x14, 0x1d, 0x31), QColor(0xcd, 0xd6, 0xf4),
                    QColor(0x89, 0xb4, 0xfa), QColor(0x26, 0x32, 0x4d));
    out << makeDark("solarized-dark", "Solarized dark", "The low-contrast Solarized palette (teal background).",
                    QColor(0x00, 0x2b, 0x36), QColor(0x07, 0x36, 0x42), QColor(0x93, 0xa1, 0xa1),
                    QColor(0x26, 0x8b, 0xd2), QColor(0x0e, 0x4b, 0x59));
    out << makeDark("high-contrast", "High contrast (dark)",
                    "Pure black with white text and yellow selection, for projectors and low vision.",
                    QColor(0, 0, 0), QColor(0, 0, 0), QColor(0xff, 0xff, 0xff), QColor(0xff, 0xd6, 0x00),
                    QColor(0x55, 0x55, 0x55));
    return out;
}

}  // namespace

DisplayTheme::DisplayTheme() { m_current = themes().front(); }

DisplayTheme* DisplayTheme::instance()
{
    static DisplayTheme* theme = new DisplayTheme;  // lives as long as the process
    return theme;
}

const QList<ThemeDef>& DisplayTheme::themes()
{
    static const QList<ThemeDef> all = buildThemes();
    return all;
}

const ThemeDef* DisplayTheme::find(const QString& id)
{
    for (const ThemeDef& t : themes())
        if (t.id == id) return &t;
    return nullptr;
}

bool DisplayTheme::setTheme(const QString& id)
{
    const ThemeDef* t = find(id);
    if (!t) return false;
    if (t->id == m_current.id) return true;
    m_current = *t;
    emit changed();
    return true;
}

void DisplayTheme::setThemePlots(bool on)
{
    if (on == m_themePlots) return;
    m_themePlots = on;
    emit changed();
}

void DisplayTheme::setThemeExports(bool on)
{
    if (on == m_themeExports) return;
    m_themeExports = on;
    emit changed();
}

bool DisplayTheme::plotOverlayActive(bool exporting) const
{
    return m_current.dark && m_themePlots && (!exporting || m_themeExports);
}

void DisplayTheme::applyTo(Style& s) const
{
    const ThemeColors& c = m_current.c;
    s.figureFace = c.figureFace;
    s.axesFace = c.axesFace;
    s.axesEdge = c.axesEdge;
    s.labelColor = c.foreground;
    s.titleColor = c.foreground;
    for (TickStyle* t : {&s.xtick, &s.ytick}) t->labelColor = c.foreground;
    s.gridColor = c.grid;
    s.minorGridColor = c.minorGrid;
    s.legendFace = c.legendFace;
    s.legendEdge = c.legendEdge;
    s.peakColor = ink(s.peakColor);
    s.peakLabelColor = c.foreground;
    s.annotationColor = c.foreground;
}

QColor DisplayTheme::ink(const QColor& col) const
{
    if (!col.isValid() || !m_current.dark) return col;
    const QColor hsl = col.toHsl();
    const double l = hsl.lightnessF(), sat = hsl.hslSaturationF();
    // Near-black greys ('k', '#333'): the theme's text colour.
    if (sat < 0.25 && l < 0.3) {
        QColor out = m_current.c.foreground;
        out.setAlpha(col.alpha());
        return out;
    }
    // Dark saturated colours (navy, dark red): same hue, lighter, so series
    // stay distinguishable. Pure red / blue (lightness 0.5) are kept as they are.
    if (l < 0.3) {
        QColor out = QColor::fromHslF(hsl.hslHueF(), sat, 0.6, col.alphaF());
        return out;
    }
    return col;
}

QPalette DisplayTheme::palette() const
{
    const ThemeColors& c = m_current.c;
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.base);
    p.setColor(QPalette::AlternateBase, c.alternateBase);
    p.setColor(QPalette::ToolTipBase, c.base);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::PlaceholderText, c.mutedText);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.button);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, c.link);
    p.setColor(QPalette::Highlight, c.highlight);
    p.setColor(QPalette::HighlightedText, c.highlightedText);
    p.setColor(QPalette::Light, c.button.lighter(130));
    p.setColor(QPalette::Midlight, c.button.lighter(115));
    p.setColor(QPalette::Mid, c.button.darker(130));
    p.setColor(QPalette::Dark, c.button.darker(160));
    p.setColor(QPalette::Shadow, Qt::black);
    for (QPalette::ColorRole r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, r, c.mutedText);
    p.setColor(QPalette::Disabled, QPalette::Highlight, c.button.darker(120));
    return p;
}

}  // namespace gp
