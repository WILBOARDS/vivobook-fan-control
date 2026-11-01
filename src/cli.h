#pragma once

#include <QStringList>

namespace fanctl {

// Returns the process exit code. Also reports whether the caller asked for the
// GUI so main() can decide which application object to build.
bool looksLikeGuiRequest(const QStringList &arguments, const QString &programName);
int runCli(const QStringList &arguments);

}  // namespace fanctl
