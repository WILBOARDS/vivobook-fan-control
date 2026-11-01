#pragma once

#include <QList>
#include <QMainWindow>
#include <QPointer>

#include "core/sensors.h"

class QCheckBox;
class QCloseEvent;
class QLabel;
class QSystemTrayIcon;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace fanctl {

class FanCard;
class HistoryChart;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void buildCards();
    void buildControls();
    void buildTray();
    void sample();
    void requestMode(const QString &fan, ModeValue mode);
    void applyBootRestore(bool enable);

    Machine m_machine;
    QString m_discoveryError;
    QList<QPointer<FanCard>> m_cards;
    QVBoxLayout *m_root = nullptr;
    HistoryChart *m_tempChart = nullptr;
    HistoryChart *m_rpmChart = nullptr;
    QCheckBox *m_bootCheck = nullptr;
    QLabel *m_pathLabel = nullptr;
    QLabel *m_headline = nullptr;
    QTimer *m_timer = nullptr;
    ::QSystemTrayIcon *m_tray = nullptr;
    bool m_syncingBootCheck = false;
    bool m_trayUnavailable = false;
};

}  // namespace fanctl
