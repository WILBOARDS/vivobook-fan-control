#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QIcon>
#include <QStringList>

#include "cli.h"
#include "ui/mainwindow.h"

int main(int argc, char *argv[])
{
    QStringList arguments;
    for (int i = 1; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);
    const QString programName = QFileInfo(QString::fromLocal8Bit(argv[0])).fileName();

    if (!fanctl::looksLikeGuiRequest(arguments, programName)) {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
        return fanctl::runCli(app.arguments().mid(1));
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("vivobook-fan-control"));
    QApplication::setApplicationDisplayName(QStringLiteral("Vivobook Fan Control"));
    QApplication::setOrganizationName(QStringLiteral("vivobook-fan-control"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("vivobook-fan-control"));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("vivobook-fan-control")));

    fanctl::MainWindow window;
    window.show();
    return app.exec();
}
