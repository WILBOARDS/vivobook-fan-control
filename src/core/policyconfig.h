#pragma once

#include <QString>

namespace fanctl {

// Temperature thresholds for the background policy service. Stored in a
// world-readable key=value file so the GUI and CLI can show it without a
// polkit prompt; only the root helper writes it.
struct PolicyConfig {
    int cpuFullAbove = 85;
    int cpuAutoBelow = 75;
    int gpuFullAbove = 85;
    int gpuAutoBelow = 75;
    int intervalSeconds = 5;

    QString errorText() const;
    bool isValid() const { return errorText().isEmpty(); }
};

QString policyConfigPath();

PolicyConfig loadPolicyConfig();

// Serialises with the current values so unrelated keys are never lost.
QString policyConfigText(const PolicyConfig &config);
bool savePolicyConfig(const PolicyConfig &config, QString *error = nullptr);

// Parses a "key=value" argument such as cpu_full_above=85 into the config.
bool applyPolicyArgument(const QString &argument, PolicyConfig *config, QString *error = nullptr);

}  // namespace fanctl
