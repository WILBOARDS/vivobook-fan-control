#include "core/policyconfig.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

namespace fanctl {
namespace {

constexpr int kMinThreshold = 30;
constexpr int kMaxThreshold = 110;
constexpr int kMinInterval = 2;
constexpr int kMaxInterval = 60;

int readInt(const QString &value, int fallback)
{
    bool ok = false;
    const int parsed = value.toInt(&ok);
    return ok ? parsed : fallback;
}

}  // namespace

QString PolicyConfig::errorText() const
{
    const auto checkRange = [](int value, int lo, int hi) { return value >= lo && value <= hi; };

    if (!checkRange(cpuFullAbove, kMinThreshold, kMaxThreshold)
        || !checkRange(gpuFullAbove, kMinThreshold, kMaxThreshold)
        || !checkRange(cpuAutoBelow, kMinThreshold, kMaxThreshold)
        || !checkRange(gpuAutoBelow, kMinThreshold, kMaxThreshold))
        return QStringLiteral("thresholds must be between %1 and %2 C").arg(kMinThreshold).arg(kMaxThreshold);

    if (cpuAutoBelow >= cpuFullAbove)
        return QStringLiteral("CPU release temperature must stay below the trigger temperature");
    if (gpuAutoBelow >= gpuFullAbove)
        return QStringLiteral("GPU release temperature must stay below the trigger temperature");
    if (!checkRange(intervalSeconds, kMinInterval, kMaxInterval))
        return QStringLiteral("check interval must be between %1 and %2 seconds").arg(kMinInterval).arg(kMaxInterval);

    return QString();
}

QString policyConfigPath()
{
    return QStringLiteral("/etc/vivobook-fan-control/policy.conf");
}

PolicyConfig loadPolicyConfig()
{
    PolicyConfig config;
    QFile file(policyConfigPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return config;

    while (!file.atEnd()) {
        const QString line = QString::fromLocal8Bit(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int sep = line.indexOf(QLatin1Char('='));
        if (sep <= 0)
            continue;
        const QString key = line.left(sep).trimmed();
        const QString value = line.mid(sep + 1).trimmed();

        if (key == QLatin1String("cpu_full_above"))
            config.cpuFullAbove = readInt(value, config.cpuFullAbove);
        else if (key == QLatin1String("cpu_auto_below"))
            config.cpuAutoBelow = readInt(value, config.cpuAutoBelow);
        else if (key == QLatin1String("gpu_full_above"))
            config.gpuFullAbove = readInt(value, config.gpuFullAbove);
        else if (key == QLatin1String("gpu_auto_below"))
            config.gpuAutoBelow = readInt(value, config.gpuAutoBelow);
        else if (key == QLatin1String("interval"))
            config.intervalSeconds = readInt(value, config.intervalSeconds);
    }
    return config;
}

QString policyConfigText(const PolicyConfig &config)
{
    QString text;
    QTextStream stream(&text);
    stream << "# Vivobook fan policy thresholds, applied by fancontrol-policyd.\n"
           << "# Written by the root helper; safe to read but not to edit by hand.\n"
           << "cpu_full_above=" << config.cpuFullAbove << '\n'
           << "cpu_auto_below=" << config.cpuAutoBelow << '\n'
           << "gpu_full_above=" << config.gpuFullAbove << '\n'
           << "gpu_auto_below=" << config.gpuAutoBelow << '\n'
           << "interval=" << config.intervalSeconds << '\n';
    return text;
}

bool savePolicyConfig(const PolicyConfig &config, QString *error)
{
    if (!config.isValid()) {
        if (error)
            *error = config.errorText();
        return false;
    }

    if (!QDir().mkpath(QStringLiteral("/etc/vivobook-fan-control"))) {
        if (error)
            *error = QStringLiteral("cannot create /etc/vivobook-fan-control");
        return false;
    }

    QFile file(policyConfigPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("cannot write %1").arg(policyConfigPath());
        return false;
    }

    const QByteArray body = policyConfigText(config).toUtf8();
    if (file.write(body) != body.size()) {
        if (error)
            *error = QStringLiteral("short write on %1").arg(policyConfigPath());
        return false;
    }
    file.close();
    // The daemon reads this as root, but the GUI and CLI show it unprivileged.
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup
                        | QFileDevice::ReadOther);
    return true;
}

bool applyPolicyArgument(const QString &argument, PolicyConfig *config, QString *error)
{
    const int sep = argument.indexOf(QLatin1Char('='));
    if (sep <= 0) {
        if (error)
            *error = QStringLiteral("expected key=value, got %1").arg(argument);
        return false;
    }

    const QString key = argument.left(sep);
    const QString value = argument.mid(sep + 1);
    bool ok = false;
    const int number = value.toInt(&ok);
    if (!ok) {
        if (error)
            *error = QStringLiteral("%1 needs a whole number").arg(key);
        return false;
    }

    if (key == QLatin1String("cpu_full_above"))
        config->cpuFullAbove = number;
    else if (key == QLatin1String("cpu_auto_below"))
        config->cpuAutoBelow = number;
    else if (key == QLatin1String("gpu_full_above"))
        config->gpuFullAbove = number;
    else if (key == QLatin1String("gpu_auto_below"))
        config->gpuAutoBelow = number;
    else if (key == QLatin1String("interval"))
        config->intervalSeconds = number;
    else {
        if (error)
            *error = QStringLiteral("unknown policy key %1").arg(key);
        return false;
    }
    return true;
}

}  // namespace fanctl
