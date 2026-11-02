// Background policy service. Runs as root from vivobook-fan-policy.service and
// holds no authority of its own: it only ever writes pwm*_enable through the
// shared core, and re-reads its thresholds every cycle so changes made through
// the helper apply without a restart.

#include <unistd.h>

#include <QCoreApplication>
#include <QThread>
#include <QTextStream>

#include <cstdio>

#include "core/policyconfig.h"
#include "core/sensors.h"

namespace {

void log(const QString &message)
{
    std::fputs(qPrintable(message + QStringLiteral("\n")), stderr);
    std::fflush(stderr);
}

// Returns the mode the thresholds call for, or -1 to leave the fan alone.
int desiredMode(double temp, int fullAbove, int autoBelow, int current)
{
    if (temp >= fullAbove)
        return current == fanctl::ModeFull ? -1 : fanctl::ModeFull;
    if (temp <= autoBelow)
        return current == fanctl::ModeAuto ? -1 : fanctl::ModeAuto;
    return -1;
}

void applyCycle(const fanctl::Machine &machine, const fanctl::PolicyConfig &config)
{
    const std::optional<double> cpuTemp = fanctl::readCpuTemp();
    const std::optional<double> gpuTemp = fanctl::readGpuTemp();
    const QVector<fanctl::FanStatus> statuses = fanctl::readFanStatuses(machine);

    for (const fanctl::FanStatus &status : statuses) {
        std::optional<double> temp;
        int fullAbove = 0;
        int autoBelow = 0;

        if (status.name == QLatin1String("cpu")) {
            temp = cpuTemp;
            fullAbove = config.cpuFullAbove;
            autoBelow = config.cpuAutoBelow;
        } else if (status.name == QLatin1String("gpu")) {
            temp = gpuTemp;
            fullAbove = config.gpuFullAbove;
            autoBelow = config.gpuAutoBelow;
        } else {
            continue;
        }

        if (!temp)
            continue;

        const int wanted = desiredMode(*temp, fullAbove, autoBelow, status.mode);
        if (wanted < 0)
            continue;

        auto spec = machine.find(status.name);
        if (!spec)
            continue;

        QString error;
        if (fanctl::writeFanMode(machine, *spec, wanted, &error)) {
            log(QStringLiteral("%1 fan %2 C -> %3")
                    .arg(status.name)
                    .arg(*temp, 0, 'f', 1)
                    .arg(fanctl::modeName(wanted)));
        } else {
            log(QStringLiteral("failed to adjust %1 fan: %2").arg(status.name, error));
        }
    }
}

}  // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("fancontrol-policyd"));

    if (geteuid() != 0) {
        log(QStringLiteral("fancontrol-policyd must run as root; start it through systemd."));
        return 4;
    }

    log(QStringLiteral("policy service started"));

    fanctl::Machine machine;
    while (true) {
        if (!machine.isValid())
            machine = fanctl::discoverMachine(nullptr);

        if (!machine.isValid()) {
            // The asus hwmon device appears late in boot; keep waiting rather
            // than exiting, since systemd only starts this unit once.
            QThread::msleep(2000);
            continue;
        }

        const fanctl::PolicyConfig config = fanctl::loadPolicyConfig();
        if (!config.isValid()) {
            log(QStringLiteral("ignoring invalid policy configuration: %1").arg(config.errorText()));
            QThread::msleep(2000);
        } else {
            applyCycle(machine, config);
            QThread::msleep(uint32_t(config.intervalSeconds) * 1000);
        }
    }

    return 0;
}
