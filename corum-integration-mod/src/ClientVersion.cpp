#include "ClientVersion.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <vector>

namespace {

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

bool isNumericIdentifier(std::string_view value) {
    return
        !value.empty() &&
        std::all_of(value.begin(), value.end(), [](char character) {
            return std::isdigit(static_cast<unsigned char>(character));
        });
}

std::vector<std::string_view> splitIdentifiers(std::string_view value) {
    std::vector<std::string_view> result;
    while (true) {
        auto const separator = value.find('.');
        result.push_back(value.substr(0, separator));
        if (separator == std::string_view::npos) break;
        value.remove_prefix(separator + 1);
    }
    return result;
}

int comparePrerelease(std::string const& left, std::string const& right) {
    if (left.empty() && right.empty()) return 0;
    if (left.empty()) return 1;
    if (right.empty()) return -1;

    auto const leftParts = splitIdentifiers(left);
    auto const rightParts = splitIdentifiers(right);
    auto const commonSize = std::min(leftParts.size(), rightParts.size());

    for (std::size_t index = 0; index < commonSize; ++index) {
        auto const leftPart = leftParts[index];
        auto const rightPart = rightParts[index];
        if (leftPart == rightPart) continue;

        auto const leftNumeric = isNumericIdentifier(leftPart);
        auto const rightNumeric = isNumericIdentifier(rightPart);
        if (leftNumeric && rightNumeric) {
            unsigned long long leftNumber = 0;
            unsigned long long rightNumber = 0;
            auto const leftResult = std::from_chars(
                leftPart.data(),
                leftPart.data() + leftPart.size(),
                leftNumber
            );
            auto const rightResult = std::from_chars(
                rightPart.data(),
                rightPart.data() + rightPart.size(),
                rightNumber
            );
            if (
                leftResult.ec == std::errc() &&
                rightResult.ec == std::errc() &&
                leftNumber != rightNumber
            ) {
                return leftNumber < rightNumber ? -1 : 1;
            }
        } else if (leftNumeric != rightNumeric) {
            return leftNumeric ? -1 : 1;
        }

        return leftPart < rightPart ? -1 : 1;
    }

    if (leftParts.size() == rightParts.size()) return 0;
    return leftParts.size() < rightParts.size() ? -1 : 1;
}

} // namespace

namespace corum {

std::optional<SemanticVersion> parseSemanticVersion(std::string_view value) {
    value = trim(value);
    if (!value.empty() && (value.front() == 'v' || value.front() == 'V')) {
        value.remove_prefix(1);
    }
    if (value.empty()) return std::nullopt;

    auto const buildSeparator = value.find('+');
    if (buildSeparator != std::string_view::npos) {
        value = value.substr(0, buildSeparator);
    }

    std::string_view prerelease;
    auto const prereleaseSeparator = value.find('-');
    if (prereleaseSeparator != std::string_view::npos) {
        prerelease = value.substr(prereleaseSeparator + 1);
        value = value.substr(0, prereleaseSeparator);
        if (prerelease.empty()) return std::nullopt;
    }

    SemanticVersion parsed;
    for (std::size_t index = 0; index < parsed.numbers.size(); ++index) {
        auto const separator = value.find('.');
        auto const part = value.substr(0, separator);
        if (!isNumericIdentifier(part)) return std::nullopt;

        unsigned long long number = 0;
        auto const result = std::from_chars(
            part.data(),
            part.data() + part.size(),
            number
        );
        if (result.ec != std::errc() || result.ptr != part.data() + part.size()) {
            return std::nullopt;
        }
        parsed.numbers[index] = number;

        if (separator == std::string_view::npos) {
            if (index != parsed.numbers.size() - 1) return std::nullopt;
            value = {};
            break;
        }
        value.remove_prefix(separator + 1);
    }

    if (!value.empty()) return std::nullopt;
    if (!prerelease.empty()) {
        auto const identifiers = splitIdentifiers(prerelease);
        if (
            std::any_of(identifiers.begin(), identifiers.end(), [](auto identifier) {
                return identifier.empty() ||
                    !std::all_of(identifier.begin(), identifier.end(), [](char character) {
                        return
                            std::isalnum(static_cast<unsigned char>(character)) ||
                            character == '-';
                    });
            })
        ) {
            return std::nullopt;
        }
        parsed.prerelease = std::string(prerelease);
    }
    return parsed;
}

int compareSemanticVersions(
    SemanticVersion const& left,
    SemanticVersion const& right
) {
    for (std::size_t index = 0; index < left.numbers.size(); ++index) {
        if (left.numbers[index] == right.numbers[index]) continue;
        return left.numbers[index] < right.numbers[index] ? -1 : 1;
    }
    return comparePrerelease(left.prerelease, right.prerelease);
}

bool versionMeetsMinimum(
    std::string_view currentVersion,
    std::string_view minimumVersion
) {
    auto const current = parseSemanticVersion(currentVersion);
    auto const minimum = parseSemanticVersion(minimumVersion);
    if (!current || !minimum) return true;
    return compareSemanticVersions(*current, *minimum) >= 0;
}

} // namespace corum
