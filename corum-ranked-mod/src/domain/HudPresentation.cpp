#include "HudPresentation.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace {

std::string formatNumber(double value) {
    if (std::abs(value - std::round(value)) < 0.0005) {
        return std::to_string(static_cast<long long>(std::llround(value)));
    }
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    auto text = stream.str();
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text;
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

HudPresentation presentHud(HudInput const& input) {
    auto const ownIsA = input.side == "A";
    auto const ownScore = ownIsA ? input.scoreA : input.scoreB;
    auto const opponentScore = ownIsA ? input.scoreB : input.scoreA;
    auto const ownClears = ownIsA ? input.clearsA : input.clearsB;
    auto const opponentClears = ownIsA ? input.clearsB : input.clearsA;
    auto const ownAttempts = ownIsA ? input.deathmatchAttemptsUsedA : input.deathmatchAttemptsUsedB;
    auto const opponentAttempts = ownIsA ? input.deathmatchAttemptsUsedB : input.deathmatchAttemptsUsedA;

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
    result.ownScoreText = "Score : " + formatNumber(ownScore);
    result.opponentScoreText = "Score : " + formatNumber(opponentScore);
    result.ownChecks = clearChecks(ownClears);
    result.opponentChecks = clearChecks(opponentClears);
    result.qualifyingText = "Qualifying : " + formatNumber(input.qualifyingPercent) + "%";
    result.deathmatch = input.deathmatch;
    if (input.deathmatch) {
        result.ownAttemptText = "Attempts : " + std::to_string(std::clamp(ownAttempts, 0, 3)) + "/3";
        result.opponentAttemptText = "Attempts : " + std::to_string(std::clamp(opponentAttempts, 0, 3)) + "/3";
    }

    if (displayState == "DEATHMATCH_PLAYING") {
        result.stateText = "DEATH MATCH";
        result.timerText = "3 ATTEMPTS";
    } else if (displayState == "LAST_ATTEMPT_WINDOW") {
        result.stateText = "LAST ATTEMPT";
        result.timerText = windowCountdown(displayRemainingMillis);
        result.windowStateFirst = true;
    } else if (displayState == "FINAL_ATTEMPT_WINDOW") {
        result.stateText = "FINAL ATTEMPT";
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
        result.opponentProgressText = std::to_string(std::clamp(
            input.opponentCurrentProgress.value_or(0),
            0,
            100
        )) + "%";
        result.practiceIndicatorVisible = input.localPractice;
    }
    return result;
}

} // namespace corum::ranked
