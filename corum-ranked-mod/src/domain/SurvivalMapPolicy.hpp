#pragma once
#include <algorithm>

namespace corum::ranked {
enum class SurvivalDifficultyGroup { RedBronze, Silver, Gold };
enum class SurvivalMapPhase { Round1, Round2, Round3, Final, Tiebreak, Round4 };
enum class SurvivalDemon { None, Easy, Medium, Hard, Insane, Extreme, Invalid };

// GD 2.2081 / Geode DemonDifficultyType. Search demonFilter uses other codes.
inline SurvivalDemon survivalDemonFromNativeCode(int code) {
    switch(code) {
        case 0:return SurvivalDemon::Hard;
        case 3:return SurvivalDemon::Easy;
        case 4:return SurvivalDemon::Medium;
        case 5:return SurvivalDemon::Insane;
        case 6:return SurvivalDemon::Extreme;
        default:return SurvivalDemon::Invalid;
    }
}
struct SurvivalMapMetadata {
    int levelId = 0, stars = 0, version = 0;
    SurvivalDemon demon = SurvivalDemon::None;
    bool automatic = false, platformer = false;
    int length=-1;
    bool operator==(SurvivalMapMetadata const&) const = default;
};
inline bool survivalMapAllowed(SurvivalMapMetadata const& map, SurvivalDifficultyGroup group,
    SurvivalMapPhase phase, int minimumLevelId = 40000000) {
    if(map.levelId < std::max(40000000,minimumLevelId) || map.version < 1 ||
        map.automatic || map.platformer || map.length>3 || map.length<-1 || map.stars < 1 || map.stars > 10) return false;
    if(phase==SurvivalMapPhase::Round4)phase=SurvivalMapPhase::Round3;
    auto const offset = static_cast<int>(group);
    if(phase == SurvivalMapPhase::Tiebreak)
        return map.stars == 10 && static_cast<int>(map.demon) == static_cast<int>(SurvivalDemon::Medium)+offset;
    if(map.demon != SurvivalDemon::None) {
        if(map.stars != 10) return false;
        if(phase == SurvivalMapPhase::Round3) return group == SurvivalDifficultyGroup::Gold && map.demon == SurvivalDemon::Easy;
        if(phase != SurvivalMapPhase::Final) return false;
        return map.demon >= SurvivalDemon::Easy && static_cast<int>(map.demon) <= static_cast<int>(SurvivalDemon::Easy)+offset;
    }
    if(phase == SurvivalMapPhase::Final) return group == SurvivalDifficultyGroup::RedBronze && map.stars >= 7 && map.stars <= 9;
    auto const minimum = 4 + offset + static_cast<int>(phase);
    return map.stars >= minimum && map.stars <= std::min(9,minimum+2);
}
} // namespace corum::ranked
