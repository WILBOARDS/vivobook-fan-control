#pragma once

#include <QList>
#include <QMainWindow>
#include <QPointer>

#include "core/sensors.h"

class QCloseEvent;
class QLabel;
class QStackedWidget;
class QSystemTrayIcon;
class QTimer;
class QToolButton;

namespace fanctl {

class FanCard;
class HistoryChart;
class SettingsPage;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Background mode used by the login service: stay in the tray, and show the
    // window instead if the desktop offers no tray at all.
    void startHidden();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    enum Page {
        PageOverview = 0,
        PageSettings = 1,
    };

    QWidget *buildOverview();
    QWidget *buildSettings();
    void buildTray();
    void sample();
    void requestMode(const QString &fan, ModeValue mode);
    void showPage(Page page);
    void syncPage();
    // The glyph is painted from the palette, so a theme change means painting it again.
    void syncNavButton();

    Machine m_machine;
    QString m_discoveryError;
    QString m_deviceName;
    QString m_monitorName;
    QList<QPointer<FanCard>> m_cards;
    QStackedWidget *m_pages = nullptr;
    QToolButton *m_navButton = nullptr;
    QLabel *m_headline = nullptr;
    SettingsPage *m_settings = nullptr;
    HistoryChart *m_tempChart = nullptr;
    HistoryChart *m_rpmChart = nullptr;
    QLabel *m_pathLabel = nullptr;
    QTimer *m_timer = nullptr;
    ::QSystemTrayIcon *m_tray = nullptr;
    bool m_quitting = false;
    bool m_trayUnavailable = false;
};

}  // namespace fanctl
