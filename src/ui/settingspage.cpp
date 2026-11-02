#include "ui/settingspage.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

#include "core/controller.h"
#include "ui/policypanel.h"
#include "ui/style.h"

namespace fanctl {

SettingsPage::SettingsPage(QWidget *parent) : QFrame(parent)
{
    auto *layout = new QVBoxLayout(this);
    style::applyPage(layout);

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
    QObject::connect(m_policy, &PolicyPanel::statusMessage, this, &SettingsPage::statusMessage);

    refresh();
}

void SettingsPage::refresh()
{
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
