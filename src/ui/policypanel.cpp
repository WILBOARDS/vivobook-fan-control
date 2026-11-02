#include "ui/policypanel.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/controller.h"
#include "core/policyconfig.h"
#include "ui/style.h"

namespace fanctl {

PolicyPanel::PolicyPanel(QWidget *parent) : QGroupBox(QStringLiteral("Automatic fan policy"), parent)
{
    auto *layout = new QVBoxLayout(this);
    style::applySection(layout);

    m_enabled = new QCheckBox(QStringLiteral("Run the policy service in the background"), this);
    m_enabled->setToolTip(QStringLiteral(
        "Enables vivobook-fan-policy.service, a root service that forces the fans to full speed "
        "above the trigger temperature and back to automatic below the release temperature."));

    m_note = new QLabel(this);
    style::applyHint(m_note);

    auto makeSpin = [this](int value) {
        auto *spin = new QSpinBox(this);
        spin->setRange(30, 110);
        spin->setSuffix(QStringLiteral(" °C"));
        spin->setValue(value);
        spin->setFixedWidth(96);
        return spin;
    };

    const PolicyConfig config = loadPolicyConfig();
    m_cpuFull = makeSpin(config.cpuFullAbove);
    m_cpuRelease = makeSpin(config.cpuAutoBelow);
    m_gpuFull = makeSpin(config.gpuFullAbove);
    m_gpuRelease = makeSpin(config.gpuAutoBelow);

    m_interval = new QSpinBox(this);
    m_interval->setRange(2, 60);
    m_interval->setSuffix(QStringLiteral(" s"));
    m_interval->setValue(config.intervalSeconds);
    m_interval->setFixedWidth(96);

    // One grid for both fans and the poll interval so every column stays aligned.
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(style::kSectionGap);
    grid->setVerticalSpacing(style::kControlGap);
    grid->addWidget(new QLabel(QStringLiteral("Full speed above"), this), 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("Back to automatic below"), this), 0, 2);
    grid->addWidget(new QLabel(QStringLiteral("CPU"), this), 1, 0);
    grid->addWidget(m_cpuFull, 1, 1);
    grid->addWidget(m_cpuRelease, 1, 2);
    grid->addWidget(new QLabel(QStringLiteral("GPU"), this), 2, 0);
    grid->addWidget(m_gpuFull, 2, 1);
    grid->addWidget(m_gpuRelease, 2, 2);
    grid->addWidget(new QLabel(QStringLiteral("Check every"), this), 3, 0);
    grid->addWidget(m_interval, 3, 1);
    grid->setColumnStretch(3, 1);

    m_apply = new QPushButton(QStringLiteral("Apply thresholds"), this);
    m_apply->setToolTip(QStringLiteral("Writes the thresholds to the policy service's config file."));
    auto *applyRow = new QHBoxLayout;
    applyRow->addStretch();
    applyRow->addWidget(m_apply);

    layout->addWidget(m_enabled);
    layout->addWidget(m_note);
    layout->addSpacing(style::kGap);
    layout->addLayout(grid);
    layout->addLayout(applyRow);

    QObject::connect(m_apply, &QPushButton::clicked, this, &PolicyPanel::applyThresholds);
    QObject::connect(m_enabled, &QCheckBox::toggled, this, &PolicyPanel::toggleService);

    reload();
}

void PolicyPanel::reload()
{
    m_syncing = true;
    m_enabled->setChecked(controller::policyServiceEnabled());
    m_syncing = false;

    m_note->setText(m_enabled->isChecked()
                        ? QStringLiteral("The service re-reads these values on every check, so applying "
                                         "takes effect without a restart. Manual switches still work, "
                                         "but the policy can move the fan again on the next check.")
                        : QStringLiteral("While this is off, the fans stay exactly where you and the "
                                         "firmware leave them."));
}

void PolicyPanel::applyThresholds()
{
    if (m_cpuRelease->value() >= m_cpuFull->value()) {
        emit statusMessage(QStringLiteral("The CPU release temperature must be lower than its trigger."),
                           true);
        return;
    }
    if (m_gpuRelease->value() >= m_gpuFull->value()) {
        emit statusMessage(QStringLiteral("The GPU release temperature must be lower than its trigger."),
                           true);
        return;
    }

    const QStringList assignments = {
        QStringLiteral("cpu_full_above=%1").arg(m_cpuFull->value()),
        QStringLiteral("cpu_auto_below=%1").arg(m_cpuRelease->value()),
        QStringLiteral("gpu_full_above=%1").arg(m_gpuFull->value()),
        QStringLiteral("gpu_auto_below=%1").arg(m_gpuRelease->value()),
        QStringLiteral("interval=%1").arg(m_interval->value()),
    };

    m_apply->setEnabled(false);
    emit statusMessage(QStringLiteral("Saving thresholds…"), false);

    controller::setPolicyConfig(assignments, [this](Result result) {
        m_apply->setEnabled(true);
        emit statusMessage(result.ok ? QStringLiteral("Thresholds saved.") : result.message, !result.ok);
    });
}

void PolicyPanel::toggleService(bool enable)
{
    if (m_syncing)
        return;

    m_enabled->setEnabled(false);
    emit statusMessage(enable ? QStringLiteral("Starting the policy service…")
                              : QStringLiteral("Stopping the policy service…"),
                       false);

    controller::setPolicyService(enable, [this, enable](Result result) {
        m_enabled->setEnabled(true);
        if (!result.ok) {
            m_syncing = true;
            m_enabled->setChecked(!enable);
            m_syncing = false;
        }
        emit statusMessage(result.message.isEmpty() ? QStringLiteral("Policy service updated.")
                                                    : result.message,
                           !result.ok);
        reload();
    });
}

}  // namespace fanctl
