#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>

namespace corum::ranked {
inline constexpr float CompetitiveClockScale=0.60f*1.70f;
struct CompetitiveClockPair {float clockX,deltaX;};
// Solo GameplayUx's authored minimum offset and text-width gap calculation.
inline CompetitiveClockPair competitiveClockPair(float center,float clockWidth,float deltaWidth) {
    auto const offset=std::max(34.0f,(clockWidth+deltaWidth)*.25f+10.0f*.5f);
    return {center-offset,center+offset};
}
// Round result presentation (5.5 s): 1.95 s uniform roster, 1.55 s sequential
// eliminations (lowest rank first), 2.0 s hold of the final colours.
inline constexpr int SurvivalRosterMilliseconds=5500;
inline constexpr int SurvivalBeforeEliminationMilliseconds=1950;
inline constexpr int SurvivalEliminationMilliseconds=1550;
inline constexpr int SurvivalAfterEliminationMilliseconds=SurvivalRosterMilliseconds-SurvivalBeforeEliminationMilliseconds-SurvivalEliminationMilliseconds;
static_assert(SurvivalAfterEliminationMilliseconds==2000);
/** One player's fall (move+rotate+shrink); its red card fade starts with it. */
inline constexpr int SurvivalEliminationFallMilliseconds=560;
/** Strictly increasing start offsets (ms from the elimination window start).
 * The last fall ends inside the 1.55 s window; more eliminated players only
 * compress the spacing (64-player rounds are not claimed visually distinct). */
inline std::vector<int> survivalEliminationStarts(std::size_t count) {
    std::vector<int> starts;
    if (count == 0) return starts;
    auto const span = SurvivalEliminationMilliseconds - SurvivalEliminationFallMilliseconds;
    for (std::size_t i = 0; i < count; ++i)
        starts.push_back(count == 1 ? 0 : static_cast<int>(static_cast<long long>(span) * static_cast<long long>(i) / static_cast<long long>(count - 1)));
    return starts;
}
}
