#pragma once

#include <QString>
#include <QVector>

#include <optional>

namespace fanctl {

// pwmN_enable values understood by the asus hwmon driver on the M7400QE.
// The driver exposes no pwmN value file, so manual (1) is not selectable.
enum ModeValue {
    ModeFull = 0,
    ModeAuto = 2,
};

struct FanSpec {
    int index = 0;
    QString name;
    QString label;
};

struct FanStatus {
    int index = 0;
    QString name;
    QString label;
    int rpm = -1;
    int mode = -1;
};

struct Machine {
    QString path;
    QVector<FanSpec> fans;

    bool isValid() const { return !path.isEmpty(); }
    bool has(const QString &name) const;
    std::optional<FanSpec> find(const QString &name) const;
};

Machine discoverMachine(QString *error = nullptr);

QVector<FanStatus> readFanStatuses(const Machine &machine);

// Writes pwmN_enable and reads it back, because sysfs accepts a write even when
// the firmware refuses the mode. Callers must hold permission to write sysfs,
// i.e. the root helper or the policy daemon.
bool writeFanMode(const Machine &machine, const FanSpec &fan, int mode, QString *error = nullptr);

std::optional<double> readCpuTemp();
std::optional<double> readGpuTemp();

QString modeName(int mode);
QString fanDisplayName(const QString &name);

// Complete DMI machine name, e.g. "ASUS Vivobook M7400QE" on a Vivobook 14X.
QString machineModel();

// Wire words used by the CLI, the GUI and the root helper.
QString modeToWord(int mode);
bool modeFromWord(const QString &word, ModeValue *mode);

}  // namespace fanctl
