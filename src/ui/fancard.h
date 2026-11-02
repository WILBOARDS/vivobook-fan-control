#pragma once

#include <QGroupBox>

#include <optional>

#include "core/sensors.h"

class QLabel;
class QRadioButton;

namespace fanctl {

class FanCard : public QGroupBox {
    Q_OBJECT

public:
    explicit FanCard(const FanSpec &spec, QWidget *parent = nullptr);

    void updateStatus(const FanStatus &status, std::optional<double> temp);
    void setBusy(bool busy);
    void showMessage(const QString &text, bool error = false);
    void reflectMode(int mode);

    QString fanName() const { return m_spec.name; }

signals:
    void modeRequested(const QString &fan, ModeValue mode);

private:
    FanSpec m_spec;
    QLabel *m_rpm = nullptr;
    QLabel *m_temp = nullptr;
    QLabel *m_message = nullptr;
    QRadioButton *m_auto = nullptr;
    QRadioButton *m_full = nullptr;
    bool m_busy = false;
};

}  // namespace fanctl
