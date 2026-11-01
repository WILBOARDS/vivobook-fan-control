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

bool bootRestoreEnabled()
{
    QProcess process;
    process.start(QStringLiteral("systemctl"), {QStringLiteral("is-enabled"), kService});
    if (!process.waitForStarted(3000) || !process.waitForFinished(3000))
        return false;
    return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed() == QStringLiteral("enabled");
}

}  // namespace controller
}  // namespace fanctl
