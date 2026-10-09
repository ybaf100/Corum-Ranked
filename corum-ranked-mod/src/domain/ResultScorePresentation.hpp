#pragma once

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace corum::ranked {

struct StandingTargetView {
    int accountId = 0;
    std::string player;
    bool bot = false;
    int rank = 0, score = 0;
};

struct StandingView {
    std::optional<int> rank;
    int total = 0;
    std::optional<double> topPercent;
    std::optional<StandingTargetView> target;
};

struct TierCutoffView { std::string tier; int minimum = 0; };

struct ScoreSeriesView {
    bool active = false;
    std::string sourceTier, targetTier;
    int cutoff = 0, gamesRequired = 0, winsRequired = 0;
    std::vector<std::string> results;
};

struct ResultScorePlayerView {
    StandingView before, after;
    ScoreSeriesView promotion;
};

struct ResultScoreView {
    bool available = false;
    std::vector<TierCutoffView> bands;
    ResultScorePlayerView playerA, playerB;
};

enum class ScoreMotion { Normal, Promotion, Demotion };

struct ScoreAnimationInput {
    std::string matchId, beforeTier, afterTier;
    int before = 0, after = 0;
    bool won = false, promotionStarted = false, promotionInProgress = false;
    bool promoted = false, promotionFailed = false, demoted = false, demotionWarning = false;
    int previousMarks = 0;
    std::vector<TierCutoffView> bands;
    StandingView standingBefore, standingAfter;
    ScoreSeriesView promotion;

    ScoreMotion motion() const {
        return promoted ? ScoreMotion::Promotion : demoted ? ScoreMotion::Demotion : ScoreMotion::Normal;
    }
    double duration() const { return motion() == ScoreMotion::Normal ? 2.0 : 4.0; }
};

inline bool validScoreBands(std::vector<TierCutoffView> const& bands) {
    if (bands.size() != 5) return false;
    static std::vector<std::string> const tiers {"RED", "AQUA", "BRONZE", "SILVER", "GOLD"};
    for (std::size_t i = 0; i < tiers.size(); ++i) {
        if (bands[i].tier != tiers[i] || (i && bands[i].minimum <= bands[i - 1].minimum)) return false;
    }
    return true;
}

struct ScoreProgressSample {
    int score = 0, minimum = 0, target = 0;
    std::string tier, nextTier;
    double fill = 0.0;
    bool complete = false, goldLeader = false;
    StandingView standing;
};

/** Presentation only: interpolate the exact server values, never calculate LP. */
inline ScoreProgressSample sampleScoreProgress(ScoreAnimationInput const& input, double elapsed) {
    ScoreProgressSample sample;
    auto const fraction = std::clamp(elapsed / input.duration(), 0.0, 1.0);
    sample.complete = fraction >= 1.0;
    // Ease the endpoints and retain sub-LP precision for the bar. Only the
    // displayed number is rounded; a small gain must not move in whole-LP steps.
    auto const eased = fraction * fraction * (3.0 - 2.0 * fraction);
    auto const interpolated = input.before + (static_cast<double>(input.after) - input.before) * eased;
    sample.score = static_cast<int>(std::lround(interpolated));
    sample.tier = input.beforeTier;
    if (input.beforeTier != input.afterTier) {
        auto const afterBand = std::find_if(input.bands.begin(), input.bands.end(), [&](auto const& b) { return b.tier == input.afterTier; });
        auto const beforeBand = std::find_if(input.bands.begin(), input.bands.end(), [&](auto const& b) { return b.tier == input.beforeTier; });
        if (sample.complete || (input.promoted && afterBand != input.bands.end() && interpolated >= afterBand->minimum) ||
            (input.demoted && beforeBand != input.bands.end() && interpolated < beforeBand->minimum)) sample.tier = input.afterTier;
    }
    // GOLD uses the result's fixed target for the entire score reveal. Moving
    // to a newly polled ladder/target on its last frame would jump the bar.
    sample.standing = sample.tier == "GOLD" || sample.complete || sample.tier != input.beforeTier
        ? input.standingAfter : input.standingBefore;
    auto const band = std::find_if(input.bands.begin(), input.bands.end(), [&](auto const& b) { return b.tier == sample.tier; });
    if (band == input.bands.end()) return sample;
    sample.minimum = band->minimum;
    if (std::next(band) != input.bands.end()) {
        sample.target = std::next(band)->minimum; sample.nextTier = std::next(band)->tier;
    } else {
        sample.goldLeader = sample.standing.rank == 1;
        sample.target = sample.standing.target ? sample.standing.target->score : sample.score;
    }
    auto const span = static_cast<double>(sample.target) - sample.minimum;
    sample.fill = sample.goldLeader ? 1.0 : span > 0 ? std::clamp((interpolated - sample.minimum) / span, 0.0, 1.0) : 0.0;
    return sample;
}

// The result owns the foreground until both reveals finish; a Skip callback
// is claimed once before removing the layer or restoring the parent input.
class ResultPresentationGate {
public:
    bool begin(std::string const& matchId) {
        if (matchId.empty() || matchId == m_played) return false;
        m_played = matchId; return true;
    }
    void reset() { m_played.clear(); }
private:
    std::string m_played;
};

} // namespace corum::ranked
