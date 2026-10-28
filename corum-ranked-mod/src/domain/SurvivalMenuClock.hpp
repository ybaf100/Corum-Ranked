#pragma once
#include "SurvivalRoundPresentation.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

namespace corum::ranked {
// Presentation consumes the existing server clock; it owns no countdown.
struct SurvivalMenuClock {
    bool visible=false, stale=false;
    std::string phase, text;
};
inline SurvivalMenuClock survivalMenuClock(bool active,bool freePlay,std::string const& state,
    std::string const& phase,int round,std::optional<std::int64_t> remaining,
    std::optional<std::int64_t> countdown,std::optional<std::int64_t> syncAge){
    SurvivalMenuClock result;
    if(!active||!freePlay||state=="ROUND_RESULT"||state=="FINAL_RESULT"||state=="CANCELLED")return result;
    result.visible=true;result.phase=survivalRoundLabel(phase,round,true);
    result.stale=!syncAge||*syncAge>15000;
    if(result.stale){result.text="RECONNECTING";return result;}
    if(state=="PLAYING"||state=="COUNTDOWN"){
        auto time=state=="PLAYING"?remaining:countdown;
        if(!time){result.text="SYNCING";return result;}
        auto seconds=(std::max<std::int64_t>(0,*time)+999)/1000;
        result.text=(state=="COUNTDOWN"?"GO ":"")+std::to_string(seconds/60)+":"+(seconds%60<10?"0":"")+std::to_string(seconds%60);
    }else result.text="PREPARING";
    return result;
}
}
