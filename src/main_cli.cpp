#include <QCoreApplication>
#include <QTextStream>

#include "cli.h"

// Command line only entry point: links against Qt Core alone, so this binary
// pulls no GUI stack onto a headless machine.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vivobook-fan-control"));
    QCoreApplication::setApplicationVersion(QStringLiteral(APP_VERSION));

    const QStringList arguments = app.arguments().mid(1);
    for (const QString &argument : arguments) {
        if (argument == QLatin1String("gui") || argument == QLatin1String("--gui")
            || argument == QLatin1String("--tray")) {
            QTextStream stream(stdout);
            stream << "This build is command line only. Install the vivobook-fan-control package "
                      "for the graphical window."
                   << Qt::endl;
            return 2;
        }
    }

    return fanctl::runCli(arguments);
}
