#pragma once
#include <cmath>
#include <optional>
#include <string>

namespace corum::ranked {
struct MapMetadata {
    std::string kind = "UNKNOWN";
    std::optional<int> stars, gddlTier, officialLevelId;
    std::string gddlColor;
    std::optional<double> durationSeconds;
    std::optional<int> featureScore,epic;
    std::optional<int> lengthCategory;
    bool operator==(MapMetadata const&) const = default;
};
inline std::string mapLengthText(std::optional<double> seconds,std::optional<int> category=std::nullopt) {
    if (!seconds || !std::isfinite(*seconds) || *seconds <= 0 || *seconds > 3600 * 24 * 30) {
        static char const* const labels[]{"TINY","SHORT","MEDIUM","LONG","XL"};
        return category && *category>=0 && *category<5?labels[*category]:"UNKNOWN LENGTH";
    }
    auto const duration = static_cast<int>(std::lround(*seconds));
    return std::to_string(duration / 60) + "m " + std::to_string(duration % 60) + "s";
}
inline std::string mapLengthText(MapMetadata const& metadata) {return mapLengthText(metadata.durationSeconds,metadata.lengthCategory);}
inline int officialStarsFromDifficulty(int average) {
    switch (average) { case 1: return 2; case 2: return 3; case 3: return 4;
        case 4: return 6; case 5: return 8; default: return 0; }
}
} // namespace corum::ranked
