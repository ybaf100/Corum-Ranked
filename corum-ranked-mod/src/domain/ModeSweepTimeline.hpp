#pragma once

#include <algorithm>
#include <cmath>

namespace corum::ranked {

// "Seam sweep": main menu (L1, the Survival / Solo halves) to a mode lobby
// (L2, the mode card on the left and its panel). The selected half becomes
// the lobby's left column, the other half slides off along the seam, the tier
// trophy flies to its lobby place and the panel rows come in one by one.
// Leaving the lobby plays the same frames backwards (t from the end to 0).
//
// Every value is a pure function of t in milliseconds: t = 0 is the main
// menu at rest, t = ModeSweepMs the lobby at rest.
inline constexpr float ModeSweepMs = 780.0f;
inline constexpr float ModeSweepFlightStart = 120.0f, ModeSweepFlightEnd = 520.0f;

namespace sweep {
inline float clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
inline float segment(float t, float from, float to) { return to <= from ? (t >= to ? 1.0f : 0.0f) : clamp01((t - from) / (to - from)); }
inline float inCubic(float p) { return p * p * p; }
inline float outCubic(float p) { return 1.0f - std::pow(1.0f - p, 3.0f); }
inline float inOutCubic(float p) { return p < 0.5f ? 4.0f * p * p * p : 1.0f - std::pow(-2.0f * p + 2.0f, 3.0f) / 2.0f; }
inline float outExpo(float p) { return p >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * p); }
inline float outBack(float p) { constexpr float c1 = 1.70158f, c3 = c1 + 1.0f; return 1.0f + c3 * std::pow(p - 1.0f, 3.0f) + c1 * std::pow(p - 1.0f, 2.0f); }
} // namespace sweep

struct ModeSweepFrame {
    float press = 1.0f;       // ENTER slab scale (the tap; opening only)
    float otherOut = 0.0f;    // other half: share of its slide off-screen (0..1)
    float otherAlpha = 1.0f;
    float travel = 0.0f;      // selected half -> lobby card position (0..1)
    float selAlpha = 1.0f;    // the menu half fades out over the lobby card
    float cardAlpha = 0.0f;
    bool flying = false;      // the trophy is in flight (shown on its own)
    float flight = 0.0f;      // trophy: menu place (0) -> lobby place (1)
    float lift = 0.0f;        // trophy arc, 0..1 of the lift height
    bool menuTrophyHome = true, lobbyTrophyHome = false;
    float header = 0.0f;      // header title swap (0 menu, 1 lobby)
    float heroOffset = 1.0f;  // START button: remaining share of its slide in
    float heroAlpha = 0.0f;
};

inline ModeSweepFrame modeSweepFrame(float t) {
    using namespace sweep;
    t = std::clamp(t, 0.0f, ModeSweepMs);
    ModeSweepFrame f;
    f.press = 1.0f - 0.07f * std::sin(3.14159265f * segment(t, 0.0f, 110.0f));
    f.otherOut = inCubic(segment(t, 60.0f, 380.0f));
    f.otherAlpha = 1.0f - f.otherOut;
    f.travel = outExpo(segment(t, 90.0f, 500.0f));
    f.cardAlpha = segment(t, 200.0f, 460.0f);
    f.selAlpha = 1.0f - f.cardAlpha;
    f.flying = t >= ModeSweepFlightStart && t < ModeSweepFlightEnd;
    f.flight = outCubic(segment(t, ModeSweepFlightStart, ModeSweepFlightEnd));
    // The arc peaks halfway through the flight time (not its eased distance).
    f.lift = std::sin(3.14159265f * segment(t, ModeSweepFlightStart, ModeSweepFlightEnd));
    f.menuTrophyHome = t < ModeSweepFlightStart;
    f.lobbyTrophyHome = t >= ModeSweepFlightEnd;
    f.header = inOutCubic(segment(t, 180.0f, 420.0f));
    f.heroOffset = 1.0f - outBack(segment(t, 470.0f, ModeSweepMs));
    f.heroAlpha = segment(t, 470.0f, 560.0f);
    return f;
}

// One row band of the lobby panel, top to bottom: slides in from the right
// with a signal-colour line drawn under it that fades once the row settles.
struct ModeSweepBand {
    float offset = 1.0f; // remaining share of the slide in (0 = in place)
    float alpha = 0.0f;
    float wipe = 0.0f;   // share of the row the line has drawn
    float wipeAlpha = 0.0f;
};

inline float modeSweepBandStart(int index, int count) {
    auto const step = count > 1 ? std::min(45.0f, 180.0f / static_cast<float>(count - 1)) : 0.0f;
    return 300.0f + step * static_cast<float>(std::max(0, index));
}

inline ModeSweepBand modeSweepBand(float t, int index, int count) {
    using namespace sweep;
    t = std::clamp(t, 0.0f, ModeSweepMs);
    auto const a = modeSweepBandStart(index, count);
    ModeSweepBand b;
    auto const p = outCubic(segment(t, a, a + 260.0f));
    b.offset = 1.0f - p;
    b.alpha = p;
    auto const w = segment(t, a, a + 200.0f);
    b.wipe = outCubic(w);
    // The line is gone by the end, so leaving starts without it.
    b.wipeAlpha = w > 0.0f ? 1.0f - segment(t, a + 200.0f, std::min(a + 380.0f, ModeSweepMs)) : 0.0f;
    return b;
}

// Opening runs t forward; leaving the lobby runs the same frames backwards.
inline float modeSweepTime(float elapsedMs, bool opening) {
    auto const t = std::clamp(elapsedMs, 0.0f, ModeSweepMs);
    return opening ? t : ModeSweepMs - t;
}

} // namespace corum::ranked
