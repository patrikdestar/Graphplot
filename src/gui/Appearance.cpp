// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
#include "Appearance.h"

#include "render/DisplayTheme.h"

#include <QActionGroup>
#include <QApplication>
#include <QEvent>
#include <QMenu>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QToolTip>

namespace gp {

namespace {

// One settings file for the app and the Python module (their application
// names differ, so it is named explicitly).
constexpr const char* kOrg = "Graphplot";
constexpr const char* kApp = "Graphplot";

}  // namespace

Appearance* Appearance::instance()
{
    static Appearance* a = new Appearance;  // lives as long as the process
    return a;
}

void Appearance::load()
{
    if (!m_filterInstalled && qApp) {
        qApp->installEventFilter(this);
        m_filterInstalled = true;
    }
    QSettings s(kOrg, kApp);
    setThemePlots(s.value("appearance/themePlots", true).toBool(), false);
    setThemeExports(s.value("appearance/themeExports", false).toBool(), false);
    setTooltipsEnabled(s.value("appearance/tooltips", true).toBool(), false);
    setTheme(s.value("appearance/theme", "system").toString(), false);
}

void Appearance::save(const QString& key, const QVariant& value)
{
    QSettings s(kOrg, kApp);
    s.setValue(key, value);
}

QString Appearance::theme() const { return DisplayTheme::instance()->current().id; }

bool Appearance::setTheme(const QString& id, bool save)
{
    if (!DisplayTheme::find(id)) return false;
    DisplayTheme::instance()->setTheme(id);
    applyWidgetPalette();
    if (save) this->save("appearance/theme", id);
    emit changed();
    return true;
}

void Appearance::applyWidgetPalette()
{
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) return;
    if (m_originalStyle.isEmpty() && app->style()) m_originalStyle = app->style()->name();
    const ThemeDef& t = DisplayTheme::instance()->current();
    if (!t.usesPalette) {
        // "System": the platform's own style and palette.
        if (!m_originalStyle.isEmpty() && app->style()->name() != m_originalStyle)
            QApplication::setStyle(QStyleFactory::create(m_originalStyle));
        // A default QPalette = the platform theme's palette (follows OS
        // light/dark mode and colour schemes); same for tooltips.
        QApplication::setPalette(QPalette());
        QToolTip::setPalette(QPalette());
        return;
    }
    // Fusion draws every widget from the palette, so the theme looks the same
    // on every platform (native styles may ignore dark palettes).
    if (app->style()->name().compare("fusion", Qt::CaseInsensitive) != 0)
        QApplication::setStyle(QStyleFactory::create("Fusion"));
    const QPalette p = DisplayTheme::instance()->palette();
    QApplication::setPalette(p);
    QToolTip::setPalette(p);
}

void Appearance::setThemePlots(bool on, bool save)
{
    DisplayTheme::instance()->setThemePlots(on);
    if (save) this->save("appearance/themePlots", on);
    emit changed();
}

void Appearance::setThemeExports(bool on, bool save)
{
    DisplayTheme::instance()->setThemeExports(on);
    if (save) this->save("appearance/themeExports", on);
    emit changed();
}

void Appearance::setTooltipsEnabled(bool on, bool save)
{
    if (!m_filterInstalled && qApp) {
        qApp->installEventFilter(this);
        m_filterInstalled = true;
    }
    m_tooltips = on;
    if (!on) QToolTip::hideText();
    if (save) this->save("appearance/tooltips", on);
    emit changed();
}

bool Appearance::eventFilter(QObject* watched, QEvent* event)
{
    if (!m_tooltips && event->type() == QEvent::ToolTip) return true;  // swallowed: no hover text
    return QObject::eventFilter(watched, event);
}

QMenu* Appearance::createMenu(QWidget* parent)
{
    auto* menu = new QMenu("&Appearance", parent);
    menu->setToolTipsVisible(true);

    auto* group = new QActionGroup(menu);
    QList<QAction*> themeActions;
    for (const ThemeDef& t : DisplayTheme::themes()) {
        QAction* a = menu->addAction(t.name);
        a->setCheckable(true);
        a->setData(t.id);
        a->setToolTip(t.description);
        a->setStatusTip(t.description);
        group->addAction(a);
        themeActions << a;
        connect(a, &QAction::triggered, this, [this, id = t.id] { setTheme(id); });
    }
    menu->addSeparator();
    QAction* plots = menu->addAction("Dark themes also colour the plots");
    plots->setCheckable(true);
    plots->setToolTip("On: plots get the theme's dark background and light text on screen.\n"
                      "Off: plots keep their style colours (as they will be exported).");
    connect(plots, &QAction::toggled, this, [this](bool on) { setThemePlots(on); });
    QAction* exports = menu->addAction("Exports use the theme colours too");
    exports->setCheckable(true);
    exports->setToolTip("Off (recommended for papers): PNG/SVG/PDF exports and the clipboard use the\n"
                        "figure's own style, usually light. On: exports look like the screen.");
    connect(exports, &QAction::toggled, this, [this](bool on) { setThemeExports(on); });
    menu->addSeparator();
    QAction* tips = menu->addAction("Show explanations on hover");
    tips->setCheckable(true);
    tips->setToolTip("Show a short explanation when the mouse rests on a button, field or menu entry.");
    connect(tips, &QAction::toggled, this, [this](bool on) { setTooltipsEnabled(on); });

    // Keep the check marks in sync with the settings (also when changed
    // from another window or from Python).
    auto sync = [=, this] {
        const QString id = theme();
        for (QAction* a : themeActions) {
            const QSignalBlocker b(a);
            a->setChecked(a->data().toString() == id);
        }
        const QSignalBlocker b1(plots), b2(exports), b3(tips);
        plots->setChecked(DisplayTheme::instance()->themePlots());
        exports->setChecked(DisplayTheme::instance()->themeExports());
        exports->setEnabled(DisplayTheme::instance()->themePlots());
        tips->setChecked(m_tooltips);
    };
    sync();
    connect(this, &Appearance::changed, menu, sync);
    return menu;
}

}  // namespace gp
