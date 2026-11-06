#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace corum::ranked {

// Season label and time per mode (set in Admin). targetAt is the season end
// while the mode is enabled, or the mode's start time while it is disabled.
struct SeasonView {
    bool known = false;
    bool preseason = true;
    int number = 0;
    std::optional<std::int64_t> targetAtMs;
    bool operator==(SeasonView const&) const = default;
};

enum class SeasonCountdownKind { Hidden, LeftDays, LeftClock, Ended, StartsDays, StartsHours };
enum class SeasonTone { Gold, Red, Muted };

struct SeasonCountdown {
    SeasonCountdownKind kind = SeasonCountdownKind::Hidden;
    SeasonTone tone = SeasonTone::Muted;
    std::int64_t days = 0, hours = 0, minutes = 0, seconds = 0;
};

// Absolute instants minus server-synced time: correct in every time zone.
inline SeasonCountdown seasonCountdown(SeasonView const& season, bool modeEnabled, std::int64_t serverNowMs) {
    SeasonCountdown result;
    if (!season.known || !season.targetAtMs) return result;
    auto const left = *season.targetAtMs - serverNowMs;
    if (left <= 0) {
        if (!modeEnabled) return result; // start time passed while still disabled
        result.kind = SeasonCountdownKind::Ended;return result;
    }
    auto const totalSeconds = (left + 999) / 1000;
    result.days = totalSeconds / 86400;
    result.hours = (totalSeconds % 86400) / 3600;
    result.minutes = (totalSeconds % 3600) / 60;
    result.seconds = totalSeconds % 60;
    if (!modeEnabled) {
        // Disabled mode: "starts in", in light grey.
        result.kind = result.days > 0 ? SeasonCountdownKind::StartsDays : SeasonCountdownKind::StartsHours;
        result.tone = SeasonTone::Muted;
        return result;
    }
    result.kind = result.days > 0 ? SeasonCountdownKind::LeftDays : SeasonCountdownKind::LeftClock;
    result.tone = result.days > 0 ? SeasonTone::Gold : SeasonTone::Red;
    return result;
}

// One shared timer when both modes show the same season and state.
inline bool seasonTimerShared(SeasonView const& solo, bool soloEnabled, SeasonView const& survival, bool survivalEnabled) {
    return solo == survival && soloEnabled == survivalEnabled;
}

// Hand: one 30-degree step per second with a short overshoot snap.
inline float seasonHandAngle(std::int64_t serverNowMs) {
    auto const second = static_cast<double>(serverNowMs / 1000);
    auto const fraction = std::clamp(static_cast<double>(serverNowMs % 1000) / 220.0, 0.0, 1.0);
    constexpr double c1 = 1.70158, c3 = c1 + 1.0;
    auto const eased = 1.0 + c3 * std::pow(fraction - 1.0, 3) + c1 * std::pow(fraction - 1.0, 2);
    return static_cast<float>(std::fmod((second - 1.0 + eased) * 30.0, 360.0));
}

} // namespace corum::ranked
