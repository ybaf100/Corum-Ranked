#pragma once

namespace corum::ranked {

enum class RankedPracticeTransition {
    None,
    Enter,
    LeaveForFreshAttempt,
    LeaveAndExit,
};

struct RankedPracticeActions {
    bool endNormalAttempt = false;
    bool suppressScoring = false;
    bool resetForFreshAttempt = false;
    bool exitLocalGameplay = false;
    bool affectOpponent = false;
};

struct RankedGameplayPolicyInput {
    bool rankedLevel = false;
    bool testMode = false;
    bool rankedPractice = false;
    bool requestedPractice = false;
    bool canStartFreshAttempt = false;
};

struct RankedResetActions {
    bool endAttempt = false;
    bool restartInPlace = false;
    bool startFreshAttempt = false;
    bool exitLocalGameplay = false;
};

[[nodiscard]] constexpr bool rankedScoringEnabled(
    bool rankedLevel,
    bool testMode,
    bool rankedPractice,
    bool gamePracticeMode
) {
    return rankedLevel && !testMode && !rankedPractice && !gamePracticeMode;
}

[[nodiscard]] constexpr RankedPracticeTransition practiceTransition(
    RankedGameplayPolicyInput const& input
) {
    if (!input.rankedLevel || input.testMode) return RankedPracticeTransition::None;
    if (input.requestedPractice && !input.rankedPractice) {
        return RankedPracticeTransition::Enter;
    }
    if (!input.requestedPractice && input.rankedPractice) {
        return input.canStartFreshAttempt
            ? RankedPracticeTransition::LeaveForFreshAttempt
            : RankedPracticeTransition::LeaveAndExit;
    }
    return RankedPracticeTransition::None;
}

[[nodiscard]] constexpr RankedPracticeActions practiceActions(
    RankedPracticeTransition transition
) {
    switch (transition) {
        case RankedPracticeTransition::Enter:
            return {
                .endNormalAttempt = true,
                .suppressScoring = true,
            };
        case RankedPracticeTransition::LeaveForFreshAttempt:
            return {
                .resetForFreshAttempt = true,
            };
        case RankedPracticeTransition::LeaveAndExit:
            return {
                .suppressScoring = true,
                .exitLocalGameplay = true,
            };
        case RankedPracticeTransition::None:
            return {};
    }
    return {};
}

[[nodiscard]] constexpr RankedResetActions rankedResetActions(
    bool scoringEnabled,
    bool attemptEndReported,
    bool canStartFreshAttempt
) {
    if (!scoringEnabled) return {};
    return {
        .endAttempt = !attemptEndReported,
        .restartInPlace = canStartFreshAttempt,
        .startFreshAttempt = canStartFreshAttempt,
        .exitLocalGameplay = !canStartFreshAttempt,
    };
}

} // namespace corum::ranked
