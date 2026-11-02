#pragma once

#include <QString>
#include <QStringList>

#include <functional>

#include "core/sensors.h"

namespace fanctl {

struct Result {
    bool ok = false;
    QString message;

    static Result success(const QString &message = QString());
    static Result failure(const QString &message);
};

using ResultHandler = std::function<void(Result)>;

namespace controller {

// Privileged verbs, executed through the polkit-guarded root helper.
void applyMode(const QString &fan, ModeValue mode, ResultHandler handler);
void setBootRestore(bool enabled, ResultHandler handler);

// Same verbs, blocking, for command line use.
Result applyModeSync(const QString &fan, ModeValue mode);
Result setBootRestoreSync(bool enabled);

bool bootRestoreEnabled();

// Temperature policy thresholds and the root daemon that applies them.
void setPolicyConfig(const QStringList &assignments, ResultHandler handler);
void setPolicyService(bool enabled, ResultHandler handler);
Result setPolicyConfigSync(const QStringList &assignments);
Result setPolicyServiceSync(bool enabled);
bool policyServiceEnabled();

// Tray autostart is a user unit, so it needs no polkit authorisation.
bool trayAutostartEnabled();
Result setTrayAutostart(bool enabled);

}  // namespace controller
}  // namespace fanctl
