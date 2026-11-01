// Development-only: renders the main window offscreen to a PNG so the layout
// can be inspected without a display. Not built by default and not packaged.
#include <QApplication>
#include <QTimer>

#include "ui/mainwindow.h"

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const QString output = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                    : QStringLiteral("/tmp/fancontrol-gui.png");
    // Real readings only: a populated 5 minute graph needs ~5 minutes of sampling.
    const int dwellSeconds = argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt() : 16;

    fanctl::MainWindow window;
    window.resize(760, 640);
    window.show();

    QTimer::singleShot(dwellSeconds * 1000, &app, [&window, output]() {
        const QPixmap pixmap = window.grab();
        pixmap.save(output);
        qInfo().noquote() << QStringLiteral("saved %1 (%2x%3)").arg(output).arg(pixmap.width()).arg(pixmap.height());
        QApplication::quit();
    });

    return app.exec();
}
