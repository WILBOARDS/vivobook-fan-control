#pragma once

#include <QString>

class QApplication;

namespace fanctl {
namespace theme {

enum class Mode {
    System,  // whatever the desktop theme plugin hands the app at start-up
    Light,
    Dark,
};

// The choice is stored in QSettings, so a restart keeps it and the next launch
// applies it before the first window is built.
QString nameOf(Mode mode);
Mode modeFromName(const QString &name, Mode fallback = Mode::System);

// Called once, before any override, so System has something to return to.
void captureSystem(QApplication *app);

void apply(QApplication *app, Mode mode);
Mode stored();
void store(Mode mode);
Mode current();

}  // namespace theme
}  // namespace fanctl
