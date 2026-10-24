#pragma once
#include <algorithm>
#include <cstddef>
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
    float titleX=0,avatarX=0,titleWidth=0;
};
struct SurvivalGridLayout {
    std::size_t columns,rows,pageSize,pages;
    float cellWidth,cellHeight;
    bool compact;
};
inline bool survivalCompactCards(std::size_t survivors) {return survivors>16;}
inline SurvivalGridLayout survivalGridLayout(float width,float height,std::size_t count,int survivors) {
    count=std::max<std::size_t>(1,count);
    bool const compact=survivalCompactCards(static_cast<std::size_t>(std::max(0,survivors)));
    auto const columns=std::min<std::size_t>(compact?(width<420?6:8):4,count);
    auto const totalRows=(count+columns-1)/columns;
    auto const available=std::max(24.f,height-163.f);
    // Tied cutoffs and result boards can still contain more than 16 rows after
    // the survivor count drops. Page full cards too, keeping readable heights.
    auto const rows=std::min(totalRows,std::max<std::size_t>(1,static_cast<std::size_t>(available/(compact?24.f:36.f))));
    auto const pageSize=columns*rows;
    return {columns,rows,pageSize,(count+pageSize-1)/pageSize,
        std::max(24.f,std::min(width-36,compact?760.f:480.f))/columns,
        compact?std::min(44.f,available/rows):std::min(66.f,std::max(36.f,available/rows)),compact};
}
inline SurvivalCardLayout survivalCardLayout(float cellWidth,float cellHeight,bool compact=false) {
    if(compact){
        float const width=cellWidth-4,height=cellHeight-4;
        float const avatar=std::clamp(height-6,6.f,26.f),textWidth=std::max(6.f,width-avatar-10);
        return {width,height,height/2,height/2,0,std::min(12.f,height-4),0,avatar,
            avatar+7+textWidth/2,3+avatar/2,textWidth};
    }
    float const width=cellWidth-8,height=cellHeight-4;
    float const textHeight=std::clamp(height*.23f,7.0f,12.0f);
    return {width,height,height-4-textHeight/2,height/2,4+textHeight/2,
        textHeight,textHeight,std::max(6.0f,height-2*textHeight-12),width/2,width/2,width-12};
}
// Explicit server elimination always outranks the live podium. Unknown rank
// gets no medal; final standings provide their own authoritative ranks.
inline int survivalCardMedal(int rank,bool eliminated) {
    return eliminated?0:rank>=1 && rank<=3?rank:0;
}
}
