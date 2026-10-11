#pragma once

#include <algorithm>
#include <array>
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
    auto distance = (phase - std::floor(phase)) * 2.0 * (static_cast<double>(width) + height);
    if (distance <= width) return {static_cast<float>(distance),height};
    distance -= width;
    if (distance <= height) return {width,height - static_cast<float>(distance)};
    distance -= height;
    if (distance <= width) return {width - static_cast<float>(distance),0};
    return {0,static_cast<float>(distance - width)};
}

struct BorderSegment { BorderPoint from, to; };
struct BorderStroke {
    // A stroke of at most one lap crosses at most four corners, including wrap.
    std::array<BorderSegment,5> segments{};
    std::size_t count = 0;
};

/** Split a short clockwise stroke at every corner instead of cutting diagonally. */
inline BorderStroke clockwiseBorderStroke(float width, float height, double phase, float length) {
    BorderStroke stroke;
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(phase)
        || !std::isfinite(length) || width <= 0 || height <= 0 || length <= 0) return stroke;
    auto const perimeter = 2.0 * (static_cast<double>(width) + height);
    auto distance = (phase - std::floor(phase)) * perimeter;
    auto remaining = std::min(static_cast<double>(length),perimeter);
    while (remaining > 0 && stroke.count < stroke.segments.size()) {
        auto const corner = distance < width ? static_cast<double>(width)
            : distance < width + static_cast<double>(height) ? width + static_cast<double>(height)
            : distance < 2.0 * width + height ? 2.0 * width + height : perimeter;
        auto const amount = std::min(remaining,corner - distance);
        if (amount <= 0) break;
        auto const from = clockwiseBorderPoint(width,height,distance / perimeter);
        auto const to = clockwiseBorderPoint(width,height,(distance + amount) / perimeter);
        // Native segment tessellation must never receive a zero-length edge.
        if (from.x != to.x || from.y != to.y) stroke.segments[stroke.count++] = {from,to};
        remaining -= amount;
        distance += amount;
        if (distance >= perimeter) distance = 0;
    }
    return stroke;
}
} // namespace corum::ranked
