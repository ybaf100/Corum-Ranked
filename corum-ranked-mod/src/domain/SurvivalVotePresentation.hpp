#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace corum::ranked {

// MATCH FOUND holds in the queue window before the roster intro opens.
inline constexpr int SurvivalMatchFoundHoldMs = 1000;

// Hidden match-wide difficulty preference held during the Survival intro.
// The client never learns who voted what or the tally: only how many voted.
enum class SurvivalVoteStage { Hidden, Choosing, Chosen, Closed };

inline SurvivalVoteStage survivalVoteStage(bool present, bool resolved, bool introGate,
    std::string_view myBallot, std::optional<std::int64_t> opensInMs) {
    if (!present || !introGate) return SurvivalVoteStage::Hidden;
    if (resolved) return SurvivalVoteStage::Closed;
    if (!opensInMs || *opensInMs > 0) return SurvivalVoteStage::Hidden;
    return myBallot.empty() ? SurvivalVoteStage::Choosing : SurvivalVoteStage::Chosen;
}

struct SurvivalVoteCard {
    char const* ballot;
    char const* titleKey;
    char const* titleFallback;
    char const* descKey;
    char const* descFallback;
    char const* face;
    int nativeDifficulty; // GJDifficultySprite: Easy, Hard, Demon and N/A faces.
};
inline constexpr std::array<SurvivalVoteCard, 4> SurvivalVoteCards {{
    {"EASY", "survival-vote-easy", "EASY", "survival-vote-easy-desc", "Vote for an easier match overall.", "EASY", 1},
    {"NORMAL", "survival-vote-normal", "NORMAL", "survival-vote-normal-desc", "Vote for a moderate match overall.", "NORMAL", 3},
    {"HARD", "survival-vote-hard", "HARD", "survival-vote-hard-desc", "Vote for a harder match overall.", "HARD", 6},
    {"NONE", "survival-vote-none", "NO PREFERENCE", "survival-vote-none-desc",
        "No preference. A random hidden vote goes to one of the other three.", "N/A", 0},
}};

inline int survivalVoteCardIndex(std::string_view ballot) {
    for (std::size_t i = 0; i < SurvivalVoteCards.size(); ++i)
        if (ballot == SurvivalVoteCards[i].ballot) return static_cast<int>(i);
    return -1;
}

// Card entry: fade in while rising, staggered left to right (0.09 s apart).
inline float survivalVoteCardEntry(float elapsedSeconds, std::size_t index) {
    auto const t = std::clamp((elapsedSeconds - 0.09f * static_cast<float>(index)) / 0.42f, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

// A pick waits this long before it is sent; tapping another card changes it.
inline constexpr float SurvivalVoteConfirmSeconds = 1.0f;
// The timer turns red for the last seconds of the window.
inline constexpr int SurvivalVoteUrgentSeconds = 3;

// Whole seconds left in the window, never negative.
inline int survivalVoteSecondsLeft(std::optional<std::int64_t> closesInMs) {
    if (!closesInMs) return 0;
    return static_cast<int>(std::max<std::int64_t>(0, (*closesInMs + 999) / 1000));
}

} // namespace corum::ranked
