#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace corum::ranked {
struct LeaderboardEntry {
    std::optional<int> rank, lp;
    int accountId = 0, promotionGames = 0;
    bool bot = false, promotion = false, warning = false;
    std::string player, tier;
    std::vector<std::string> marks;
};

struct LeaderboardColumns {
    float rank, icon, name, nameWidth, status, statusWidth, tierIcon, tierName, tierWidth, value, valueWidth;
};

inline LeaderboardColumns leaderboardColumns(float width) {
    return {24, 64, 90, std::max(28.0f,width - 352), width - 252, 80,
        width - 156, width - 133, 57, width - 68, 60};
}

inline std::string leaderboardValue(LeaderboardEntry const& entry) {
    if (!entry.promotion) return entry.lp ? std::to_string(*entry.lp) + " LP" : "— LP";
    std::string value;
    for (int index = 0; index < std::clamp(entry.promotionGames,1,8); ++index) {
        if (index) value += " ";
        value += static_cast<std::size_t>(index) < entry.marks.size()
            ? entry.marks[index] == "WIN" ? "O" : entry.marks[index] == "LOSS" ? "X" : "_"
            : "_";
    }
    return value;
}

struct BorderPoint { float x, y; };
/** Start at the upper left and travel right, down, left, up at uniform speed. */
inline BorderPoint clockwiseBorderPoint(float width, float height, double phase) {
    width = std::max(0.0f,width); height = std::max(0.0f,height);
    auto distance = (phase - std::floor(phase)) * 2 * (width + height);
    if (distance <= width) return {static_cast<float>(distance),height};
    distance -= width;
    if (distance <= height) return {width,height - static_cast<float>(distance)};
    distance -= height;
    if (distance <= width) return {width - static_cast<float>(distance),0};
    return {0,static_cast<float>(distance - width)};
}
} // namespace corum::ranked
