#pragma once
#include <cstddef>
namespace corum::ranked::replay {
/** User-selectable replay seek throughput. Seeking always replays real native
 * frames from a verified reset (no position checkpoints); the speed only
 * changes how many of those frames run per rendered frame. */
enum class ReplaySeekSpeed { Low = 0, Normal = 1, Fast = 2 };
struct ReplaySeekBudget { std::size_t framesPerPump; double millisPerPump; };
inline ReplaySeekSpeed replaySeekSpeedFromInt(int value) {
    return value <= 0 ? ReplaySeekSpeed::Low : value >= 2 ? ReplaySeekSpeed::Fast : ReplaySeekSpeed::Normal;
}
inline int replaySeekSpeedToInt(ReplaySeekSpeed speed) { return static_cast<int>(speed); }
inline ReplaySeekSpeed nextReplaySeekSpeed(ReplaySeekSpeed speed) {
    return speed == ReplaySeekSpeed::Low ? ReplaySeekSpeed::Normal : speed == ReplaySeekSpeed::Normal ? ReplaySeekSpeed::Fast : ReplaySeekSpeed::Low;
}
/** Normal keeps the previous fixed policy (96 frames / 3 ms). Fast trades frame
 * time for throughput and is opt-in behind a warning. Normal playback (not
 * seeking) never uses these budgets: it advances one recorded frame per dt. */
inline ReplaySeekBudget replaySeekBudget(ReplaySeekSpeed speed) {
    switch (speed) {
        case ReplaySeekSpeed::Low: return {48, 2.0};
        case ReplaySeekSpeed::Fast: return {640, 14.0};
        case ReplaySeekSpeed::Normal: break;
    }
    return {96, 3.0};
}
}
