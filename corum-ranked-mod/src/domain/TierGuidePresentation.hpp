#pragma once
#include "RankTierPresentation.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace corum::ranked {

// One server tier band (Solo: RED3..GOLD with LP ranges; Survival: group
// lower bounds only).
struct TierGuideBand {
    std::string tier;
    int minimum = 0;
    std::optional<int> maximumExclusive;
    int promotionGames = 0, promotionWins = 0;
};

// Q3 column: one base tier with its divisions folded together.
struct TierGuideColumn {
    std::string base;
    int divisions = 0;
    int minimum = 0;
    std::optional<int> maximumExclusive;
    int promotionGames = 0, promotionWins = 0;
    bool present = false;
};

// The five Q3 columns in RED -> GOLD order. A base with no band stays
// present=false so the column can show a dash instead of disappearing.
inline std::array<TierGuideColumn, 5> tierGuideColumns(std::vector<TierGuideBand> const& bands) {
    static constexpr std::array<char const*, 5> bases{"RED", "AQUA", "BRONZE", "SILVER", "GOLD"};
    std::array<TierGuideColumn, 5> columns{};
    for (std::size_t i = 0; i < bases.size(); ++i) columns[i].base = bases[i];
    for (auto const& band : bands) {
        auto const base = rankTierBase(band.tier);
        for (auto& column : columns) {
            if (column.base != base) continue;
            if (!column.present || band.minimum < column.minimum) column.minimum = band.minimum;
            if (!column.present) column.maximumExclusive = band.maximumExclusive;
            else if (!band.maximumExclusive) column.maximumExclusive.reset();
            else if (column.maximumExclusive && *band.maximumExclusive > *column.maximumExclusive)
                column.maximumExclusive = band.maximumExclusive;
            if (band.promotionGames > 0 && column.promotionGames == 0) {
                column.promotionGames = band.promotionGames;
                column.promotionWins = band.promotionWins;
            }
            ++column.divisions;
            column.present = true;
        }
    }
    return columns;
}

} // namespace corum::ranked
