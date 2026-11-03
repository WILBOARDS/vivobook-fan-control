// Development-only: renders the main window offscreen to a PNG so the layout
// can be inspected without a display. Not built by default and not packaged.
#include <QApplication>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>

#include "ui/mainwindow.h"
#include "ui/theme.h"

int main(int argc, char *argv[])
{
    // Offscreen uses the fallback style; set QT_QPA_PLATFORM=xcb (or run under
    // Wayland) to see the layout in the theme the desktop actually applies.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("vivobook-fan-control"));
    QApplication::setOrganizationName(QStringLiteral("vivobook-fan-control"));

    // Same start-up sequence as main.cpp, so the snapshot shows the theme the
    // installed app would use.
    fanctl::theme::captureSystem(&app);
    fanctl::theme::apply(&app, fanctl::theme::stored());

    const QString output = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                    : QStringLiteral("/tmp/fancontrol-gui.png");
    // Real readings only: a populated 5 minute graph needs ~5 minutes of sampling.
    const int dwellSeconds = argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt() : 16;
    const bool settings = argc > 3 && QString::fromLocal8Bit(argv[3]) == QLatin1String("settings");
    const QStringList dims = argc > 4 ? QString::fromLocal8Bit(argv[4]).split(QLatin1Char('x'))
                                      : QStringLiteral("720x600").split(QLatin1Char('x'));

    fanctl::MainWindow window;
    window.resize(dims.size() == 2 ? QSize(dims.at(0).toInt(), dims.at(1).toInt()) : QSize(720, 600));
    window.show();

    QTimer::singleShot(dwellSeconds * 1000, &app, [&window, output, settings]() {
        if (auto *pages = window.findChild<QStackedWidget *>())
            pages->setCurrentIndex(settings ? 1 : 0);
        QApplication::processEvents();
        const QPixmap pixmap = window.grab();
        pixmap.save(output);
        qInfo().noquote() << QStringLiteral("saved %1 (%2x%3)").arg(output).arg(pixmap.width()).arg(pixmap.height());
        QApplication::quit();
    });

    return app.exec();
}
