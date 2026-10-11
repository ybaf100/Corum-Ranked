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

struct GoldTieState {
    int priority = 1, count = 0;
    StandingTargetView target;
    bool preferMoving = false;
};
struct GoldScoreGroup {
    int score = 0, count = 0;
    StandingTargetView target;
    bool preferMoving = false;
    std::vector<GoldTieState> states {};
};
struct GoldMovingRival {
    std::optional<int> before, after;
    StandingTargetView target;
    int beforePriority = 1, afterPriority = 1;
};
struct GoldScoreTimeline {
    bool available = false;
    int fixedTotal = 0, higherFixed = 0;
    std::vector<GoldScoreGroup> groups;
    std::optional<GoldMovingRival> moving;
    std::vector<StandingTargetView> overtakes {};
    int beforePriority = 1, afterPriority = 1;
};

// The server selects individual avatars, including LP ties and historical bots.
// Legacy results only carry one representative per group. Keep that fallback
// bounded as well; never duplicate an avatar to stand in for an unknown player.
inline std::vector<StandingTargetView> goldOvertakeTargets(GoldScoreTimeline const& timeline, int before, int after) {
    if (after <= before || !timeline.available) return {};
    auto selected = timeline.overtakes;
    if (selected.empty()) {
        for (auto const& group : timeline.groups)
            if (group.score > before && group.score < after) selected.push_back(group.target);
        if (timeline.moving && timeline.moving->before && timeline.moving->after &&
            *timeline.moving->before > before && *timeline.moving->after < after) selected.push_back(timeline.moving->target);
    }
    std::stable_sort(selected.begin(),selected.end(),[](auto const& a,auto const& b) { return a.rank < b.rank; });
    if (selected.size() > 5) selected.resize(5);
    std::reverse(selected.begin(),selected.end());
    return selected;
}

inline StandingTargetView goldOvertakeAt(GoldScoreTimeline const& timeline, StandingTargetView target, double fraction) {
    if (timeline.moving && timeline.moving->target.accountId == target.accountId && timeline.moving->target.bot == target.bot) {
        auto const& moving=*timeline.moving;
        auto const before=moving.before.value_or(target.score),after=moving.after.value_or(before);
        target.score=static_cast<int>(std::lround(before+(static_cast<double>(after)-before)*fraction));
    }
    return target;
}

struct ResultScorePlayerView {
    StandingView before, after;
    ScoreSeriesView promotion;
    GoldScoreTimeline goldTimeline;
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
    GoldScoreTimeline goldTimeline;

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
    bool goldOvertake = false;
};

inline void sampleGoldTimeline(ScoreAnimationInput const& input, double fraction, ScoreProgressSample& sample) {
    auto const& timeline = input.goldTimeline;
    if (!timeline.available) return;
    std::optional<StandingTargetView> moving;
    auto const priority = sample.tier == input.beforeTier && fraction < 1 ? timeline.beforePriority : timeline.afterPriority;
    auto const movingPriority = timeline.moving ? (fraction < 1 ? timeline.moving->beforePriority : timeline.moving->afterPriority) : 1;
    if (timeline.moving) {
        auto const& rival = *timeline.moving;
        auto const present = fraction >= 1 ? rival.after.has_value() : rival.before.has_value();
        if (present) {
            moving = rival.target;
            auto const before = rival.before.value_or(rival.after.value_or(0));
            auto const after = rival.after.value_or(before);
            moving->score = static_cast<int>(std::lround(before + (static_cast<double>(after) - before) * fraction));
        }
    }
    auto const ahead = [](int score, int state, int otherScore, int otherState) {
        return score > otherScore || (score == otherScore && state > otherState);
    };
    auto const rankAt = [&](int score, int state) {
        auto count = timeline.higherFixed;
        for (auto const& group : timeline.groups) {
            if (group.score > score) count += group.count;
            else if (group.score == score) for (auto const& tie : group.states) if (tie.priority > state) count += tie.count;
        }
        if (moving && ahead(moving->score,movingPriority,score,state)) ++count;
        return count + 1;
    };
    sample.standing.rank = rankAt(sample.score,priority);
    sample.standing.total = timeline.fixedTotal + 1 + static_cast<int>(moving.has_value());
    sample.standing.topPercent = 100.0 * *sample.standing.rank / sample.standing.total;
    std::optional<StandingTargetView> nearest, hero;
    bool allPassed = true;
    int heroBefore = -1;
    int nearestPriority = 1, heroPriority = 1;
    auto consider = [&](StandingTargetView const& candidate, int state, bool prefer) {
        if (ahead(candidate.score,state,sample.score,priority) &&
            (!nearest || candidate.score < nearest->score || (candidate.score == nearest->score &&
                (state < nearestPriority || (state == nearestPriority && prefer))))) {
            nearest = candidate; nearestPriority = state;
        }
    };
    for (auto const& group : timeline.groups) {
        if (group.states.empty()) consider(group.target,1,false);
        else for (auto const& tie : group.states) consider(tie.target,tie.priority,false);
        if (input.after > input.before && group.score > input.before && group.score < input.after) {
            allPassed = allPassed && sample.score > group.score;
            if (group.score > heroBefore) { hero = group.target; heroBefore = group.score;
                heroPriority = group.states.empty() ? 1 : group.states.front().priority; }
        }
    }
    if (moving) {
        auto prefer = false;
        for (auto const& group : timeline.groups) if (group.score == moving->score) {
            prefer = group.preferMoving;
            for (auto const& tie : group.states) if (tie.priority == movingPriority) prefer = tie.preferMoving;
        }
        consider(*moving,movingPriority,prefer);
        auto const& rival = *timeline.moving;
        if (input.after > input.before && rival.before && rival.after && *rival.before > input.before && *rival.after < input.after) {
            allPassed = allPassed && sample.score > moving->score;
            if (*rival.before > heroBefore || (*rival.before == heroBefore && prefer)) { hero = moving; heroPriority = movingPriority; }
        }
    }
    // One overtake animation: keep the highest-ranked crossed rival until all
    // crossed scores have been reached, then reveal the next actual target.
    sample.goldOvertake = hero.has_value() && allPassed;
    sample.standing.target = hero && !allPassed ? hero : nearest;
    if (sample.standing.target) {
        auto const targetPriority = hero && !allPassed ? heroPriority : nearestPriority;
        sample.standing.target->rank = rankAt(sample.standing.target->score,targetPriority) +
            (ahead(sample.score,priority,sample.standing.target->score,targetPriority) ? 1 : 0);
    }
    if (fraction >= 1 && input.standingAfter.rank) {
        sample.standing = input.standingAfter;
    }
}

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
    // Older results retain their fixed target. New GOLD timelines retarget
    // within this immutable result, without consulting a newly polled ladder.
    sample.standing = sample.tier == "GOLD" || sample.complete || sample.tier != input.beforeTier
        ? input.standingAfter : input.standingBefore;
    if (sample.tier == "GOLD") sampleGoldTimeline(input,eased,sample);
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
