#include "cli.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTextStream>

#include "core/controller.h"
#include "core/policyconfig.h"
#include "core/sensors.h"

namespace fanctl {
namespace {

void print(const QString &text = QString())
{
    QTextStream stream(stdout);
    stream << text << Qt::endl;
}

void printUsage(const QString &program)
{
    print(QStringLiteral(
        "Usage:\n"
        "  %1 status                     Show fan modes, RPM and temperatures\n"
        "  %1 cpu auto|full             Set the CPU fan mode\n"
        "  %1 gpu auto|full             Set the GPU fan mode\n"
        "  %1 all-auto                  Restore both fans to automatic\n"
        "  %1 all-full                  Run both fans at full speed\n"
        "  %1 boot on|off               Remember fan modes and re-apply them at boot\n"
        "  %1 policy show               Show the temperature policy and its service state\n"
        "  %1 policy on|off             Run the policy service in the background\n"
        "  %1 policy set <key=value>    Change thresholds (cpu_full_above, cpu_auto_below, ...)\n"
        "  %1 gui                       Open the graphical window\n"
        "\n"
        "Fan changes go through a polkit-authorized root helper, so you may be\n"
        "asked to authenticate once per session.").arg(program));
}

int printStatus()
{
    QString error;
    const Machine machine = discoverMachine(&error);
    if (!machine.isValid()) {
        print(error);
        return 1;
    }

    print(QStringLiteral("=== ASUS Vivobook Fan Status ==="));
    for (const FanStatus &status : readFanStatuses(machine)) {
        const QString rpm = status.rpm >= 0 ? QStringLiteral("%1 RPM").arg(status.rpm)
                                           : QStringLiteral("n/a");
        print(QStringLiteral("%1 Fan: %2 | %3")
                  .arg(fanDisplayName(status.name), rpm, modeName(status.mode)));
    }

    if (auto cpu = readCpuTemp())
        print(QStringLiteral("CPU Temp: %1 C").arg(*cpu, 0, 'f', 1));
    if (auto gpu = readGpuTemp())
        print(QStringLiteral("GPU Temp: %1 C").arg(*gpu, 0, 'f', 1));

    print();
    print(QStringLiteral("Hardware path: %1").arg(machine.path));
    print(QStringLiteral("Restore at boot: %1")
              .arg(controller::bootRestoreEnabled() ? QStringLiteral("enabled")
                                                   : QStringLiteral("disabled")));
    return 0;
}

int setFans(const QString &fan, const QString &word)
{
    ModeValue mode = ModeAuto;
    if (!modeFromWord(word, &mode)) {
        print(QStringLiteral("Mode must be auto or full"));
        return 1;
    }

    const Result result = controller::applyModeSync(fan, mode);
    print(result.message.isEmpty()
              ? (result.ok ? QStringLiteral("%1 fan set to %2").arg(fan, word) : QStringLiteral("Failed."))
              : result.message);
    return result.ok ? 0 : 1;
}

int setBoot(const QString &word)
{
    if (word != QStringLiteral("on") && word != QStringLiteral("off")) {
        print(QStringLiteral("Boot restore must be on or off"));
        return 1;
    }
    const bool enable = word == QStringLiteral("on");
    const Result result = controller::setBootRestoreSync(enable);
    if (!result.message.isEmpty()) {
        print(result.message);
        return result.ok ? 0 : 1;
    }
    print(QStringLiteral("Boot restore %1.")
              .arg(enable ? QStringLiteral("enabled") : QStringLiteral("disabled")));
    return 0;
}

int showPolicy()
{
    const fanctl::PolicyConfig config = loadPolicyConfig();
    print(QStringLiteral("=== ASUS Vivobook Fan Policy ==="));
    print(QStringLiteral("Service: %1")
              .arg(controller::policyServiceEnabled() ? QStringLiteral("running")
                                                     : QStringLiteral("stopped")));
    print(QStringLiteral("CPU: full above %1 C, back to automatic below %2 C")
              .arg(config.cpuFullAbove).arg(config.cpuAutoBelow));
    print(QStringLiteral("GPU: full above %1 C, back to automatic below %2 C")
              .arg(config.gpuFullAbove).arg(config.gpuAutoBelow));
    print(QStringLiteral("Check interval: %1 s").arg(config.intervalSeconds));
    print();
    print(QStringLiteral("Config file: %1").arg(policyConfigPath()));
    return 0;
}

int runPolicy(const QStringList &rest)
{
    if (rest.isEmpty() || rest.at(0) == QStringLiteral("show"))
        return showPolicy();

    const QString verb = rest.at(0);
    if (verb == QStringLiteral("on") || verb == QStringLiteral("off")) {
        const bool enable = verb == QStringLiteral("on");
        const Result result = controller::setPolicyServiceSync(enable);
        print(result.message.isEmpty() ? QStringLiteral("Policy service %1.").arg(verb) : result.message);
        return result.ok ? 0 : 1;
    }

    if (verb == QStringLiteral("set")) {
        const QStringList assignments = rest.mid(1);
        if (assignments.isEmpty()) {
            print(QStringLiteral("Usage: policy set cpu_full_above=85 cpu_auto_below=75 "
                                 "gpu_full_above=85 gpu_auto_below=75 interval=5"));
            return 1;
        }
        const Result result = controller::setPolicyConfigSync(assignments);
        print(result.message.isEmpty() ? QStringLiteral("Policy thresholds saved.") : result.message);
        return result.ok ? 0 : 1;
    }

    print(QStringLiteral("Policy must be show, on, off or set"));
    return 1;
}

const QStringList &cliVerbs()
{
    static const QStringList verbs = {QStringLiteral("status"), QStringLiteral("cpu"),
                                      QStringLiteral("gpu"), QStringLiteral("all-auto"),
                                      QStringLiteral("all-full"), QStringLiteral("boot"),
                                      QStringLiteral("policy"), QStringLiteral("gui"),
                                      QStringLiteral("help"), QStringLiteral("version")};
    return verbs;
}

}  // namespace

bool looksLikeGuiRequest(const QStringList &arguments, const QString &programName)
{
    const bool guiBinary = programName == QLatin1String("vivobook-fan-control");

    if (!arguments.isEmpty()) {
        const QString first = arguments.at(0);
        if (first == QStringLiteral("gui") || first == QStringLiteral("--gui"))
            return true;
        if (cliVerbs().contains(first) || first == QStringLiteral("-h")
            || first == QStringLiteral("--help") || first == QStringLiteral("-v")
            || first == QStringLiteral("--version"))
            return false;
        // An unknown option on the command line binary should print usage, not
        // open a window.
        return guiBinary;
    }
    return guiBinary;
}

int runCli(const QStringList &arguments)
{
    const QString program = QFileInfo(QCoreApplication::applicationFilePath()).fileName();

    if (arguments.isEmpty() || arguments.at(0) == QStringLiteral("status"))
        return printStatus();

    const QString verb = arguments.at(0);
    const QString second = arguments.size() > 1 ? arguments.at(1) : QString();

    if (verb == QStringLiteral("cpu") || verb == QStringLiteral("gpu")) {
        if (second.isEmpty()) {
            print(QStringLiteral("Mode must be auto or full"));
            return 1;
        }
        return setFans(verb, second);
    }
    if (verb == QStringLiteral("all-auto"))
        return setFans(QStringLiteral("all"), QStringLiteral("auto"));
    if (verb == QStringLiteral("all-full"))
        return setFans(QStringLiteral("all"), QStringLiteral("full"));
    if (verb == QStringLiteral("boot"))
        return setBoot(second);
    if (verb == QStringLiteral("policy"))
        return runPolicy(arguments.mid(1));
    if (verb == QStringLiteral("version")) {
        print(QStringLiteral("vivobook-fan-control %1").arg(QCoreApplication::applicationVersion()));
        return 0;
    }
    if (verb == QStringLiteral("help") || verb == QStringLiteral("-h") || verb == QStringLiteral("--help")) {
        printUsage(program);
        return 0;
    }

    print(QStringLiteral("Unknown command: %1").arg(verb));
    printUsage(program);
    return 1;
}

}  // namespace fanctl
