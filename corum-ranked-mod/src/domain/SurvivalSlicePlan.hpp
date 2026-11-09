#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace corum::ranked {

// Slice ("musou"): the victims' icons scatter over a wide field, the gold
// executor icon dashes through them in all directions cutting one per dash,
// the last one gets a finale (wind-up, hit-stop, follow-through) and the icon
// flies back to its card. Times are seconds from the elimination window start.
// The total (sliceMusouMilliseconds) mirrors the rules package survivalSliceMs,
// which sets the server's round-result presentation length.
inline constexpr int SliceCutStartMs = 1300;
inline constexpr int SliceDashBudgetMs = 2000;
inline constexpr int SliceDashMinMs = 50;
inline constexpr int SliceDashMaxMs = 100;
inline constexpr float SliceFinalWindup = 0.35f;
inline constexpr float SliceFinalStrike = 0.06f;
inline constexpr float SliceHitStop = 0.12f;
inline constexpr float SliceFinalThrough = 0.14f;
inline constexpr float SliceFollow = 0.25f;
inline constexpr float SliceReturn = 0.60f;
inline constexpr int SliceFinaleMs = 1520; // windup + strike + hit-stop + through + follow + return
inline constexpr int SliceHoldMs = 1100;   // after the icon is home (rules SURVIVAL_SLICE_HOLD_MS)
inline constexpr int SliceResultBeforeMs = 1950;
inline constexpr int SliceResultMinimumMs = 5500;
inline constexpr float SliceVictimSpread = 0.55f; // victims leave over this span
inline constexpr float SliceVictimFlight = 0.70f; // each victim's flight to the field
inline constexpr float SliceLift = 0.35f;         // executor rises out of its card
inline constexpr float SliceTravel = 0.65f;       // executor flies over the field
static_assert(SliceFinaleMs == 350 + 60 + 120 + 140 + 250 + 600);
static_assert(SliceLift + SliceTravel + 0.3f <= SliceCutStartMs / 1000.0f + 0.001f);

inline int sliceDashMilliseconds(std::size_t victims) {
    if (victims < 2) return 0;
    auto const regular = static_cast<double>(victims - 1);
    return std::clamp(static_cast<int>(std::lround(SliceDashBudgetMs / regular)), SliceDashMinMs, SliceDashMaxMs);
}
/** Elimination window start until the executor icon is home. */
inline int sliceMusouMilliseconds(std::size_t victims) {
    if (victims == 0) return 0;
    return SliceCutStartMs + static_cast<int>(victims - 1) * sliceDashMilliseconds(victims) + SliceFinaleMs;
}
/** Round-result presentation length with a Slice executor of `victims`. */
inline int sliceResultMilliseconds(std::size_t victims) {
    if (victims == 0) return SliceResultMinimumMs;
    return std::max(SliceResultMinimumMs, SliceResultBeforeMs + sliceMusouMilliseconds(victims) + SliceHoldMs);
}

struct SliceVec { float x = 0, y = 0; };
inline SliceVec sliceUnit(SliceVec v) {
    auto const l = std::sqrt(v.x * v.x + v.y * v.y);
    return l > 0.0001f ? SliceVec{v.x / l, v.y / l} : SliceVec{0, -1};
}
inline float sliceDistance(SliceVec a, SliceVec b) { return std::hypot(a.x - b.x, a.y - b.y); }

struct SliceDash {
    std::size_t victim = 0; // index into the victims (elimination order)
    float start = 0;        // dash start
    float cut = 0;          // when the blade crosses the victim
    SliceVec from, to, dir;
};
struct SliceMusouPlan {
    std::vector<SliceVec> field;    // per victim: scattered resting point
    std::vector<float> leave;       // per victim: when its icon leaves the card
    std::vector<SliceDash> dashes;  // regular dashes in cut order
    std::size_t finale = 0;         // the last victim (nearest the field centre)
    SliceVec start;                 // executor hover point before the first dash
    SliceVec windup, through, finaleDir;
    float dash = 0, windupAt = 0, finaleCut = 0, home = 0;
};

// Deterministic small RNG (same stream for the same victim count).
struct SliceRandom {
    std::uint32_t state;
    explicit SliceRandom(std::uint32_t seed) : state(seed) {}
    float next() {
        state += 0x6D2B79F5u;
        std::uint32_t t = state;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + ((t ^ (t >> 7)) * (t | 61u));
        return static_cast<float>((t ^ (t >> 14)) >> 8) / 16777216.0f;
    }
};

/**
 * `center`/`radius`: the scatter ellipse; `gap`: minimum distance between
 * resting icons (shrinks when the field is crowded); `overshoot`: how far a
 * dash runs past its victim; `finalReach`: wind-up and follow-through length;
 * `low`/`high`: the stage bounds the executor stays inside.
 * The executor's dash is a flash-like zip, x(u) = 1 - 2^(-10u) (cocos
 * CCEaseExponentialOut), so the blade crosses a victim at
 * u = -log2(1 - f) / 10, f = distance / (distance + overshoot).
 */
inline SliceMusouPlan sliceMusouPlan(std::size_t victims, SliceVec center, SliceVec radius, float gap, float overshoot,
    float finalReach, SliceVec low, SliceVec high) {
    SliceMusouPlan plan;
    if (victims == 0) return plan;
    SliceRandom random(static_cast<std::uint32_t>(victims) * 7919u + 17u);
    // Scatter: rejection sampling inside the ellipse, relaxing the gap.
    for (int guard = 0; guard < 60 && plan.field.size() < victims; ++guard) {
        plan.field.clear();
        for (std::size_t k = 0; k < victims; ++k) {
            bool placed = false;
            for (int tries = 0; tries < 400 && !placed; ++tries) {
                auto const a = random.next() * 6.2831853f, r = std::sqrt(random.next());
                SliceVec const p{center.x + std::cos(a) * r * radius.x, center.y + std::sin(a) * r * radius.y};
                if (std::all_of(plan.field.begin(), plan.field.end(), [&](SliceVec q) { return sliceDistance(p, q) >= gap; })) {
                    plan.field.push_back(p);
                    placed = true;
                }
            }
            if (!placed) break;
        }
        if (plan.field.size() < victims) gap *= 0.94f;
    }
    while (plan.field.size() < victims) plan.field.push_back(center);
    for (std::size_t k = 0; k < victims; ++k)
        plan.leave.push_back(0.05f + SliceVictimSpread * static_cast<float>(k) / static_cast<float>(victims));

    auto clampInside = [&](SliceVec p) { return SliceVec{std::clamp(p.x, low.x, high.x), std::clamp(p.y, low.y, high.y)}; };
    // The finale victim: nearest the centre. The rest: a tour that keeps the
    // dashes long and turns hard every time.
    plan.finale = 0;
    for (std::size_t k = 1; k < victims; ++k)
        if (sliceDistance(plan.field[k], center) < sliceDistance(plan.field[plan.finale], center)) plan.finale = k;
    plan.start = clampInside({center.x, center.y + radius.y + gap});
    std::vector<std::size_t> rest;
    for (std::size_t k = 0; k < victims; ++k) if (k != plan.finale) rest.push_back(k);
    auto const ideal = std::max(gap * 3.0f, std::min(radius.x, radius.y) * 1.1f);
    SliceVec at = plan.start, previous{0, -1};
    plan.dash = static_cast<float>(sliceDashMilliseconds(victims)) / 1000.0f;
    auto clock = SliceCutStartMs / 1000.0f;
    while (!rest.empty()) {
        std::size_t best = 0;
        float score = 1e9f;
        for (std::size_t i = 0; i < rest.size(); ++i) {
            auto const p = plan.field[rest[i]];
            auto const d = sliceDistance(at, p);
            auto const n = sliceUnit({p.x - at.x, p.y - at.y});
            auto const s = std::abs(d - ideal) / ideal + 0.7f * ((previous.x * n.x + previous.y * n.y) + 1.0f) / 2.0f;
            if (s < score) { score = s; best = i; }
        }
        auto const victim = rest[best];
        rest.erase(rest.begin() + static_cast<std::ptrdiff_t>(best));
        auto const target = plan.field[victim];
        auto const dir = sliceUnit({target.x - at.x, target.y - at.y});
        auto const distance = sliceDistance(at, target);
        auto const f = distance / (distance + overshoot);
        auto const crossing = std::min(1.0f, -std::log2(1.0f - f) / 10.0f);
        SliceDash dash{victim, clock, clock + crossing * plan.dash, at, {target.x + dir.x * overshoot, target.y + dir.y * overshoot}, dir};
        plan.dashes.push_back(dash);
        at = dash.to;
        previous = dir;
        clock += plan.dash;
    }
    plan.windupAt = clock;
    auto const target = plan.field[plan.finale];
    auto const away = sliceUnit({at.x - target.x, at.y - target.y});
    plan.windup = clampInside({target.x + away.x * finalReach, target.y + away.y * finalReach});
    plan.finaleDir = sliceUnit({target.x - plan.windup.x, target.y - plan.windup.y});
    plan.through = clampInside({target.x + plan.finaleDir.x * finalReach, target.y + plan.finaleDir.y * finalReach});
    plan.finaleCut = plan.windupAt + SliceFinalWindup + SliceFinalStrike;
    plan.home = plan.finaleCut + SliceHitStop + SliceFinalThrough + SliceFollow + SliceReturn;
    return plan;
}

} // namespace corum::ranked
