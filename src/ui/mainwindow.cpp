#include "ui/mainwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDateTime>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/controller.h"
#include "ui/fancard.h"
#include "ui/historychart.h"
#include "ui/settingspage.h"
#include "ui/style.h"

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

// The desktop icon theme has no dependable settings glyph, so the nav buttons
// draw their own and stay identical on any theme. Everything is square-ended:
// flat caps and mitre joins, no rounded corners.
QIcon navIcon(const QString &kind, const QColor &color)
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    if (kind == QLatin1String("back")) {
        painter.setPen(QPen(color, 8, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
        painter.drawPolyline(QPolygonF() << QPointF(41, 13) << QPointF(22, 32) << QPointF(41, 51));
    } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        for (int i = 0; i < 8; ++i) {
            painter.save();
            painter.translate(32, 32);
            painter.rotate(i * 45.0);
            painter.drawRect(QRectF(-4.5, -30, 9, 14));
            painter.restore();
        }
        painter.drawEllipse(QRectF(12, 12, 40, 40));
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.drawEllipse(QRectF(24, 24, 16, 16));
    }

    painter.end();
    return QIcon(pixmap);
}

bool statusNotifierRegistered()
{
    return QDBusConnection::sessionBus().isConnected()
           && QDBusConnection::sessionBus().interface()->isServiceRegistered(
                  QStringLiteral("org.kde.StatusNotifierWatcher"));
}

// A tray host resolves IconName through its own icon theme, which does not know
// ours unless the hicolor fallback is searched. Carrying only pixmaps leaves the
// host nothing to fail at: the artwork is already in the DBus message.
QIcon trayIcon()
{
    QIcon themed = QIcon::fromTheme(QStringLiteral("vivobook-fan-control"));
    if (themed.isNull())
        return appIcon();

    QIcon icon;
    for (int size : {16, 22, 24, 32, 48, 64})
        icon.addPixmap(themed.pixmap(size, size));
    return icon.isNull() ? appIcon() : icon;
}

}  // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Vivobook Fan Control"));
    setWindowIcon(appIcon());
    resize(720, 600);
    // Deliberately no setMinimumSize(): a hand-picked floor below what the layout
    // needs makes Qt clip the bottom of the page instead of refusing to shrink.

    m_machine = discoverMachine(&m_discoveryError);
    m_deviceName = m_machine.isValid() ? machineModel() : QStringLiteral("No fans to control");
    // The status bar is narrow; the full sysfs path stays available on hover.
    m_monitorName = m_machine.path.section(QLatin1Char('/'), -1);

    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    style::applyPage(centralLayout);
    centralLayout->setContentsMargins(0, 0, 0, 0);

    // One header above both pages: the title says where you are, the gear on the
    // right is what moves between them.
    m_headline = new QLabel(central);
    QFont headlineFont = m_headline->font();
    headlineFont.setBold(true);
    headlineFont.setPointSizeF(headlineFont.pointSizeF() * 1.15);
    m_headline->setFont(headlineFont);
    // One line: the model reads as a name, and a name broken over three lines
    // reads as three separate things.
    m_headline->setText(m_deviceName);

    m_navButton = new QToolButton(central);
    m_navButton->setIconSize(QSize(20, 20));
    m_navButton->setFixedSize(32, 32);
    m_navButton->setAutoRaise(false);
    // A flat square outline instead of the theme's rounded button.
    m_navButton->setStyleSheet(QStringLiteral(
        "QToolButton{border:1px solid palette(Mid);border-radius:0;background:transparent;}"
        "QToolButton:hover{border-color:palette(Dark);background:palette(Mid);}"
        "QToolButton:pressed{border-color:palette(Dark);background:palette(Dark);}"));
    m_navButton->setShortcut(QKeySequence::Preferences);
    syncNavButton();
    QObject::connect(m_navButton, &QToolButton::clicked, this, [this] {
        if (!m_pages)
            return;
        showPage(m_pages->currentIndex() == PageOverview ? PageSettings : PageOverview);
    });

    auto *header = new QHBoxLayout;
    header->setContentsMargins(style::kPageMargin, style::kPageMargin, style::kPageMargin, 0);
    header->setSpacing(style::kGap);
    header->addWidget(m_headline);
    header->addStretch();
    header->addWidget(m_navButton);
    centralLayout->addLayout(header);

    if (m_machine.isValid()) {
        m_pages = new QStackedWidget(central);
        m_pages->addWidget(buildOverview());
        m_pages->addWidget(buildSettings());
        QObject::connect(m_pages, &QStackedWidget::currentChanged, this, &MainWindow::syncPage);
        centralLayout->addWidget(m_pages, 1);
    } else {
        m_navButton->setVisible(false);
        centralLayout->addWidget(style::hint(m_discoveryError, central));
        centralLayout->addStretch(1);
    }

    setCentralWidget(central);

    m_pathLabel = new QLabel(central);
    style::applyHint(m_pathLabel);
    statusBar()->addPermanentWidget(m_pathLabel);

    if (!m_machine.isValid()) {
        statusBar()->showMessage(m_discoveryError);
        return;
    }

    m_pathLabel->setText(m_monitorName);
    m_pathLabel->setToolTip(m_machine.path);
    buildTray();

    m_timer = new QTimer(this);
    QObject::connect(m_timer, &QTimer::timeout, this, &MainWindow::sample);
    m_timer->start(2000);
    sample();

    auto *quit = new QShortcut(QKeySequence::Quit, this);
    QObject::connect(quit, &QShortcut::activated, qApp, &QApplication::quit);
}

QWidget *MainWindow::buildOverview()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    style::applyPage(layout);

    auto *cards = new QHBoxLayout;
    cards->setSpacing(style::kSectionGap);
    for (const FanSpec &spec : m_machine.fans) {
        auto *card = new FanCard(spec, page);
        QObject::connect(card, &FanCard::modeRequested, this, &MainWindow::requestMode);
        cards->addWidget(card);
        m_cards.append(card);
    }
    layout->addLayout(cards);

    style::addSectionGap(layout);

    m_tempChart = new HistoryChart(QStringLiteral("Temperature"), QStringLiteral("°C"), 0.0, 100.0, page);
    m_rpmChart = new HistoryChart(QStringLiteral("Fan speed"), QStringLiteral("rpm"), -1.0, -1.0, page);
    m_rpmChart->setRetentionSeconds(300);
    layout->addWidget(m_tempChart, 1);
    layout->addWidget(m_rpmChart, 1);

    style::addSectionGap(layout);

    auto *autoButton = new QPushButton(QStringLiteral("Return to automatic"), page);
    QObject::connect(autoButton, &QPushButton::clicked, this,
                     [this] { requestMode(QStringLiteral("all"), ModeAuto); });

    auto *fullButton = new QPushButton(QStringLiteral("Blast both fans"), page);
    fullButton->setToolTip(QStringLiteral("Force both fans to full speed until you switch back."));
    QObject::connect(fullButton, &QPushButton::clicked, this,
                     [this] { requestMode(QStringLiteral("all"), ModeFull); });

    auto *actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(style::kGap);
    actions->addStretch();
    actions->addWidget(autoButton);
    actions->addWidget(fullButton);
    layout->addLayout(actions);

    return page;
}

QWidget *MainWindow::buildSettings()
{
    m_settings = new SettingsPage(this);
    QObject::connect(m_settings, &SettingsPage::statusMessage, this,
                     [this](const QString &text, bool error) {
                         statusBar()->showMessage(text, error ? 12000 : 6000);
                     });
    QObject::connect(m_settings, &SettingsPage::themeChanged, this, &MainWindow::syncNavButton);

    auto *back = new QShortcut(QKeySequence::Cancel, m_settings);
    back->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(back, &QShortcut::activated, this, [this] { showPage(PageOverview); });

    return m_settings;
}

void MainWindow::showPage(Page page)
{
    m_pages->setCurrentIndex(page);
}

void MainWindow::syncPage()
{
    const bool settings = m_pages->currentIndex() == PageSettings;
    m_headline->setText(settings ? QStringLiteral("Settings") : m_deviceName);
    syncNavButton();
    if (settings)
        m_settings->refresh();
}

void MainWindow::syncNavButton()
{
    const bool settings = m_pages && m_pages->currentIndex() == PageSettings;
    // The application palette, because the copy that reaches this button only
    // lands on the next turn of the event loop.
    m_navButton->setIcon(navIcon(settings ? QStringLiteral("back") : QStringLiteral("gear"),
                                 qApp->palette().color(QPalette::ButtonText)));
    m_navButton->setAccessibleName(settings ? QStringLiteral("Back")
                                            : QStringLiteral("Settings"));
    m_navButton->setToolTip(settings ? QStringLiteral("Back to the fans (Esc)")
                                     : QStringLiteral("Startup and policy settings (Ctrl+,)"));
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable() && !statusNotifierRegistered()) {
        m_trayUnavailable = true;
        return;
    }

    m_tray = new QSystemTrayIcon(trayIcon(), this);
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
        std::optional<double> temp;
        if (status.name == QStringLiteral("cpu"))
            temp = cpuTemp;
        else if (status.name == QStringLiteral("gpu"))
            temp = gpuTemp;

        for (const QPointer<FanCard> &card : m_cards) {
            if (card && card->fanName() == status.name)
                card->updateStatus(status, temp);
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
                             .arg(m_monitorName,
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

void MainWindow::startHidden()
{
    if (m_tray)
        return;
    // Nothing to hide into, so a background start would leave the app unusable.
    show();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_quitting || !m_tray) {
        event->accept();
        return;
    }

    QSettings settings;
    const QString remembered = settings.value(QStringLiteral("closeAction")).toString();
    if (settings.value(QStringLiteral("rememberCloseChoice"), false).toBool() && !remembered.isEmpty()) {
        if (remembered == QLatin1String("tray")) {
            hide();
            event->ignore();
            return;
        }
        m_quitting = true;
        event->accept();
        QApplication::quit();
        return;
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("Keep running in the background?"));
    box.setText(QStringLiteral("Vivobook Fan Control can stay in the tray so you can change fan "
                               "modes without opening the window again."));
    QPushButton *trayButton = box.addButton(QStringLiteral("Minimize to tray"), QMessageBox::AcceptRole);
    QPushButton *quitButton = box.addButton(QStringLiteral("Quit"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    auto *remember = new QCheckBox(QStringLiteral("Remember my choice"), &box);
    box.setCheckBox(remember);
    box.setDefaultButton(trayButton);
    box.exec();

    if (box.clickedButton() == trayButton) {
        if (remember->isChecked()) {
            settings.setValue(QStringLiteral("rememberCloseChoice"), true);
            settings.setValue(QStringLiteral("closeAction"), QStringLiteral("tray"));
        }
        hide();
        event->ignore();
        return;
    }

    if (box.clickedButton() == quitButton) {
        if (remember->isChecked()) {
            settings.setValue(QStringLiteral("rememberCloseChoice"), true);
            settings.setValue(QStringLiteral("closeAction"), QStringLiteral("quit"));
        }
        m_quitting = true;
        event->accept();
        QApplication::quit();
        return;
    }

    event->ignore();
}

}  // namespace fanctl
