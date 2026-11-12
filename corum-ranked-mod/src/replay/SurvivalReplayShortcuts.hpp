#pragma once

namespace corum::ranked::replay {

// Desktop keyboard shortcuts for the replay controls. The mouse already drives
// the on-screen buttons; these keys mirror them (cocos enumKeyCodes values).
enum class ReplayShortcut { None, TogglePause, Restart, Back10, Forward10, Faster, Slower };

constexpr ReplayShortcut replayShortcut(int key) {
    switch (key) {
    case 0x20: case 0x4B: return ReplayShortcut::TogglePause; // Space, K
    case 0x52: case 0x24: return ReplayShortcut::Restart;     // R, Home
    case 0x25: case 0x4A: return ReplayShortcut::Back10;      // Left, J
    case 0x27: case 0x4C: return ReplayShortcut::Forward10;   // Right, L
    case 0x26: return ReplayShortcut::Faster;                 // Up
    case 0x28: return ReplayShortcut::Slower;                 // Down
    default: return ReplayShortcut::None;
    }
}

// The SPEED button cycles 0.5x -> 1x -> 2x -> 4x; the arrow keys step within
// that range and stop at either end instead of wrapping.
constexpr double replaySpeedStep(double speed, int direction) {
    constexpr double speeds[] = {.5, 1, 2, 4};
    int index = 1;
    for (int i = 0; i < 4; ++i) if (speeds[i] == speed) index = i;
    index += direction > 0 ? 1 : direction < 0 ? -1 : 0;
    if (index < 0) index = 0;
    if (index > 3) index = 3;
    return speeds[index];
}

} // namespace corum::ranked::replay
