// Root-side helper. Only ever invoked through pkexec (polkit) or by the
// restore-at-boot systemd unit, so its interface is deliberately narrow and
// every argument is validated: no paths come from the caller and no shell is
// ever spawned.

#include <unistd.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QThread>

#include <cstdio>

#include "core/policyconfig.h"
#include "core/sensors.h"

namespace {

using ModeMap = QMap<QString, QString>;

const QString kStateDir = QStringLiteral("/var/lib/vivobook-fan-control");
const QString kStateFile = kStateDir + QStringLiteral("/modes");
const QString kService = QStringLiteral("vivobook-fan-restore.service");
const QString kPolicyService = QStringLiteral("vivobook-fan-policy.service");

void out(const QString &text)
{
    QTextStream stream(stdout);
    stream << text << Qt::endl;
}

void err(const QString &text)
{
    std::fputs(qPrintable(text + QStringLiteral("\n")), stderr);
    std::fflush(stderr);
}

ModeMap loadModes()
{
    ModeMap modes;
    QFile file(kStateFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return modes;

    while (!file.atEnd()) {
        const QString line = QString::fromLocal8Bit(file.readLine()).trimmed();
        const int sep = line.indexOf(QLatin1Char('='));
        if (sep <= 0)
            continue;
        modes.insert(line.left(sep), line.mid(sep + 1));
    }
    return modes;
}

bool saveModes(const ModeMap &modes)
{
    if (!QDir().mkpath(kStateDir)) {
        err(QStringLiteral("cannot create %1").arg(kStateDir));
        return false;
    }

    QFile file(kStateFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        err(QStringLiteral("cannot write %1").arg(kStateFile));
        return false;
    }

    for (auto it = modes.cbegin(); it != modes.cend(); ++it)
        file.write(qPrintable(it.key() + QLatin1Char('=') + it.value() + QLatin1Char('\n')));
    return true;
}

bool writeMode(const fanctl::Machine &machine, const fanctl::FanSpec &fan, int value)
{
    QString error;
    if (fanctl::writeFanMode(machine, fan, value, &error))
        return true;
    err(error);
    return false;
}

int apply(const QStringList &args)
{
    if (args.size() != 2) {
        err(QStringLiteral("usage: apply <cpu|gpu|all> <auto|full>"));
        return 1;
    }

    QString *error = nullptr;
    const fanctl::Machine machine = fanctl::discoverMachine(error);
    if (!machine.isValid()) {
        err(error ? *error : QStringLiteral("ASUS fan controller not detected."));
        return 2;
    }

    const QString fanArg = args.at(0).toLower();
    fanctl::ModeValue mode = fanctl::ModeAuto;
    if (!fanctl::modeFromWord(args.at(1), &mode)) {
        err(QStringLiteral("mode must be auto or full"));
        return 1;
    }

    QVector<fanctl::FanSpec> targets;
    if (fanArg == QStringLiteral("all")) {
        targets = machine.fans;
    } else if (auto spec = machine.find(fanArg)) {
        targets.append(*spec);
    } else {
        QStringList names;
        for (const auto &spec : machine.fans)
            names.append(spec.name);
        err(QStringLiteral("fan must be one of: %1, all").arg(names.join(QStringLiteral(", "))));
        return 1;
    }

    ModeMap modes = loadModes();
    for (const fanctl::FanSpec &fan : targets) {
        if (!writeMode(machine, fan, mode))
            return 3;
        modes.insert(fan.name, fanctl::modeToWord(mode));
        out(QStringLiteral("%1 fan set to %2").arg(fan.name, fanctl::modeToWord(mode)));
    }

    if (!saveModes(modes))
        return 3;
    return 0;
}

int restore(const QStringList &args)
{
    Q_UNUSED(args);

    ModeMap modes = loadModes();
    if (modes.isEmpty()) {
        out(QStringLiteral("no remembered fan modes; leaving firmware defaults alone"));
        return 0;
    }

    // The asus hwmon device appears only once the platform module has loaded.
    fanctl::Machine machine;
    for (int attempt = 0; attempt < 20 && !machine.isValid(); ++attempt) {
        machine = fanctl::discoverMachine(nullptr);
        if (machine.isValid())
            break;
        QThread::msleep(500);
    }
    if (!machine.isValid()) {
        err(QStringLiteral("ASUS fan controller not present at boot; nothing restored"));
        return 2;
    }

    for (auto it = modes.cbegin(); it != modes.cend(); ++it) {
        auto spec = machine.find(it.key());
        if (!spec)
            continue;
        fanctl::ModeValue mode = fanctl::ModeAuto;
        if (!fanctl::modeFromWord(it.value(), &mode))
            continue;
        if (writeMode(machine, *spec, mode))
            out(QStringLiteral("restored %1 fan to %2").arg(it.key(), it.value()));
    }
    return 0;
}

int runSystemctl(const QStringList &arguments)
{
    QProcess process;
    process.start(QStringLiteral("systemctl"), arguments);
    if (!process.waitForStarted(5000)) {
        err(QStringLiteral("cannot run systemctl"));
        return 3;
    }
    if (!process.waitForFinished(30000)) {
        err(QStringLiteral("systemctl timed out"));
        return 3;
    }
    if (process.exitCode() != 0) {
        err(QString::fromLocal8Bit(process.readAll()).trimmed());
        return 3;
    }
    return 0;
}

int boot(const QStringList &args)
{
    if (args.size() != 1 || (args.at(0) != QStringLiteral("on") && args.at(0) != QStringLiteral("off"))) {
        err(QStringLiteral("usage: boot <on|off>"));
        return 1;
    }

    const bool enable = args.at(0) == QStringLiteral("on");
    if (enable) {
        QDir().mkpath(kStateDir);
        if (loadModes().isEmpty()) {
            // Seed the state with whatever the firmware is doing right now so the
            // first boot after enabling does not force an arbitrary mode.
            const fanctl::Machine machine = fanctl::discoverMachine(nullptr);
            ModeMap modes;
            for (const fanctl::FanStatus &status : fanctl::readFanStatuses(machine))
                modes.insert(status.name, status.mode == fanctl::ModeFull ? QStringLiteral("full")
                                                                         : QStringLiteral("auto"));
            if (!saveModes(modes))
                return 3;
        }
    }

    const int rc = runSystemctl({enable ? QStringLiteral("enable") : QStringLiteral("disable"),
                                 QStringLiteral("--now"), kService});
    if (rc != 0)
        return rc;

    out(enable ? QStringLiteral("fan modes will be restored at boot")
               : QStringLiteral("fan modes will no longer be restored at boot"));
    return 0;
}

int policy(const QStringList &args)
{
    if (args.isEmpty() || args.at(0) != QStringLiteral("set") || args.size() < 2) {
        err(QStringLiteral("usage: policy set <key=value> [...]"
                           " (cpu_full_above, cpu_auto_below, gpu_full_above, gpu_auto_below, interval)"));
        return 1;
    }

    fanctl::PolicyConfig config = fanctl::loadPolicyConfig();
    for (const QString &argument : args.mid(1)) {
        QString error;
        if (!fanctl::applyPolicyArgument(argument, &config, &error)) {
            err(error);
            return 1;
        }
    }

    if (!config.isValid()) {
        err(config.errorText());
        return 1;
    }

    QString error;
    if (!fanctl::savePolicyConfig(config, &error)) {
        err(error);
        return 3;
    }

    out(QStringLiteral("policy thresholds saved to %1").arg(fanctl::policyConfigPath()));
    out(fanctl::policyConfigText(config).trimmed());
    return 0;
}

int service(const QStringList &args)
{
    if (args.size() != 1 || (args.at(0) != QStringLiteral("on") && args.at(0) != QStringLiteral("off"))) {
        err(QStringLiteral("usage: service <on|off>"));
        return 1;
    }

    const bool enable = args.at(0) == QStringLiteral("on");
    const int rc = runSystemctl({enable ? QStringLiteral("enable") : QStringLiteral("disable"),
                                 QStringLiteral("--now"), kPolicyService});
    if (rc != 0)
        return rc;

    out(enable ? QStringLiteral("temperature policy is now running in the background")
               : QStringLiteral("temperature policy stopped"));
    return 0;
}

void usage()
{
    out(QStringLiteral("vivobook fan control helper\n"
                       "  apply <cpu|gpu|all> <auto|full>\n"
                       "  restore\n"
                       "  boot <on|off>\n"
                       "  policy set <key=value> [...]\n"
                       "  service <on|off>"));
}

}  // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("fancontrol-helper"));

    if (geteuid() != 0) {
        err(QStringLiteral("this helper must run as root (via pkexec)."));
        return 4;
    }

    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty() || args.at(0) == QStringLiteral("-h") || args.at(0) == QStringLiteral("--help")) {
        usage();
        return args.isEmpty() ? 1 : 0;
    }

    const QString verb = args.at(0);
    const QStringList rest = args.mid(1);

    if (verb == QStringLiteral("apply"))
        return apply(rest);
    if (verb == QStringLiteral("restore"))
        return restore(rest);
    if (verb == QStringLiteral("boot"))
        return boot(rest);
    if (verb == QStringLiteral("policy"))
        return policy(rest);
    if (verb == QStringLiteral("service"))
        return service(rest);

    err(QStringLiteral("unknown verb: %1").arg(verb));
    usage();
    return 1;
}
