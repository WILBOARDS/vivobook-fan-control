#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
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

    const bool trayMode = arguments.contains(QStringLiteral("--tray"));

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("vivobook-fan-control"));
    QApplication::setApplicationDisplayName(QStringLiteral("Vivobook Fan Control"));
    QApplication::setOrganizationName(QStringLiteral("vivobook-fan-control"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("vivobook-fan-control"));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("vivobook-fan-control")));

    // The login service and a manual launch would otherwise leave two tray icons
    // fighting over the same fans.
    QLockFile lock(QDir::tempPath() + QStringLiteral("/vivobook-fan-control.lock"));
    if (!lock.tryLock(100)) {
        if (!trayMode) {
            QMessageBox::information(nullptr, QStringLiteral("Vivobook Fan Control"),
                                     QStringLiteral("Vivobook Fan Control is already running in the "
                                                    "background. Use its tray icon to change fan modes."));
        }
        return 0;
    }

    fanctl::MainWindow window;
    if (trayMode)
        window.startHidden();
    else
        window.show();
    return app.exec();
}
