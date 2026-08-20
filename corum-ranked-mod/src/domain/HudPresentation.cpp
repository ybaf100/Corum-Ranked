#include "HudPresentation.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace {

std::string formatNumber(double value) {
    if (std::abs(value - std::round(value)) < 0.001) {
        return std::to_string(static_cast<int>(std::lround(value)));
    }
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << value;
    return stream.str();
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

std::array<ClearCheckColor, 2> clearChecks(int approvedClears) {
    auto const count = std::clamp(approvedClears, 0, 2);
    return {
        count >= 1 ? ClearCheckColor::Green : ClearCheckColor::Gray,
        count >= 2 ? ClearCheckColor::Green : ClearCheckColor::Gray,
    };
}

HudLayout layoutHud(float width, float height) {
    return {
        .topLeftX = 42.0f,
        .topRightX = width - 42.0f,
        .topY = height - 7.0f,
        .bottomLeftX = 10.0f,
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

    HudPresentation result;
    result.fpsText = input.renderFps && std::isfinite(*input.renderFps) && *input.renderFps > 0.0
        ? "FPS : " + std::to_string(static_cast<int>(std::lround(*input.renderFps)))
        : "FPS : -";
    result.ownScoreText = "Score : " + std::to_string(ownScore);
    result.opponentScoreText = "Score : " + std::to_string(opponentScore);
    result.ownChecks = clearChecks(ownClears);
    result.opponentChecks = clearChecks(opponentClears);
    result.qualifyingText = "Qualifying : " + formatNumber(input.qualifyingPercent) + "%";

    if (input.state == "LAST_ATTEMPT_WINDOW") {
        result.stateText = "LAST ATTEMPT";
        result.timerText = windowCountdown(input.remainingMillis);
        result.windowStateFirst = true;
    } else if (input.state == "FINAL_ATTEMPT_WINDOW") {
        result.stateText = "FINAL ATTEMPT";
        result.timerText = windowCountdown(input.remainingMillis);
        result.windowStateFirst = true;
    } else {
        result.timerText = normalCountdown(input.remainingMillis);
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
    return result;
}

} // namespace corum::ranked
