#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace corum::ranked {

enum class ClearCheckColor {
    Gray,
    Green,
};

struct HudLayout {
    float topLeftX = 0.0f;
    float topRightX = 0.0f;
    float topY = 0.0f;
    float bottomLeftX = 0.0f;
    float bottomRightX = 0.0f;
    float bottomY = 0.0f;
    float spectatorCenterX = 0.0f;
    float spectatorCenterY = 0.0f;
};

struct HudInput {
    std::string side;
    std::string state;
    std::string banner;
    int roundNumber = 0;
    double scoreA = 0.0;
    double scoreB = 0.0;
    int clearsA = 0;
    int clearsB = 0;
    bool deathmatch = false;
    int deathmatchSequence = 0;
    int deathmatchAttemptsUsedA = 0;
    int deathmatchAttemptsUsedB = 0;
    double qualifyingPercent = 100.0;
    std::optional<std::int64_t> remainingMillis;
    std::optional<std::int64_t> signedRemainingMillis;
    std::optional<double> renderFps;
    bool spectatorActive = false;
    std::string spectatorOpponentName;
    std::optional<int> spectatorCurrentProgress;
    bool opponentProgressEnabled = false;
    std::optional<int> opponentCurrentProgress;
    bool localPractice = false;
};

struct HudPresentation {
    std::string fpsText;
    std::string roundText;
    std::string ownScoreText;
    std::string opponentScoreText;
    std::array<ClearCheckColor, 3> ownChecks;
    std::array<ClearCheckColor, 3> opponentChecks;
    std::string timerText;
    std::string stateText;
    bool windowStateFirst = false;
    bool deathmatch = false;
    std::string ownAttemptText;
    std::string opponentAttemptText;
    std::string qualifyingText;
    bool spectatorVisible = false;
    std::string spectatorOpponentText;
    std::string spectatorProgressText;
    bool opponentProgressVisible = false;
    std::string opponentProgressText;
    bool practiceIndicatorVisible = false;
};

[[nodiscard]] std::array<ClearCheckColor, 3> clearChecks(int approvedClears);
[[nodiscard]] HudLayout layoutHud(float width, float height);
[[nodiscard]] HudPresentation presentHud(HudInput const& input);

} // namespace corum::ranked
