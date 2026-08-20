#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace corum {

struct SemanticVersion {
    std::array<unsigned long long, 3> numbers {0, 0, 0};
    std::string prerelease;
};

std::optional<SemanticVersion> parseSemanticVersion(std::string_view value);
int compareSemanticVersions(
    SemanticVersion const& left,
    SemanticVersion const& right
);
bool versionMeetsMinimum(
    std::string_view currentVersion,
    std::string_view minimumVersion
);

} // namespace corum
