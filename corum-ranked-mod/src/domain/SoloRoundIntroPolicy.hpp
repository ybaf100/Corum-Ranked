#pragma once
#include <algorithm>
#include <cmath>

namespace corum::ranked {
// Solo round overview -> revolving map reveal -> board rise. Times are seconds
// from the start of the overview (the start of the round's PREPARE phase).
struct SoloIntroTimeline {
    float shift = -1, slam = -1, spin = 0, revealed = 0, exit = 0, board = 0, done = 0;
};
inline constexpr float kSoloIntroSpinSeconds = 1.8f;
inline constexpr float kSoloIntroRevealHold = 1.0f;
inline constexpr float kSoloIntroExitSeconds = 0.42f, kSoloIntroExitStagger = 0.13f;
inline constexpr float kSoloIntroRiseSeconds = 0.52f, kSoloIntroRiseStagger = 0.09f;
inline constexpr float kSoloIntroShiftSeconds = 0.65f, kSoloIntroSlamSeconds = 0.28f;
inline constexpr float kSoloIntroTurns = 2.5f;

// Death match entry shifts the round cards one slot left, then slams the new
// death-match card into the freed slot before the same revolving reveal.
inline SoloIntroTimeline soloIntroTimeline(bool deathmatchShift) {
    SoloIntroTimeline t;
    if (deathmatchShift) { t.shift = 1.2f; t.slam = 1.95f; t.spin = 3.1f; }
    else t.spin = 1.6f;
    t.revealed = t.spin + kSoloIntroSpinSeconds;
    t.exit = t.revealed + kSoloIntroRevealHold;
    t.board = t.exit + 0.6f;
    t.done = t.board + kSoloIntroRiseSeconds + 2 * kSoloIntroRiseStagger;
    return t;
}

inline float soloEaseOutCubic(float p) { p = std::clamp(p, 0.f, 1.f); return 1 - std::pow(1 - p, 3.f); }
inline float soloEaseInCubic(float p) { p = std::clamp(p, 0.f, 1.f); return p * p * p; }
inline float soloEaseOutQuart(float p) { p = std::clamp(p, 0.f, 1.f); return 1 - std::pow(1 - p, 4.f); }

// Revolving door: the card turns about its vertical axis and decelerates onto
// its back face. Cocos has no perspective here, so the width follows |cos|.
struct SoloRevolveFrame { float angleDegrees = 0, scaleX = 1, lift = 1; bool front = true; };
inline SoloRevolveFrame soloRevolveFrame(float elapsedSinceSpin) {
    SoloRevolveFrame frame;
    if (elapsedSinceSpin <= 0) return frame;
    auto const p = std::clamp(elapsedSinceSpin / kSoloIntroSpinSeconds, 0.f, 1.f);
    frame.angleDegrees = soloEaseOutQuart(p) * kSoloIntroTurns * 360.f;
    auto const c = std::cos(frame.angleDegrees * 3.14159265f / 180.f);
    frame.lift = 1 + 0.04f * std::sin(p * 3.14159265f);
    frame.scaleX = std::max(0.f, std::abs(c));
    frame.front = c >= 0;
    return frame;
}
}
