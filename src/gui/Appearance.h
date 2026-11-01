// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
//
// Application-wide appearance settings, shared by every window and by the
// Python module:
//   * the display theme (System, Light, and several dark variants), see
//     render/DisplayTheme.h for what a theme changes;
//   * whether plots and exports follow the theme;
//   * hover explanations (tooltips) on or off.
//
// Settings are remembered between sessions (QSettings, organisation and
// application "Graphplot"), so the app and Python scripts share them.
#pragma once

#include <QObject>
#include <QString>

class QMenu;
class QWidget;

namespace gp {

class Appearance : public QObject {
    Q_OBJECT
public:
    static Appearance* instance();

    // Reads the saved settings and applies them (call once the QApplication exists).
    void load();

    QString theme() const;
    // Applies a theme ("system", "light", "dark", "dim", "midnight",
    // "solarized-dark", "high-contrast"). save: remember it for next time.
    bool setTheme(const QString& id, bool save = true);
    void setThemePlots(bool on, bool save = true);
    void setThemeExports(bool on, bool save = true);

    bool tooltipsEnabled() const { return m_tooltips; }
    void setTooltipsEnabled(bool on, bool save = true);

    // "Appearance" menu (themes, plot/export options, tooltips) kept in sync
    // with the settings across all windows.
    QMenu* createMenu(QWidget* parent);

signals:
    void changed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;  // suppresses tooltips when off

private:
    Appearance() = default;
    void applyWidgetPalette();
    void save(const QString& key, const QVariant& value);

    bool m_tooltips = true;
    bool m_filterInstalled = false;
    QString m_originalStyle;  // platform widget style, restored by "System"
};

}  // namespace gp
