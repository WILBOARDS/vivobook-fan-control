#pragma once

#include <QFrame>

class QCheckBox;
class QComboBox;

namespace fanctl {

class PolicyPanel;

// Everything you set once and then leave alone, so the overview page can stay
// down to the numbers you actually watch.
class SettingsPage : public QFrame {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);

    // Re-reads the systemd units and the config file, which may have changed
    // since the page was built or from the command line.
    void refresh();

signals:
    void statusMessage(const QString &text, bool error);
    // The palette changed, so anything drawn from the old colours needs a redo.
    void themeChanged();

private:
    void applyBootRestore(bool enable);
    void applyAutostart(bool enable);

    QCheckBox *m_bootCheck = nullptr;
    QCheckBox *m_autostartCheck = nullptr;
    QComboBox *m_theme = nullptr;
    PolicyPanel *m_policy = nullptr;
    bool m_syncingBoot = false;
    bool m_syncingAutostart = false;
};

}  // namespace fanctl
