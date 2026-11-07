#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace corum::ranked {

// One active Solo map with its pool-wide statistics, as served by
// GET /api/ranked/solo/map-pool. Rates are percentages with one decimal;
// a missing rate means the denominator is still zero.
struct MapPoolRow {
    // difficulty is the sheet's Corum rating text; gdDifficulty the GD label.
    std::string canonicalLevelId, levelId, title, creator, difficulty, gdDifficulty, kind = "UNKNOWN", gddlColor;
    std::optional<double> rating;
    int difficultyRank = 99, pool = 1;
    double qualifyingPercent = 0;
    std::optional<int> stars, gddlTier;
    std::optional<double> durationSeconds;
    int rounds = 0, draws = 0, banOffers = 0, bans = 0;
    std::optional<double> share, drawRate, banRate;
};

// Difficulty (easiest first) is the default order.
enum class MapPoolSort { Difficulty, Share, Draw, Ban, Rounds };
inline constexpr std::array<MapPoolSort, 5> kMapPoolSorts{
    MapPoolSort::Difficulty, MapPoolSort::Share, MapPoolSort::Draw, MapPoolSort::Ban, MapPoolSort::Rounds};

inline bool mapPoolEasierFirst(MapPoolRow const& a, MapPoolRow const& b) {
    if (a.difficultyRank != b.difficultyRank) return a.difficultyRank < b.difficultyRank;
    if (a.gddlTier.value_or(0) != b.gddlTier.value_or(0)) return a.gddlTier.value_or(0) < b.gddlTier.value_or(0);
    if (a.rating.value_or(0) != b.rating.value_or(0)) return a.rating.value_or(0) < b.rating.value_or(0);
    if (a.pool != b.pool) return a.pool < b.pool;
    if (a.title != b.title) return a.title < b.title;
    return a.canonicalLevelId < b.canonicalLevelId;
}

inline double mapPoolSortValue(MapPoolRow const& row, MapPoolSort sort) {
    // Rates without data sort below every real value, including 0%.
    switch (sort) {
        case MapPoolSort::Share: return row.share.value_or(-1);
        case MapPoolSort::Draw: return row.drawRate.value_or(-1);
        case MapPoolSort::Ban: return row.banRate.value_or(-1);
        case MapPoolSort::Rounds: return row.rounds;
        case MapPoolSort::Difficulty: break;
    }
    return 0;
}

// Pool filter tabs: ALL then P1..P6. About five and a half fit the strip;
// the rest scroll in from the right.
inline constexpr int kMapPoolTabs = 7;
inline constexpr float kMapPoolVisibleTabs = 5.5f;
inline float mapPoolTabStripWidth(float tabWidth, float gap) {
    return kMapPoolVisibleTabs * tabWidth + (kMapPoolVisibleTabs - 0.5f) * gap;
}
inline float mapPoolTabScroll(float offset, float contentWidth, float viewWidth) {
    return std::clamp(offset, std::min(0.0f, viewWidth - contentWidth), 0.0f);
}

// Difficulty and the GD face codes of MapDifficulty::nativeCode, in
// difficulty order (Auto ... Extreme Demon); 99 is unknown.
inline int mapPoolRankFromNative(int nativeCode) {
    switch (nativeCode) {
        case -1: return 0;
        case 1: case 2: case 3: case 4: case 5: return nativeCode;
        case 7: return 6;   // Easy Demon
        case 8: return 7;   // Medium Demon
        case 6: return 8;   // Hard Demon
        case 9: return 9;   // Insane Demon
        case 10: return 10; // Extreme Demon
        default: return 99;
    }
}

// Difficulty starts easiest first; the counts start with the most.
inline bool mapPoolDefaultAscending(MapPoolSort sort) { return sort == MapPoolSort::Difficulty; }

// "a before b" in ascending order. Difficulty groups by Pool first, then
// orders by difficulty inside each Pool.
inline bool mapPoolAscendingLess(MapPoolRow const& a, MapPoolRow const& b, MapPoolSort sort) {
    if (sort == MapPoolSort::Difficulty) {
        if (a.pool != b.pool) return a.pool < b.pool;
        return mapPoolEasierFirst(a, b);
    }
    auto const va = mapPoolSortValue(a, sort), vb = mapPoolSortValue(b, sort);
    if (va != vb) return va < vb;
    return mapPoolEasierFirst(a, b);
}

// Indexes of the rows to show: `pool` 0 shows every pool.
inline std::vector<std::size_t> mapPoolOrder(std::vector<MapPoolRow> const& rows, int pool, MapPoolSort sort, bool ascending) {
    std::vector<std::size_t> order;
    for (std::size_t index = 0; index < rows.size(); ++index)
        if (pool == 0 || rows[index].pool == pool) order.push_back(index);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        auto const& a = rows[left];
        auto const& b = rows[right];
        // Difficulty reverses completely (Pool 6 hardest first). A count or
        // rate reverses only its value: ties stay easiest first.
        if (sort == MapPoolSort::Difficulty) return ascending ? mapPoolAscendingLess(a, b, sort) : mapPoolAscendingLess(b, a, sort);
        auto const va = mapPoolSortValue(a, sort), vb = mapPoolSortValue(b, sort);
        if (va != vb) return ascending ? va < vb : va > vb;
        return mapPoolEasierFirst(a, b);
    });
    return order;
}
inline std::vector<std::size_t> mapPoolOrder(std::vector<MapPoolRow> const& rows, int pool, MapPoolSort sort) {
    return mapPoolOrder(rows, pool, sort, mapPoolDefaultAscending(sort));
}

// Byte length of `text` cut to at most `keep` UTF-8 characters.
inline std::size_t mapPoolUtf8Prefix(std::string const& text, std::size_t keep) {
    std::size_t bytes = 0, characters = 0;
    while (bytes < text.size() && characters < keep) {
        auto const lead = static_cast<unsigned char>(text[bytes]);
        bytes += lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        ++characters;
    }
    return std::min(bytes, text.size());
}

// "12.4%", "50%", or "-" while there is no data.
inline std::string mapPoolPercent(std::optional<double> value) {
    if (!value || !std::isfinite(*value)) return "-";
    auto const tenths = static_cast<long long>(std::llround(*value * 10));
    auto text = std::to_string(tenths / 10);
    if (tenths % 10 != 0) text += "." + std::to_string(std::llabs(tenths % 10));
    return text + "%";
}

inline std::string mapPoolNumber(long long value) {
    auto digits = std::to_string(value < 0 ? -value : value);
    for (auto at = static_cast<long long>(digits.size()) - 3; at > 0; at -= 3) digits.insert(static_cast<std::size_t>(at), ",");
    return value < 0 ? "-" + digits : digits;
}

// Level length or Death Match duration: "1:24", "12:05".
inline std::string mapPoolClock(std::optional<double> seconds) {
    if (!seconds || !std::isfinite(*seconds) || *seconds < 0) return "-";
    auto const total = static_cast<long long>(std::llround(*seconds));
    auto const rest = total % 60;
    return std::to_string(total / 60) + ":" + (rest < 10 ? "0" : "") + std::to_string(rest);
}

// One decimal without a trailing ".0": average attempts per round.
inline std::string mapPoolDecimal(std::optional<double> value) {
    auto text = mapPoolPercent(value);
    if (!text.empty() && text.back() == '%') text.pop_back();
    return text;
}

// Index of the deadliest 10% progress bin, or -1 before any death.
inline int mapPoolDeathPeak(std::vector<double> const& bins) {
    int peak = -1;
    double best = 0;
    for (std::size_t index = 0; index < bins.size(); ++index)
        if (bins[index] > best) { best = bins[index]; peak = static_cast<int>(index); }
    return peak;
}

// Whole minutes since the list was received; 0 shows "just now".
inline int mapPoolUpdatedMinutes(double elapsedSeconds) {
    return elapsedSeconds > 0 && std::isfinite(elapsedSeconds) ? static_cast<int>(elapsedSeconds / 60) : 0;
}

} // namespace corum::ranked
