#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace corum::ranked {

// The lobby loading bar follows the runtime's real entry steps (the status
// each step sets). Inside a step the bar creeps towards the next one but
// stops short of it, so it never looks stalled and never claims a step that
// has not happened.
struct LoadingStep {
    float reached = 0.0f;  // where this step starts
    float next = 0.0f;     // where the following step starts
};

inline LoadingStep loadingStepFor(std::string_view status, bool awaitingSurvival) {
    // Ready, waiting only for the Survival profile: the last step.
    if (awaitingSurvival) return {0.86f, 0.98f};
    auto const starts = [&](std::string_view prefix) { return status.substr(0, prefix.size()) == prefix; };
    if (starts("Loading authoritative Ranked configuration")) return {0.06f, 0.26f};
    if (starts("Verifying your Geometry Dash account")) return {0.26f, 0.50f};
    if (starts("Creating a server session")) return {0.50f, 0.70f};
    if (starts("Checking for an active Placement run")) return {0.70f, 0.86f};
    // Other Loading statuses (placement start / finalize) have no fixed place.
    return {0.30f, 0.86f};
}

// One frame of the shown value. It catches up quickly with the reached step,
// then creeps towards the next step and stays 2% short of it. It never moves
// back, so a later status with an earlier place does not rewind the bar.
inline float advanceLoadingBar(float shown, LoadingStep step, float dt) {
    dt = std::clamp(dt, 0.0f, 0.25f);
    if (shown < step.reached)
        return std::min(step.reached, shown + (step.reached - shown) * std::min(1.0f, dt * 6.0f) + dt * 0.05f);
    auto const cap = step.next - 0.02f;
    if (shown >= cap) return shown;
    return std::min(cap, shown + (cap - shown) * std::min(1.0f, dt * 0.35f));
}

// The tip after `current`: a different one at random when there are several.
inline std::size_t nextLoadingTip(std::size_t current, std::size_t count, std::size_t random) {
    if (count <= 1) return 0;
    auto const pick = random % (count - 1);
    return pick >= current ? pick + 1 : pick;
}

} // namespace corum::ranked
