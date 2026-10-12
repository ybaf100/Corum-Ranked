#pragma once
#include <algorithm>

namespace corum::ranked {
inline constexpr float CompetitiveClockScale=0.60f*1.70f;
struct CompetitiveClockPair {float clockX,deltaX;};
// Solo GameplayUx's authored minimum offset and text-width gap calculation.
inline CompetitiveClockPair competitiveClockPair(float center,float clockWidth,float deltaWidth) {
    auto const offset=std::max(34.0f,(clockWidth+deltaWidth)*.25f+10.0f*.5f);
    return {center-offset,center+offset};
}
inline constexpr int SurvivalRosterMilliseconds=5000;
inline constexpr int SurvivalEliminationMilliseconds=1050;
inline constexpr int SurvivalAfterEliminationMilliseconds=2000;
inline constexpr int SurvivalBeforeEliminationMilliseconds=SurvivalRosterMilliseconds-SurvivalEliminationMilliseconds-SurvivalAfterEliminationMilliseconds;
}
