#include "ui/mainwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDateTime>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QShortcut>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "core/controller.h"
#include "ui/fancard.h"
#include "ui/historychart.h"

namespace fanctl {
namespace {

QIcon appIcon()
{
    QIcon themed = QIcon::fromTheme(QStringLiteral("vivobook-fan-control"));
    if (!themed.isNull())
        return themed;

    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x2B, 0x6C, 0xB4));
    painter.drawRoundedRect(2, 2, 60, 60, 14, 14);
    painter.setBrush(QColor(0xF2, 0xF5, 0xF8));
    for (int i = 0; i < 3; ++i) {
        painter.save();
        painter.translate(32, 32);
        painter.rotate(i * 120);
        painter.drawEllipse(QRectF(2, -6, 20, 12));
        painter.restore();
    }
    painter.drawEllipse(QRectF(26, 26, 12, 12));
    painter.end();
    return QIcon(pixmap);
}

bool statusNotifierRegistered()
{
    return QDBusConnection::sessionBus().isConnected()
           && QDBusConnection::sessionBus().interface()->isServiceRegistered(
                  QStringLiteral("org.kde.StatusNotifierWatcher"));
}

}  // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Vivobook Fan Control"));
    setWindowIcon(appIcon());
    resize(760, 640);
    setMinimumSize(560, 480);

    m_machine = discoverMachine(&m_discoveryError);

    auto *central = new QWidget(this);
    m_root = new QVBoxLayout(central);

    m_headline = new QLabel(central);
    QFont headlineFont = m_headline->font();
    headlineFont.setBold(true);
    headlineFont.setPointSizeF(headlineFont.pointSizeF() * 1.15);
    m_headline->setFont(headlineFont);
    m_headline->setWordWrap(true);
    m_root->addWidget(m_headline);

    if (m_machine.isValid()) {
        m_headline->setText(QStringLiteral("ASUS %1").arg(machineModel()));
        buildCards();
        m_tempChart = new HistoryChart(QStringLiteral("Temperature"), QStringLiteral("°C"), 0.0, 100.0,
                                       central);
        m_rpmChart = new HistoryChart(QStringLiteral("Fan speed"), QStringLiteral("rpm"), -1.0, -1.0,
                                      central);
        m_rpmChart->setRetentionSeconds(300);
        m_root->addWidget(m_tempChart);
        m_root->addWidget(m_rpmChart);
        buildControls();
    } else {
        m_headline->setText(QStringLiteral("No fans to control"));
        m_root->addStretch();
    }
    m_root->addStretch();

    setCentralWidget(central);

    m_pathLabel = new QLabel(central);
    statusBar()->addPermanentWidget(m_pathLabel);

    if (!m_machine.isValid()) {
        statusBar()->showMessage(m_discoveryError);
        return;
    }

    m_pathLabel->setText(m_machine.path);
    buildTray();

    m_timer = new QTimer(this);
    QObject::connect(m_timer, &QTimer::timeout, this, &MainWindow::sample);
    m_timer->start(2000);
    sample();

    auto *quit = new QShortcut(QKeySequence::Quit, this);
    QObject::connect(quit, &QShortcut::activated, qApp, &QApplication::quit);
}

void MainWindow::buildCards()
{
    auto *row = new QHBoxLayout;
    for (const FanSpec &spec : m_machine.fans) {
        auto *card = new FanCard(spec, this);
        QObject::connect(card, &FanCard::modeRequested, this, &MainWindow::requestMode);
        row->addWidget(card);
        m_cards.append(card);
    }
    m_root->addLayout(row);
}

void MainWindow::buildControls()
{
    m_bootCheck = new QCheckBox(QStringLiteral("Restore these fan modes at boot"), this);
    m_bootCheck->setToolTip(QStringLiteral(
        "Writes the modes you choose here to /var/lib/vivobook-fan-control/modes and re-applies "
        "them early in the next boot."));
    QObject::connect(m_bootCheck, &QCheckBox::toggled, this, [this](bool enable) {
        if (!m_syncingBootCheck)
            applyBootRestore(enable);
    });
    m_bootCheck->setChecked(controller::bootRestoreEnabled());

    auto *fullButton = new QToolButton(this);
    fullButton->setText(QStringLiteral("Blast both fans"));
    fullButton->setToolTip(QStringLiteral("Force both fans to full speed until you switch back."));
    QObject::connect(fullButton, &QToolButton::clicked, this,
                     [this] { requestMode(QStringLiteral("all"), ModeFull); });

    auto *autoButton = new QToolButton(this);
    autoButton->setText(QStringLiteral("Return to automatic"));
    QObject::connect(autoButton, &QToolButton::clicked, this,
                     [this] { requestMode(QStringLiteral("all"), ModeAuto); });

    auto *row = new QHBoxLayout;
    row->addWidget(m_bootCheck);
    row->addStretch();
    row->addWidget(autoButton);
    row->addWidget(fullButton);
    m_root->addLayout(row);
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable() && !statusNotifierRegistered()) {
        m_trayUnavailable = true;
        return;
    }

    m_tray = new QSystemTrayIcon(appIcon(), this);
    auto *menu = new QMenu(this);
    menu->addAction(QStringLiteral("Show window"), this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
    menu->addSeparator();
    menu->addAction(QStringLiteral("All fans: automatic"), this,
                    [this] { requestMode(QStringLiteral("all"), ModeAuto); });
    menu->addAction(QStringLiteral("All fans: full speed"), this,
                    [this] { requestMode(QStringLiteral("all"), ModeFull); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("Quit"), qApp, &QApplication::quit);
    m_tray->setContextMenu(menu);
    m_tray->setToolTip(QStringLiteral("Vivobook Fan Control"));

    QObject::connect(m_tray, &QSystemTrayIcon::activated, this,
                     [this](QSystemTrayIcon::ActivationReason reason) {
                         if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                             if (isVisible())
                                 hide();
                             else
                                 showNormal();
                         }
                     });
    m_tray->show();
}

void MainWindow::sample()
{
    const QVector<FanStatus> statuses = readFanStatuses(m_machine);
    const std::optional<double> cpuTemp = readCpuTemp();
    const std::optional<double> gpuTemp = readGpuTemp();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    QStringList tooltipParts;
    double cpuRpm = -1000.0;
    double gpuRpm = -1000.0;

    for (const FanStatus &status : statuses) {
        for (const QPointer<FanCard> &card : m_cards) {
            if (card && card->fanName() == status.name) {
                std::optional<double> temp;
                if (status.name == QStringLiteral("cpu"))
                    temp = cpuTemp;
                else if (status.name == QStringLiteral("gpu"))
                    temp = gpuTemp;
                card->updateStatus(status, temp);
                if (status.mode != ModeAuto && status.mode != ModeFull && status.mode != 1)
                    card->showMessage(QStringLiteral("This mode is not managed by this tool."), true);
            }
        }

        if (status.name == QStringLiteral("cpu"))
            cpuRpm = status.rpm;
        else if (status.name == QStringLiteral("gpu"))
            gpuRpm = status.rpm;

        tooltipParts << QStringLiteral("%1 %2 RPM")
                            .arg(fanDisplayName(status.name))
                            .arg(status.rpm >= 0 ? QString::number(status.rpm) : QStringLiteral("--"));
    }

    m_tempChart->append(now, cpuTemp.value_or(-1000.0), gpuTemp.value_or(-1000.0));
    m_rpmChart->append(now, cpuRpm, gpuRpm);

    if (m_tray) {
        m_tray->setToolTip(tooltipParts.join(QStringLiteral(" · ")));
    }

    m_pathLabel->setText(QStringLiteral("%1 · updated %2%3")
                             .arg(m_machine.path,
                                  QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
                                  m_trayUnavailable ? QStringLiteral(" · tray unavailable")
                                                    : QString()));
}

void MainWindow::requestMode(const QString &fan, ModeValue mode)
{
    QList<QPointer<FanCard>> touched;
    for (const QPointer<FanCard> &card : m_cards) {
        if (!card)
            continue;
        if (fan == QStringLiteral("all") || card->fanName() == fan) {
            card->setBusy(true);
            card->showMessage(QStringLiteral("Applying… Authenticating if prompted."), false);
            touched.append(card);
        }
    }

    statusBar()->showMessage(QStringLiteral("Waiting for authentication…"));

    controller::applyMode(fan, mode, [this, touched](Result result) {
        for (const QPointer<FanCard> &card : touched) {
            if (!card)
                continue;
            card->setBusy(false);
            card->showMessage(result.ok ? QString() : result.message, !result.ok);
        }
        if (result.ok) {
            statusBar()->showMessage(result.message, 6000);
            sample();
        } else {
            statusBar()->showMessage(result.message, 12000);
        }
    });
}

void MainWindow::applyBootRestore(bool enable)
{
    m_bootCheck->setEnabled(false);
    statusBar()->showMessage(enable ? QStringLiteral("Enabling boot restore…")
                                   : QStringLiteral("Disabling boot restore…"));

    controller::setBootRestore(enable, [this, enable](Result result) {
        m_bootCheck->setEnabled(true);
        if (!result.ok) {
            m_syncingBootCheck = true;
            m_bootCheck->setChecked(!enable);
            m_syncingBootCheck = false;
            statusBar()->showMessage(result.message, 12000);
            return;
        }
        statusBar()->showMessage(result.message, 6000);
    });
}

}  // namespace fanctl
