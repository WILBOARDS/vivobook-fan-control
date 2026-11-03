#include "ui/settingspage.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "core/controller.h"
#include "ui/policypanel.h"
#include "ui/style.h"
#include "ui/theme.h"

namespace fanctl {

SettingsPage::SettingsPage(QWidget *parent) : QFrame(parent)
{
    auto *layout = new QVBoxLayout(this);
    style::applyPage(layout);

    auto *appearance = new QGroupBox(QStringLiteral("Appearance"), this);
    auto *appearanceLayout = new QVBoxLayout(appearance);
    style::applySection(appearanceLayout);

    auto *themeRow = new QHBoxLayout;
    themeRow->setSpacing(style::kGap);
    themeRow->addWidget(new QLabel(QStringLiteral("Theme"), appearance));
    m_theme = new QComboBox(appearance);
    m_theme->addItem(QStringLiteral("System"), static_cast<int>(theme::Mode::System));
    m_theme->addItem(QStringLiteral("Light"), static_cast<int>(theme::Mode::Light));
    m_theme->addItem(QStringLiteral("Dark"), static_cast<int>(theme::Mode::Dark));
    m_theme->setToolTip(QStringLiteral(
        "System uses the colours your desktop theme provides. Light and Dark are built in, "
        "so the window looks the same even on a desktop that has no dark palette."));
    themeRow->addWidget(m_theme);
    themeRow->addStretch();
    appearanceLayout->addLayout(themeRow);
    appearanceLayout->addWidget(style::hint(
        "Applies to this window straight away and is remembered for the next start.", appearance));

    auto *startup = new QGroupBox(QStringLiteral("Startup"), this);
    auto *startupLayout = new QVBoxLayout(startup);
    style::applySection(startupLayout);

    m_bootCheck = new QCheckBox(QStringLiteral("Restore these fan modes at boot"), startup);
    m_bootCheck->setToolTip(QStringLiteral(
        "Writes the modes you choose on the overview page to "
        "/var/lib/vivobook-fan-control/modes and re-applies them early in the next boot."));
    startupLayout->addWidget(m_bootCheck);
    startupLayout->addWidget(style::hint(
        "Keeps the fans in whatever mode you left them, across a reboot.", startup));

    m_autostartCheck = new QCheckBox(QStringLiteral("Run in the background at login"), startup);
    m_autostartCheck->setToolTip(QStringLiteral(
        "Enables the systemd user service vivobook-fan-control.service, which starts this app in "
        "the tray with your session. No administrator rights are needed for a user service."));
    startupLayout->addSpacing(style::kGap);
    startupLayout->addWidget(m_autostartCheck);
    startupLayout->addWidget(style::hint(
        "Starts the app in the tray with your session, so you can change modes without opening "
        "the window. Needs a tray host.", startup));

    m_policy = new PolicyPanel(this);

    layout->addWidget(appearance);
    layout->addWidget(startup);
    layout->addWidget(m_policy);
    layout->addStretch();

    QObject::connect(m_bootCheck, &QCheckBox::toggled, this, [this](bool enable) {
        if (!m_syncingBoot)
            applyBootRestore(enable);
    });
    QObject::connect(m_autostartCheck, &QCheckBox::toggled, this, [this](bool enable) {
        if (!m_syncingAutostart)
            applyAutostart(enable);
    });
    QObject::connect(m_theme, &QComboBox::activated, this, [this](int index) {
        const auto mode = static_cast<theme::Mode>(m_theme->itemData(index).toInt());
        theme::store(mode);
        theme::apply(qApp, mode);
        emit themeChanged();
    });
    QObject::connect(m_policy, &PolicyPanel::statusMessage, this, &SettingsPage::statusMessage);

    refresh();
}

void SettingsPage::refresh()
{
    const int current = m_theme->findData(static_cast<int>(theme::current()));
    if (current >= 0)
        m_theme->setCurrentIndex(current);

    m_syncingBoot = true;
    m_bootCheck->setChecked(controller::bootRestoreEnabled());
    m_syncingBoot = false;

    m_syncingAutostart = true;
    m_autostartCheck->setChecked(controller::trayAutostartEnabled());
    m_syncingAutostart = false;

    m_policy->reload();
}

void SettingsPage::applyBootRestore(bool enable)
{
    m_bootCheck->setEnabled(false);
    emit statusMessage(enable ? QStringLiteral("Enabling boot restore…")
                              : QStringLiteral("Disabling boot restore…"),
                       false);

    controller::setBootRestore(enable, [this, enable](Result result) {
        m_bootCheck->setEnabled(true);
        if (!result.ok) {
            m_syncingBoot = true;
            m_bootCheck->setChecked(!enable);
            m_syncingBoot = false;
        }
        emit statusMessage(result.message, !result.ok);
    });
}

void SettingsPage::applyAutostart(bool enable)
{
    m_autostartCheck->setEnabled(false);
    emit statusMessage(enable ? QStringLiteral("Enabling background start…")
                              : QStringLiteral("Disabling background start…"),
                       false);

    const Result result = controller::setTrayAutostart(enable);
    m_autostartCheck->setEnabled(true);

    m_syncingAutostart = true;
    m_autostartCheck->setChecked(result.ok ? enable : !enable);
    m_syncingAutostart = false;

    if (result.ok) {
        emit statusMessage(enable ? QStringLiteral("Vivobook Fan Control will start with your "
                                                  "session and stay in the tray.")
                                  : QStringLiteral("Vivobook Fan Control will no longer start "
                                                   "with your session."),
                           false);
    } else {
        emit statusMessage(result.message, true);
    }
}

}  // namespace fanctl
