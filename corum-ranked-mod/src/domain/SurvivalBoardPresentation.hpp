#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

namespace corum::ranked {
inline std::string survivalClockText(std::optional<std::int64_t> remaining) {
    if(!remaining)return "--:--";
    auto const seconds=(std::max<std::int64_t>(0,*remaining)+999)/1000;
    auto const minutes=seconds/60,tail=seconds%60;
    return (minutes<10?"0":"")+std::to_string(minutes)+":"+(tail<10?"0":"")+std::to_string(tail);
}
inline std::string survivalBoardClock(std::string const& state,
    std::optional<std::int64_t> startsIn,std::optional<std::int64_t> endsIn) {
    if(state=="PLAYING")return "ROUND "+survivalClockText(endsIn);
    if(state=="COUNTDOWN")return "START IN "+survivalClockText(startsIn);
    if(state=="ROUND_RESULT")return "RESULT";
    if(state=="MAP_SELECTION")return "SELECTING MAP";
    if(state=="PREPARING")return "PREPARING";
    if(state=="ENTERING")return "WAITING FOR PLAYERS";
    return "";
}
// Local card coordinates reserve separate title, avatar and score rows.
struct SurvivalCardLayout {
    float width,height,titleY,avatarY,recordY,titleHeight,recordHeight,avatarSize;
};
inline SurvivalCardLayout survivalCardLayout(float cellWidth,float cellHeight) {
    float const width=cellWidth-8,height=cellHeight-4;
    float const textHeight=std::clamp(height*.23f,7.0f,12.0f);
    return {width,height,height-4-textHeight/2,height/2,4+textHeight/2,
        textHeight,textHeight,std::max(6.0f,height-2*textHeight-12)};
}
// Explicit server elimination always outranks the live podium. Unknown rank
// gets no medal; final standings provide their own authoritative ranks.
inline int survivalCardMedal(int rank,bool eliminated) {
    return eliminated?0:rank>=1 && rank<=3?rank:0;
}
}
