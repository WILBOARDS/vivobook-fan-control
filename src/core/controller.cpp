#include "core/controller.h"

#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>

namespace fanctl {

Result Result::success(const QString &message)
{
    Result r;
    r.ok = true;
    r.message = message;
    return r;
}

Result Result::failure(const QString &message)
{
    Result r;
    r.message = message;
    return r;
}

namespace controller {
namespace {

const QString kHelperPath = QStringLiteral("/usr/libexec/vivobook-fan-control/fancontrol-helper");
const QString kService = QStringLiteral("vivobook-fan-restore.service");
const QString kPolicyService = QStringLiteral("vivobook-fan-policy.service");
const QString kTrayService = QStringLiteral("vivobook-fan-control.service");

bool unitEnabled(const QStringList &arguments)
{
    QProcess process;
    process.start(QStringLiteral("systemctl"), arguments);
    if (!process.waitForStarted(3000) || !process.waitForFinished(3000))
        return false;
    // is-enabled exits non-zero for disabled units while still printing the state.
    return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed() == QStringLiteral("enabled");
}

Result runSystemctl(const QStringList &arguments)
{
    QProcess process;
    process.start(QStringLiteral("systemctl"), arguments);
    if (!process.waitForStarted(5000))
        return Result::failure(QStringLiteral("Could not run systemctl."));
    if (!process.waitForFinished(30000)) {
        process.kill();
        return Result::failure(QStringLiteral("systemctl did not finish in time."));
    }
    const QString output = QString::fromLocal8Bit(process.readAll()).trimmed();
    if (process.exitCode() != 0)
        return Result::failure(output.isEmpty() ? QStringLiteral("systemctl failed.") : output);
    return Result::success(output);
}

QString describeFailure(int exitCode, const QString &output)
{
    const QString text = output.trimmed();

    if (exitCode == 126 || text.contains(QStringLiteral("dismissed"), Qt::CaseInsensitive))
        return QStringLiteral("Authentication was cancelled.");

    if (text.contains(QStringLiteral("not authorized"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("no policy"), Qt::CaseInsensitive))
        return QStringLiteral("Not authorized: the polkit policy is not installed on this system, "
                              "so fan changes are refused. Install the package to enable them.");

    if (exitCode == 127)
        return QStringLiteral("Root helper not found at %1. Reinstall the package.").arg(kHelperPath);

    if (!text.isEmpty())
        return text;

    return QStringLiteral("The request failed (exit code %1).").arg(exitCode);
}

void run(const QStringList &arguments, const ResultHandler &handler)
{
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pkexec.isEmpty()) {
        handler(Result::failure(QStringLiteral("pkexec is not installed; the polkit package is required.")));
        return;
    }

    auto *process = new QProcess(QCoreApplication::instance());
    process->setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(process, &QProcess::finished, process,
                     [process, handler](int exitCode, QProcess::ExitStatus status) {
                         const QString output = QString::fromLocal8Bit(process->readAll()).trimmed();
                         const bool ok = (status == QProcess::NormalExit && exitCode == 0);
                         handler(ok ? Result::success(output) : Result::failure(describeFailure(exitCode, output)));
                         process->deleteLater();
                     });
    process->start(pkexec, QStringList{kHelperPath} + arguments);
}

Result runSync(const QStringList &arguments)
{
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pkexec.isEmpty())
        return Result::failure(QStringLiteral("pkexec is not installed; the polkit package is required."));

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(pkexec, QStringList{kHelperPath} + arguments);
    if (!process.waitForStarted(10000))
        return Result::failure(QStringLiteral("Could not start %1.").arg(pkexec));
    if (!process.waitForFinished(300000)) {
        process.kill();
        return Result::failure(QStringLiteral("The helper did not finish in time."));
    }

    const QString output = QString::fromLocal8Bit(process.readAll()).trimmed();
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0)
        return Result::success(output);
    return Result::failure(describeFailure(process.exitCode(), output));
}

}  // namespace

void applyMode(const QString &fan, ModeValue mode, ResultHandler handler)
{
    run({QStringLiteral("apply"), fan, modeToWord(mode)}, std::move(handler));
}

void setBootRestore(bool enabled, ResultHandler handler)
{
    run({QStringLiteral("boot"), enabled ? QStringLiteral("on") : QStringLiteral("off")},
        std::move(handler));
}

Result applyModeSync(const QString &fan, ModeValue mode)
{
    return runSync({QStringLiteral("apply"), fan, modeToWord(mode)});
}

Result setBootRestoreSync(bool enabled)
{
    return runSync({QStringLiteral("boot"), enabled ? QStringLiteral("on") : QStringLiteral("off")});
}

void setPolicyConfig(const QStringList &assignments, ResultHandler handler)
{
    QStringList arguments{QStringLiteral("policy"), QStringLiteral("set")};
    arguments.append(assignments);
    run(arguments, std::move(handler));
}

void setPolicyService(bool enabled, ResultHandler handler)
{
    run({QStringLiteral("service"), enabled ? QStringLiteral("on") : QStringLiteral("off")},
        std::move(handler));
}

Result setPolicyConfigSync(const QStringList &assignments)
{
    QStringList arguments{QStringLiteral("policy"), QStringLiteral("set")};
    arguments.append(assignments);
    return runSync(arguments);
}

Result setPolicyServiceSync(bool enabled)
{
    return runSync({QStringLiteral("service"), enabled ? QStringLiteral("on") : QStringLiteral("off")});
}

bool policyServiceEnabled()
{
    return unitEnabled({QStringLiteral("is-enabled"), kPolicyService});
}

bool trayAutostartEnabled()
{
    return unitEnabled({QStringLiteral("--user"), QStringLiteral("is-enabled"), kTrayService});
}

Result setTrayAutostart(bool enabled)
{
    if (!enabled)
        return runSystemctl({QStringLiteral("--user"), QStringLiteral("disable"),
                             QStringLiteral("--now"), kTrayService});

    // A user unit inherits the user manager's environment, not the desktop's, so
    // the display variables have to be handed over before the tray can appear.
    QStringList imported;
    for (const QString &name : {QStringLiteral("WAYLAND_DISPLAY"), QStringLiteral("DISPLAY"),
                                QStringLiteral("XDG_SESSION_TYPE"), QStringLiteral("XDG_CURRENT_DESKTOP")}) {
        if (qEnvironmentVariableIsSet(qPrintable(name)))
            imported.append(name);
    }
    if (!imported.isEmpty()) {
        QStringList arguments{QStringLiteral("--user"), QStringLiteral("import-environment")};
        arguments.append(imported);
        runSystemctl(arguments);
    }

    return runSystemctl({QStringLiteral("--user"), QStringLiteral("enable"), QStringLiteral("--now"),
                         kTrayService});
}

bool bootRestoreEnabled()
{
    return unitEnabled({QStringLiteral("is-enabled"), kService});
}

}  // namespace controller
}  // namespace fanctl
