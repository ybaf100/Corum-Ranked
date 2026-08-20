#pragma once

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace corum::ranked {

using SettingValue = std::variant<bool, double, std::string>;

struct InstalledModSnapshot {
    std::string id;
    std::string version;
    bool enabled = false;
    bool loaded = false;
    bool internal = false;
    bool system = false;
    std::map<std::string, SettingValue> settings;
};

struct AllowedModRule {
    std::string id;
    std::string displayName;
    std::optional<std::string> minVersion;
    std::optional<std::string> maxVersion;
    bool required = false;
    bool enabled = false;
};

struct EnvironmentPolicy {
    std::vector<AllowedModRule> allowedMods;
    std::string cbfModId;
    std::map<std::string, SettingValue> cbfRequiredSettings;
};

struct EnvironmentDecision {
    bool allowed = false;
    std::vector<std::string> unauthorizedModIds;
    std::vector<std::string> allowedModIds;
    std::vector<std::string> missingRequiredModIds;
    std::vector<std::string> versionViolations;
    std::vector<std::string> cbfIssues;
};

EnvironmentDecision evaluateEnvironment(
    std::vector<InstalledModSnapshot> const& installedMods,
    EnvironmentPolicy const& policy
);

bool isAcceptableServerURL(std::string const& url);

} // namespace corum::ranked

