#include "ui/fancard.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QRadioButton>
#include <QVBoxLayout>

#include "ui/style.h"

namespace fanctl {
namespace {

QColor readableRed(const QWidget *widget)
{
    const bool dark = widget->palette().color(QPalette::Window).lightness() < 128;
    return dark ? QColor(0xE8, 0x8B, 0x7E) : QColor(0xB0, 0x35, 0x2A);
}

}  // namespace

FanCard::FanCard(const FanSpec &spec, QWidget *parent)
    : QGroupBox(QStringLiteral("%1 fan").arg(fanDisplayName(spec.name)), parent), m_spec(spec)
{
    m_rpm = new QLabel(this);
    QFont rpmFont = m_rpm->font();
    rpmFont.setPointSizeF(rpmFont.pointSizeF() * 1.7);
    rpmFont.setBold(true);
    m_rpm->setFont(rpmFont);

    m_temp = new QLabel(this);

    m_auto = new QRadioButton(QStringLiteral("Automatic"), this);
    m_full = new QRadioButton(QStringLiteral("Full speed"), this);

    QObject::connect(m_auto, &QRadioButton::clicked, this,
                     [this] { emit modeRequested(m_spec.name, ModeAuto); });
    QObject::connect(m_full, &QRadioButton::clicked, this,
                     [this] { emit modeRequested(m_spec.name, ModeFull); });

    m_message = new QLabel(this);
    m_message->setWordWrap(true);
    m_message->hide();

    auto *readout = new QHBoxLayout;
    readout->setSpacing(style::kGap);
    readout->addWidget(m_rpm);
    readout->addStretch();
    readout->addWidget(m_temp, 0, Qt::AlignBottom);

    auto *modes = new QHBoxLayout;
    modes->setSpacing(style::kSectionGap);
    modes->addWidget(m_auto);
    modes->addWidget(m_full);
    modes->addStretch();

    auto *layout = new QVBoxLayout(this);
    style::applySection(layout);
    layout->addLayout(readout);
    layout->addLayout(modes);
    layout->addWidget(m_message);

    if (!spec.label.isEmpty() && spec.label != spec.name)
        setToolTip(QStringLiteral("sysfs label: %1").arg(spec.label));
}

void FanCard::updateStatus(const FanStatus &status, std::optional<double> temp)
{
    m_rpm->setText(status.rpm >= 0 ? QStringLiteral("%1 RPM").arg(status.rpm) : QStringLiteral("-- RPM"));
    m_temp->setText(temp ? QStringLiteral("%1 °C").arg(*temp, 0, 'f', 1) : QStringLiteral("-- °C"));
    reflectMode(status.mode);
    if (m_busy)
        return;

    // 1 is the driver's manual mode: reachable from outside this tool, but the
    // radios cannot express it, so say nothing rather than lie about the state.
    const bool understood = status.mode == ModeAuto || status.mode == ModeFull || status.mode == 1;
    showMessage(understood ? QString()
                           : QStringLiteral("Currently %1, which this tool does not manage.")
                                 .arg(modeName(status.mode)),
                true);
}

void FanCard::reflectMode(int mode)
{
    m_auto->setChecked(mode == ModeAuto);
    m_full->setChecked(mode == ModeFull);
}

void FanCard::setBusy(bool busy)
{
    m_busy = busy;
    m_auto->setEnabled(!busy);
    m_full->setEnabled(!busy);
}

void FanCard::showMessage(const QString &text, bool error)
{
    if (text.isEmpty()) {
        m_message->clear();
        m_message->hide();
        return;
    }
    m_message->setText(text);
    m_message->setStyleSheet(error ? QStringLiteral("color: %1").arg(readableRed(this).name())
                                   : QString());
    m_message->show();
}

}  // namespace fanctl
