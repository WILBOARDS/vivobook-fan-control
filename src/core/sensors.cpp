#include "core/sensors.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace fanctl {
namespace {

QString readFirstLine(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    const QByteArray line = file.readLine().trimmed();
    return QString::fromLatin1(line);
}

QString hwmonName(const QString &dir)
{
    return readFirstLine(dir + QStringLiteral("/name"));
}

// Fans are numbered by pwmN_enable; fanN_label carries the firmware name.
QVector<FanSpec> fansIn(const QString &dir)
{
    QVector<FanSpec> fans;
    for (int n = 1; n <= 8; ++n) {
        const QString enable = QStringLiteral("/pwm%1_enable").arg(n);
        if (!QFileInfo::exists(dir + enable))
            continue;

        FanSpec spec;
        spec.index = n;
        const QString label = readFirstLine(dir + QStringLiteral("/fan%1_label").arg(n));
        if (!label.isEmpty())
            spec.label = label;
        if (label.contains(QStringLiteral("cpu"), Qt::CaseInsensitive))
            spec.name = QStringLiteral("cpu");
        else if (label.contains(QStringLiteral("gpu"), Qt::CaseInsensitive))
            spec.name = QStringLiteral("gpu");
        else
            spec.name = QStringLiteral("fan%1").arg(n);
        fans.append(spec);
    }
    return fans;
}

std::optional<double> readSensor(const QString &chipName, const QStringList &preferredLabels)
{
    const QDir base(QStringLiteral("/sys/class/hwmon"));
    const QStringList entries = base.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Unsorted);

    for (const QString &entry : entries) {
        const QString dir = base.filePath(entry);
        if (hwmonName(dir) != chipName)
            continue;

        QVector<double> fallbacks;
        for (int n = 1; n <= 4; ++n) {
            const QString inputPath = dir + QStringLiteral("/temp%1_input").arg(n);
            bool ok = false;
            const double millideg = readFirstLine(inputPath).toDouble(&ok);
            if (!ok || millideg <= -273000.0)
                continue;

            const QString label = readFirstLine(dir + QStringLiteral("/temp%1_label").arg(n));
            if (preferredLabels.contains(label, Qt::CaseInsensitive))
                return millideg / 1000.0;
            fallbacks.append(millideg / 1000.0);
        }
        if (!fallbacks.isEmpty())
            return fallbacks.first();
    }
    return std::nullopt;
}

}  // namespace

bool Machine::has(const QString &fanName) const
{
    return find(fanName).has_value();
}

std::optional<FanSpec> Machine::find(const QString &fanName) const
{
    for (const FanSpec &spec : fans) {
        if (spec.name == fanName)
            return spec;
    }
    return std::nullopt;
}

Machine discoverMachine(QString *error)
{
    const QDir base(QStringLiteral("/sys/class/hwmon"));
    const QStringList entries = base.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Unsorted);

    for (const QString &entry : entries) {
        const QString dir = base.filePath(entry);
        if (hwmonName(dir) != QLatin1String("asus"))
            continue;

        Machine machine;
        machine.path = dir;
        machine.fans = fansIn(dir);
        if (machine.fans.isEmpty())
            continue;

        return machine;
    }

    if (error) {
        *error = QStringLiteral(
            "ASUS fan controller not found under /sys/class/hwmon. "
            "Load the asus_wmi / asus_nb_wmi module and check that the machine exposes pwm devices.");
    }
    return Machine{};
}

QVector<FanStatus> readFanStatuses(const Machine &machine)
{
    QVector<FanStatus> statuses;
    if (!machine.isValid())
        return statuses;

    for (const FanSpec &spec : machine.fans) {
        FanStatus status;
        status.index = spec.index;
        status.name = spec.name;
        status.label = spec.label;

        bool ok = false;
        const int rpm = readFirstLine(machine.path + QStringLiteral("/fan%1_input").arg(spec.index))
                            .toInt(&ok);
        status.rpm = ok ? rpm : -1;

        const int mode = readFirstLine(machine.path + QStringLiteral("/pwm%1_enable").arg(spec.index))
                             .toInt(&ok);
        status.mode = ok ? mode : -1;

        statuses.append(status);
    }
    return statuses;
}

bool writeFanMode(const Machine &machine, const FanSpec &fan, int mode, QString *error)
{
    const QString path = machine.path + QStringLiteral("/pwm%1_enable").arg(fan.index);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("cannot open %1").arg(path);
        return false;
    }
    file.write(QByteArray::number(mode));
    file.flush();
    file.close();

    QFile check(path);
    if (check.open(QIODevice::ReadOnly | QIODevice::Text)) {
        bool ok = false;
        const int readBack = QString::fromLocal8Bit(check.readLine().trimmed()).toInt(&ok);
        if (!ok || readBack != mode) {
            if (error)
                *error = QStringLiteral("%1 rejected mode %2 (still %3)").arg(path).arg(mode).arg(readBack);
            return false;
        }
    }
    return true;
}

std::optional<double> readCpuTemp()
{
    auto value = readSensor(QStringLiteral("k10temp"), {QStringLiteral("Tctl"), QStringLiteral("TDIE")});
    if (value)
        return value;
    return readSensor(QStringLiteral("acpitz"), {});
}

std::optional<double> readGpuTemp()
{
    return readSensor(QStringLiteral("amdgpu"), {QStringLiteral("edge"), QStringLiteral("junction")});
}

QString modeName(int mode)
{
    switch (mode) {
    case ModeFull:
        return QStringLiteral("Full speed");
    case 1:
        return QStringLiteral("Manual");
    case ModeAuto:
        return QStringLiteral("Automatic");
    default:
        return QStringLiteral("Unknown");
    }
}

QString modeToWord(int mode)
{
    return mode == ModeFull ? QStringLiteral("full") : QStringLiteral("auto");
}

bool modeFromWord(const QString &word, ModeValue *mode)
{
    const QString lower = word.toLower();
    if (lower == QLatin1String("auto")) {
        if (mode)
            *mode = ModeAuto;
        return true;
    }
    if (lower == QLatin1String("full")) {
        if (mode)
            *mode = ModeFull;
        return true;
    }
    return false;
}

// The DMI identity files are world-readable and carry the same strings that
// `dmidecode -s` prints, so the complete name needs no root and no subprocess.
QString machineModel()
{
    QString brand = readFirstLine(QStringLiteral("/sys/class/dmi/id/sys_vendor"))
                        .section(QLatin1Char(' '), 0, 0);
    if (brand.compare(QLatin1String("ASUSTeK"), Qt::CaseInsensitive) == 0)
        brand = QStringLiteral("ASUS");  // the brand people use, not the legal entity

    const QString family = readFirstLine(QStringLiteral("/sys/class/dmi/id/product_family"));
    // ASUS packs product_name as "<family>_<series><model>_<model>", so the model is
    // the last underscore-separated segment.
    QString model = readFirstLine(QStringLiteral("/sys/class/dmi/id/product_name"))
                        .section(QLatin1Char('_'), -1);
    if (model.isEmpty())
        model = readFirstLine(QStringLiteral("/sys/class/dmi/id/board_name"));

    QStringList parts;
    for (const QString &part : {brand, family, model}) {
        if (!part.isEmpty() && !parts.contains(part))
            parts << part;
    }
    return parts.isEmpty() ? QStringLiteral("ASUS Vivobook") : parts.join(QLatin1Char(' '));
}

QString fanDisplayName(const QString &name)
{
    if (name == QStringLiteral("cpu"))
        return QStringLiteral("CPU");
    if (name == QStringLiteral("gpu"))
        return QStringLiteral("GPU");
    return name.toUpper();
}

}  // namespace fanctl
