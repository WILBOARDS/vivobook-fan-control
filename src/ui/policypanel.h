#pragma once

#include <QGroupBox>

class QCheckBox;
class QLabel;
class QSpinBox;
class QPushButton;

namespace fanctl {

// Thresholds for the root policy daemon. Reading them needs no privileges;
// writing and toggling the service go through the polkit helper.
class PolicyPanel : public QGroupBox {
    Q_OBJECT

public:
    explicit PolicyPanel(QWidget *parent = nullptr);

    void reload();

signals:
    void statusMessage(const QString &text, bool error);

private:
    void applyThresholds();
    void toggleService(bool enable);

    QCheckBox *m_enabled = nullptr;
    QSpinBox *m_cpuFull = nullptr;
    QSpinBox *m_cpuRelease = nullptr;
    QSpinBox *m_gpuFull = nullptr;
    QSpinBox *m_gpuRelease = nullptr;
    QSpinBox *m_interval = nullptr;
    QPushButton *m_apply = nullptr;
    QLabel *m_note = nullptr;
    bool m_syncing = false;
};

}  // namespace fanctl
