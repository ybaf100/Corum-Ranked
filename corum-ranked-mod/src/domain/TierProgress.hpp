#pragma once

#include "ResultScorePresentation.hpp"
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace corum::ranked {

// Lobby tier bar under the matchmaking start button: how far the player is
// from the next tier, or (GOLD ladder) from the player directly above.
enum class TierProgressKind {
    Hidden,     // no score or bands yet
    Progress,   // within a tier band, next band known
    Near,       // within 10% of the next band
    Promotion,  // promotion series running
    Warning,    // demotion warning (bar still shows the band)
    RankChase,  // GOLD ladder: the target is the player one rank above
    Leader,     // GOLD ladder #1: full rainbow bar
    Top,        // highest band, no ladder data
};

struct TierProgressInput {
    std::string tier;
    int score = 0;
    bool scoreKnown = false;
    std::vector<TierCutoffView> bands; // ascending minimum
    bool promotionActive = false;
    int promotionGames = 0;
    std::vector<std::string> promotionResults;
    bool demotionWarning = false;
    std::optional<int> rank;
    std::optional<StandingTargetView> target; // the player directly above
    // The ladder tier: GOLD and above in Solo, the last band in Survival.
    bool ladderTier = false;
};

struct TierProgressView {
    TierProgressKind kind = TierProgressKind::Hidden;
    std::string tier, nextTier;
    int minimum = 0, maximum = 0, score = 0, remaining = 0;
    float fill = 0.0f;
    int targetRank = 0;
    int promotionGames = 0;
    std::vector<std::string> promotionResults;
};

inline TierProgressView tierProgress(TierProgressInput const& input) {
    TierProgressView view;
    if (!input.scoreKnown || input.tier.empty()) return view;
    view.tier = input.tier;view.score = input.score;
    if (input.promotionActive) {
        view.kind = TierProgressKind::Promotion;view.fill = 1.0f;
        view.promotionGames = std::max(1, input.promotionGames);view.promotionResults = input.promotionResults;
        return view;
    }
    // Solo ladder titles (MASTER, CHAMPION, ...) sit on the GOLD band.
    auto band = std::find_if(input.bands.begin(), input.bands.end(), [&](auto const& b) { return b.tier == input.tier; });
    if (band == input.bands.end() && input.ladderTier)
        band = std::find_if(input.bands.begin(), input.bands.end(), [&](auto const& b) { return b.tier == "GOLD"; });
    view.minimum = band != input.bands.end() ? band->minimum : 0;
    if (input.ladderTier) {
        if (input.rank && *input.rank == 1) { view.kind = TierProgressKind::Leader;view.fill = 1.0f;view.targetRank = 1;return view; }
        if (input.target && input.target->rank > 0) {
            view.kind = TierProgressKind::RankChase;view.targetRank = input.target->rank;
            view.maximum = std::max(input.target->score, view.minimum + 1);
            // Passing the player above needs at least one point more than a tie.
            view.remaining = std::max(1, input.target->score - input.score);
            auto const span = static_cast<float>(view.maximum - view.minimum);
            view.fill = std::clamp(static_cast<float>(input.score - view.minimum) / span, 0.0f, 1.0f);
            return view;
        }
        view.kind = TierProgressKind::Top;view.fill = 1.0f;return view;
    }
    if (band == input.bands.end() || std::next(band) == input.bands.end()) {
        view.kind = TierProgressKind::Top;view.fill = 1.0f;return view;
    }
    view.nextTier = std::next(band)->tier;view.maximum = std::next(band)->minimum;
    auto const span = std::max(1, view.maximum - view.minimum);
    view.remaining = std::max(0, view.maximum - input.score);
    view.fill = std::clamp(static_cast<float>(input.score - view.minimum) / static_cast<float>(span), 0.0f, 1.0f);
    view.kind = input.demotionWarning ? TierProgressKind::Warning
        : view.remaining * 10 <= span ? TierProgressKind::Near : TierProgressKind::Progress;
    return view;
}

} // namespace corum::ranked
