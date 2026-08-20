#include "EnvironmentPolicy.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <sstream>
#include <string_view>

namespace {

struct VersionIdentifier {
    std::variant<unsigned long long, std::string> value;
};

struct ParsedVersion {
    std::vector<unsigned long long> numbers;
    std::vector<VersionIdentifier> prerelease;
};

bool allDigits(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
}

std::vector<std::string> split(std::string const& value, char delimiter) {
    std::vector<std::string> result;
    std::stringstream stream(value);
    std::string part;
    while (std::getline(stream, part, delimiter)) result.push_back(part);
    return result;
}

std::optional<ParsedVersion> parseVersion(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.erase(text.begin());
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) text.erase(text.begin());

    auto const metadata = text.find('+');
    if (metadata != std::string::npos) text.resize(metadata);
    auto const hyphen = text.find('-');
    auto const core = text.substr(0, hyphen);
    auto const prerelease = hyphen == std::string::npos ? std::string() : text.substr(hyphen + 1);
    if (core.empty()) return std::nullopt;

    ParsedVersion parsed;
    for (auto const& part : split(core, '.')) {
        if (!allDigits(part)) return std::nullopt;
        try {
            parsed.numbers.push_back(std::stoull(part));
        } catch (...) {
            return std::nullopt;
        }
    }
    if (!prerelease.empty()) {
        for (auto const& part : split(prerelease, '.')) {
            if (part.empty()) return std::nullopt;
            if (allDigits(part)) {
                try {
                    parsed.prerelease.push_back({std::stoull(part)});
                } catch (...) {
                    return std::nullopt;
                }
            } else {
                parsed.prerelease.push_back({part});
            }
        }
    }
    return parsed;
}

std::optional<int> compareVersions(std::string const& leftText, std::string const& rightText) {
    auto const left = parseVersion(leftText);
    auto const right = parseVersion(rightText);
    if (!left || !right) return std::nullopt;

    auto const numberCount = std::max(left->numbers.size(), right->numbers.size());
    for (std::size_t index = 0; index < numberCount; ++index) {
        auto const leftPart = index < left->numbers.size() ? left->numbers[index] : 0;
        auto const rightPart = index < right->numbers.size() ? right->numbers[index] : 0;
        if (leftPart != rightPart) return leftPart > rightPart ? 1 : -1;
    }
    if (left->prerelease.empty() != right->prerelease.empty()) {
        return left->prerelease.empty() ? 1 : -1;
    }
    auto const prereleaseCount = std::max(left->prerelease.size(), right->prerelease.size());
    for (std::size_t index = 0; index < prereleaseCount; ++index) {
        if (index >= left->prerelease.size()) return -1;
        if (index >= right->prerelease.size()) return 1;
        auto const& leftPart = left->prerelease[index].value;
        auto const& rightPart = right->prerelease[index].value;
        if (leftPart == rightPart) continue;
        if (leftPart.index() != rightPart.index()) {
            return std::holds_alternative<unsigned long long>(leftPart) ? -1 : 1;
        }
        if (auto const leftNumber = std::get_if<unsigned long long>(&leftPart)) {
            return *leftNumber > std::get<unsigned long long>(rightPart) ? 1 : -1;
        }
        return std::get<std::string>(leftPart) > std::get<std::string>(rightPart) ? 1 : -1;
    }
    return 0;
}

bool settingValuesEqual(
    corum::ranked::SettingValue const& left,
    corum::ranked::SettingValue const& right
) {
    if (left.index() != right.index()) return false;
    if (auto const leftNumber = std::get_if<double>(&left)) {
        return std::abs(*leftNumber - std::get<double>(right)) < 0.000001;
    }
    return left == right;
}

void sortAndUnique(std::vector<std::string>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

} // namespace

namespace corum::ranked {

EnvironmentDecision evaluateEnvironment(
    std::vector<InstalledModSnapshot> const& installedMods,
    EnvironmentPolicy const& policy
) {
    EnvironmentDecision decision;
    std::map<std::string, AllowedModRule const*> allowedById;
    std::map<std::string, InstalledModSnapshot const*> installedById;

    for (auto const& rule : policy.allowedMods) {
        if (!rule.enabled || rule.id.empty()) continue;
        allowedById[rule.id] = &rule;
        decision.allowedModIds.push_back(rule.id);
    }
    for (auto const& mod : installedMods) {
        if (mod.internal || mod.system || mod.id.empty()) continue;
        installedById[mod.id] = &mod;
        if (!allowedById.contains(mod.id)) decision.unauthorizedModIds.push_back(mod.id);
    }

    for (auto const& [id, rule] : allowedById) {
        if (rule->required && !installedById.contains(id)) {
            decision.missingRequiredModIds.push_back(id);
        }
    }
    for (auto const& [id, mod] : installedById) {
        auto const ruleIt = allowedById.find(id);
        if (ruleIt == allowedById.end()) continue;
        auto const& rule = *ruleIt->second;
        if (rule.minVersion) {
            auto const comparison = compareVersions(mod->version, *rule.minVersion);
            if (!comparison || *comparison < 0) {
                decision.versionViolations.push_back(
                    id + ": installed " + mod->version + ", minimum " + *rule.minVersion
                );
            }
        }
        if (rule.maxVersion) {
            auto const comparison = compareVersions(mod->version, *rule.maxVersion);
            if (!comparison || *comparison > 0) {
                decision.versionViolations.push_back(
                    id + ": installed " + mod->version + ", maximum " + *rule.maxVersion
                );
            }
        }
    }

    auto const cbfIt = installedById.find(policy.cbfModId);
    if (cbfIt == installedById.end()) {
        decision.cbfIssues.push_back("CBF_NOT_INSTALLED");
    } else {
        auto const& cbf = *cbfIt->second;
        if (!cbf.enabled || !cbf.loaded) decision.cbfIssues.push_back("CBF_NOT_ACTIVE");
        for (auto const& [key, requiredValue] : policy.cbfRequiredSettings) {
            auto const actual = cbf.settings.find(key);
            if (actual == cbf.settings.end() || !settingValuesEqual(actual->second, requiredValue)) {
                decision.cbfIssues.push_back("CBF_SETTING_MISMATCH:" + key);
            }
        }
    }

    sortAndUnique(decision.allowedModIds);
    sortAndUnique(decision.unauthorizedModIds);
    sortAndUnique(decision.missingRequiredModIds);
    sortAndUnique(decision.versionViolations);
    sortAndUnique(decision.cbfIssues);
    decision.allowed =
        decision.unauthorizedModIds.empty() &&
        decision.missingRequiredModIds.empty() &&
        decision.versionViolations.empty() &&
        decision.cbfIssues.empty();
    return decision;
}

bool isAcceptableServerURL(std::string const& url) {
    if (url.empty() || url.ends_with('/')) return false;
    if (url.starts_with("https://")) return url.size() > 8;
    return
        url.starts_with("http://localhost:") ||
        url.starts_with("http://127.0.0.1:") ||
        url.starts_with("http://[::1]:");
}

} // namespace corum::ranked

