#include "ui/theme.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QVector>
#include <optional>

namespace fanctl {
namespace theme {
namespace {

constexpr const char *kSettingsKey = "theme";

QColor rgb(quint32 value)
{
    return QColor((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
}

struct Role {
    QPalette::ColorRole role;
    quint32 active;
    quint32 disabled;
};

QPalette build(const QVector<Role> &roles)
{
    QPalette palette;
    for (const Role &entry : roles) {
        const QColor color = rgb(entry.active);
        palette.setColor(QPalette::Active, entry.role, color);
        palette.setColor(QPalette::Inactive, entry.role, color);
        palette.setColor(QPalette::Disabled, entry.role, rgb(entry.disabled));
    }
    return palette;
}

// Qt's own Fusion defaults, which is what the window looked like before the
// desktop theme was able to override it.
QPalette lightPalette()
{
    return build({
        {QPalette::Window, 0xEFEFEF, 0xEFEFEF},
        {QPalette::Base, 0xFFFFFF, 0xEFEFEF},
        {QPalette::WindowText, 0x000000, 0xEBEBEB},
        {QPalette::Text, 0x000000, 0xEBEBEB},
        {QPalette::Button, 0xEFEFEF, 0xEFEFEF},
        {QPalette::ButtonText, 0x000000, 0xEBEBEB},
        {QPalette::BrightText, 0xFFFFFF, 0xFFFFFF},
        {QPalette::Light, 0xFFFFFF, 0xFFFFFF},
        {QPalette::Midlight, 0xCACACA, 0xCACACA},
        {QPalette::Dark, 0x9F9F9F, 0xEBEBEB},
        {QPalette::Mid, 0xB8B8B8, 0xB8B8B8},
        {QPalette::Highlight, 0x308CC6, 0x919191},
        {QPalette::HighlightedText, 0xFFFFFF, 0xFFFFFF},
        {QPalette::ToolTipBase, 0xFFFFDC, 0xFFFFDC},
        {QPalette::ToolTipText, 0x000000, 0x000000},
        {QPalette::PlaceholderText, 0x767676, 0x767676},
        {QPalette::Link, 0x0000FF, 0x0000FF},
        {QPalette::LinkVisited, 0xFF00FF, 0xFF00FF},
    });
}

// COSMIC's dark scheme: warm near-black panels, grey text and a gold highlight.
QPalette darkPalette()
{
    return build({
        {QPalette::Window, 0x372A2B, 0x372A2B},
        {QPalette::Base, 0x4D3F40, 0x473A3B},
        {QPalette::WindowText, 0xC0C0C0, 0xA7A6A6},
        {QPalette::Text, 0xDADADA, 0xA5A3A3},
        {QPalette::Button, 0x615758, 0x4F4647},
        {QPalette::ButtonText, 0xDADADA, 0xA5A3A3},
        {QPalette::BrightText, 0xC0C0C0, 0xC0C0C0},
        {QPalette::Light, 0x695F60, 0x695F60},
        {QPalette::Midlight, 0x655B5C, 0x655B5C},
        {QPalette::Dark, 0x645A5B, 0x645A5B},
        {QPalette::Mid, 0x635959, 0x635959},
        {QPalette::Highlight, 0xDEBA6F, 0x594C4D},
        {QPalette::HighlightedText, 0x5F5253, 0x64533B},
        {QPalette::ToolTipBase, 0x4D3F40, 0x4D3F40},
        {QPalette::ToolTipText, 0xDADADA, 0xDADADA},
        {QPalette::PlaceholderText, 0x948D8D, 0x948D8D},
        {QPalette::Link, 0xDEBA6F, 0xDEBA6F},
        {QPalette::LinkVisited, 0xC4A468, 0xC4A468},
    });
}

std::optional<QPalette> g_systemPalette;
Mode g_current = Mode::System;

}  // namespace

QString nameOf(Mode mode)
{
    switch (mode) {
    case Mode::Light:
        return QStringLiteral("light");
    case Mode::Dark:
        return QStringLiteral("dark");
    case Mode::System:
        break;
    }
    return QStringLiteral("system");
}

Mode modeFromName(const QString &name, Mode fallback)
{
    if (name.compare(QLatin1String("light"), Qt::CaseInsensitive) == 0)
        return Mode::Light;
    if (name.compare(QLatin1String("dark"), Qt::CaseInsensitive) == 0)
        return Mode::Dark;
    if (name.compare(QLatin1String("system"), Qt::CaseInsensitive) == 0)
        return Mode::System;
    return fallback;
}

void captureSystem(QApplication *app)
{
    if (!g_systemPalette)
        g_systemPalette = app->palette();
}

void apply(QApplication *app, Mode mode)
{
    if (mode == Mode::System) {
        // Nothing was captured, so the palette in use already is the desktop's.
        if (g_systemPalette)
            app->setPalette(*g_systemPalette);
    } else {
        app->setPalette(mode == Mode::Dark ? darkPalette() : lightPalette());
    }
    g_current = mode;
}

Mode stored()
{
    return modeFromName(QSettings().value(QLatin1String(kSettingsKey)).toString());
}

void store(Mode mode)
{
    QSettings settings;
    settings.setValue(QLatin1String(kSettingsKey), nameOf(mode));
}

Mode current()
{
    return g_current;
}

}  // namespace theme
}  // namespace fanctl
