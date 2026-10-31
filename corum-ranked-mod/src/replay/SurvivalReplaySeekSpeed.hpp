#pragma once
#include <array>
#include <cstddef>
namespace corum::ranked::replay {
/** User-selectable replay / spectator seek throughput. Seeking always replays
 * real native frames from a verified reset or the validated restore history (no
 * position checkpoints); the speed only changes how many of those frames run
 * per rendered frame. */
enum class ReplaySeekSpeed { Low = 0, Normal = 1, Fast = 2, UltraFast = 3 };
inline constexpr std::array<ReplaySeekSpeed, 4> kReplaySeekSpeeds{
    ReplaySeekSpeed::Low, ReplaySeekSpeed::Normal, ReplaySeekSpeed::Fast, ReplaySeekSpeed::UltraFast};
struct ReplaySeekBudget { std::size_t framesPerPump; double millisPerPump; };
inline ReplaySeekSpeed replaySeekSpeedFromInt(int value) {
    if (value <= 0) return ReplaySeekSpeed::Low;
    if (value == 2) return ReplaySeekSpeed::Fast;
    if (value >= 3) return ReplaySeekSpeed::UltraFast;
    return ReplaySeekSpeed::Normal;
}
inline int replaySeekSpeedToInt(ReplaySeekSpeed speed) { return static_cast<int>(speed); }
/** Fast and UltraFast are opt-in behind (escalating) warnings. */
inline bool replaySeekSpeedNeedsWarning(ReplaySeekSpeed speed) {
    return speed == ReplaySeekSpeed::Fast || speed == ReplaySeekSpeed::UltraFast;
}
/** Normal keeps the previous fixed policy (96 frames / 3 ms). Normal playback
 * (not seeking) never uses these budgets: it advances one recorded frame per dt. */
inline ReplaySeekBudget replaySeekBudget(ReplaySeekSpeed speed) {
    switch (speed) {
        case ReplaySeekSpeed::Low: return {48, 2.0};
        case ReplaySeekSpeed::Fast: return {640, 14.0};
        case ReplaySeekSpeed::UltraFast: return {4096, 40.0};
        case ReplaySeekSpeed::Normal: break;
    }
    return {96, 3.0};
}
/** Spectator restore catch-up: published samples (50 ms of native world each)
 * applied per rendered frame. Normal is the previous policy (<= 8, ~2 ms). */
inline ReplaySeekBudget spectatorCatchupBudget(ReplaySeekSpeed speed) {
    switch (speed) {
        case ReplaySeekSpeed::Low: return {4, 1.0};
        case ReplaySeekSpeed::Fast: return {24, 8.0};
        case ReplaySeekSpeed::UltraFast: return {64, 30.0};
        case ReplaySeekSpeed::Normal: break;
    }
    return {8, 2.0};
}
}
