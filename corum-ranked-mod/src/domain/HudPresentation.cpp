#include "HudPresentation.hpp"
#include "ScoreDisplay.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace {

std::string formatNumber(double value,bool deathmatch=false) {
    return corum::ranked::rankedScoreText(value,deathmatch);
}

std::int64_t nonNegative(std::optional<std::int64_t> value) {
    return std::max<std::int64_t>(0, value.value_or(0));
}

std::string normalCountdown(std::optional<std::int64_t> remainingMillis) {
    if (!remainingMillis) return {};
    auto const totalSeconds = (nonNegative(remainingMillis) + 999) / 1'000;
    auto const minutes = totalSeconds / 60;
    auto const seconds = totalSeconds % 60;
    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(2) << minutes << ':'
           << std::setw(2) << seconds;
    return stream.str();
}

std::string windowCountdown(std::optional<std::int64_t> remainingMillis) {
    if (!remainingMillis) return "-";
    auto const tenths = (nonNegative(remainingMillis) + 99) / 100;
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
}

} // namespace

namespace corum::ranked {

std::array<ClearCheckColor, 3> clearChecks(int approvedClears) {
    auto const count = std::clamp(approvedClears, 0, 3);
    return {
        count >= 1 ? ClearCheckColor::Green : ClearCheckColor::Gray,
        count >= 2 ? ClearCheckColor::Green : ClearCheckColor::Gray,
        count >= 3 ? ClearCheckColor::Green : ClearCheckColor::Gray,
    };
}

HudLayout layoutHud(float width, float height) {
    return {
        .topLeftX = 42.0f,
        .topRightX = width - 42.0f,
        .topY = height - 7.0f,
        .bottomLeftX = 10.0f,
        .bottomRightX = width - 10.0f,
        .bottomY = 8.0f,
        .spectatorCenterX = width / 2.0f,
        .spectatorCenterY = height * 0.58f,
    };
}

double deathmatchIconProgress(
    double committedBest,
    double serverDisplay,
    std::optional<double> validLiveProgress
) {
    auto result = std::max(
        std::clamp(committedBest, 0.0, 100.0),
        std::clamp(serverDisplay, 0.0, 100.0)
    );
    if (validLiveProgress && std::isfinite(*validLiveProgress)) {
        result = std::max(result, std::clamp(*validLiveProgress, 0.0, 100.0));
    }
    return result;
}

HudPresentation presentHud(HudInput const& input) {
    auto const ownIsA = input.side == "A";
    auto const ownScore = ownIsA ? input.scoreA : input.scoreB;
    auto const opponentScore = ownIsA ? input.scoreB : input.scoreA;
    auto const ownClears = ownIsA ? input.clearsA : input.clearsB;
    auto const opponentClears = ownIsA ? input.clearsB : input.clearsA;
    auto const ownCompletedAttempts = ownIsA
        ? input.deathmatchAttemptsCompletedA
        : input.deathmatchAttemptsCompletedB;

    // A poll can still say ROUND_PLAYING for a fraction of a second after the
    // five-minute deadline. The normal deadline is also the start of the fixed
    // 10-second FINAL ATTEMPT window, so derive that presentation locally from
    // the signed server clock instead of briefly displaying a misleading 00:00.
    auto displayState = input.state;
    auto displayRemainingMillis = input.remainingMillis;
    if (
        displayState == "ROUND_PLAYING" && input.signedRemainingMillis &&
        *input.signedRemainingMillis <= 0
    ) {
        displayState = "FINAL_ATTEMPT_WINDOW";
        displayRemainingMillis = std::clamp<std::int64_t>(
            *input.signedRemainingMillis + 10'000,
            0,
            10'000
        );
    }

    HudPresentation result;
    if (input.deathmatch) {
        result.roundText = input.deathmatchSequence > 0
            ? "DEATH MATCH " + std::to_string(input.deathmatchSequence)
            : "DEATH MATCH";
    } else if (input.roundNumber >= 1 && input.roundNumber <= 3) {
        result.roundText = "ROUND " + std::to_string(input.roundNumber);
    }
    result.fpsText = input.renderFps && std::isfinite(*input.renderFps) && *input.renderFps > 0.0
        ? "FPS : " + std::to_string(static_cast<int>(std::lround(*input.renderFps)))
        : "FPS : -";
    result.ownScoreText = "Score : " + formatNumber(ownScore,input.deathmatch);
    result.opponentScoreText = "Score : " + formatNumber(opponentScore,input.deathmatch);
    result.ownChecks = clearChecks(ownClears);
    result.opponentChecks = clearChecks(opponentClears);
    result.deathmatch = input.deathmatch;
    if (input.deathmatch) {
        auto const ownUsed = ownIsA
            ? input.deathmatchAttemptsUsedA
            : input.deathmatchAttemptsUsedB;
        auto const opponentUsed = ownIsA
            ? input.deathmatchAttemptsUsedB
            : input.deathmatchAttemptsUsedA;
        result.ownAttemptText = "Attempts : " +
            std::to_string(std::clamp(ownUsed, 0, 3)) + "/3";
        result.opponentAttemptText = "Attempts : " +
            std::to_string(std::clamp(opponentUsed, 0, 3)) + "/3";
    }
    result.qualifyingVisible = !input.deathmatch;
    result.qualifyingText = result.qualifyingVisible
        ? "Qualifying : " + formatNumber(input.qualifyingPercent) + "%"
        : "";

    // Death Match has no Qualifying threshold and no summed Attempt score. The
    // timer position is repurposed for the local authoritative remaining budget;
    // it decrements only when an Attempt End has been accepted by the server.
    if (displayState == "DEATHMATCH_PLAYING") {
        result.stateText = "DEATH MATCH";
        result.timerText = std::to_string(std::clamp(3 - ownCompletedAttempts, 0, 3));
    } else if (displayState == "LAST_ATTEMPT_WINDOW") {
        result.stateText = "LAST ATTEMPT";
        result.timerText = windowCountdown(displayRemainingMillis);
        result.windowStateFirst = true;
    } else if (displayState == "FINAL_ATTEMPT_WINDOW") {
        result.stateText = nonNegative(displayRemainingMillis) > 0 ? "" : "FINAL ATTEMPT";
        result.timerText = windowCountdown(displayRemainingMillis);
        result.windowStateFirst = true;
    } else if (displayState == "ROUND_SETTLING") {
        // The 10-second value is a start deadline. Once it reaches zero, keep the
        // terminal 0.0 visible while an attempt that started in time finishes.
        // A three-Clear trigger uses LAST ATTEMPT; the ordinary five-minute expiry
        // uses FINAL ATTEMPT.
        auto const lastAttempt = std::max(ownClears, opponentClears) >= 3;
        result.stateText = lastAttempt ? "LAST ATTEMPT" : "FINAL ATTEMPT";
        result.timerText = "0.0";
        result.windowStateFirst = true;
    } else {
        result.timerText = normalCountdown(displayRemainingMillis);
        if (input.banner == "MATCH_POINT") result.stateText = "MATCH POINT";
        if (input.banner == "TIEBREAKER") result.stateText = "TIEBREAKER";
    }

    result.spectatorVisible = input.spectatorActive;
    if (result.spectatorVisible) {
        result.spectatorOpponentText = input.spectatorOpponentName.empty()
            ? "OPPONENT"
            : input.spectatorOpponentName;
        result.spectatorProgressText = input.spectatorCurrentProgress
            ? "CURRENT : " + std::to_string(*input.spectatorCurrentProgress) + "%"
            : "CURRENT : -";
    }
    result.opponentProgressVisible = input.opponentProgressEnabled;
    if (result.opponentProgressVisible) {
        result.opponentProgressText = input.opponentFinished
            ? "Finished"
            : std::to_string(std::clamp(
                input.opponentCurrentProgress.value_or(0),
                0,
                100
            )) + "%";
        result.practiceIndicatorVisible = input.opponentPractice;
    }
    return result;
}

} // namespace corum::ranked
