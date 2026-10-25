#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

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
    bool reduced=false;
};
inline bool survivalCompactCards(std::size_t survivors) {return survivors>30;}
inline bool survivalReducedCards(std::size_t survivors) {return survivors>16&&survivors<=30;}
enum class SurvivalCardDensity {Full,Reduced,Compact};
inline SurvivalCardDensity survivalPhaseCardDensity(int entrants,std::string_view phase) {
    if(entrants!=64)return SurvivalCardDensity::Full;
    if(phase.empty()||phase=="ROUND_1"||phase=="ROUND_2")return SurvivalCardDensity::Compact;
    return phase=="ROUND_3"?SurvivalCardDensity::Reduced:SurvivalCardDensity::Full;
}
inline SurvivalGridLayout survivalGridLayout(float width,float height,std::size_t count,int survivors,std::string_view phase="",int entrants=0) {
    count=std::max<std::size_t>(1,count);
    bool const compact=entrants?survivalPhaseCardDensity(entrants,phase)==SurvivalCardDensity::Compact:survivalCompactCards(static_cast<std::size_t>(std::max(0,survivors)));
    bool const reduced=entrants?survivalPhaseCardDensity(entrants,phase)==SurvivalCardDensity::Reduced:survivalReducedCards(static_cast<std::size_t>(std::max(0,survivors)));
    auto const columns=std::min<std::size_t>(compact?8:reduced?6:4,count);
    auto const totalRows=(count+columns-1)/columns;
    auto const available=std::max(24.f,height-(compact?133.f:163.f));
    // Tied cutoffs and result boards can still contain more than 16 rows after
    // the survivor count drops. Page full cards too, keeping readable heights.
    // Compact waiting rosters fit all 64 entrants on one screen. The unused
    // pager row becomes board space; the caller places its status below it.
    auto const rows=compact?totalRows:std::min(totalRows,std::max<std::size_t>(1,static_cast<std::size_t>(available/(reduced?28.f:36.f))));
    auto const pageSize=columns*rows;
    return {columns,rows,pageSize,(count+pageSize-1)/pageSize,
        std::max(24.f,std::min(width-36,compact?760.f:reduced?640.f:480.f))/columns,
        compact?std::min(44.f,available/rows):reduced?std::min(42.f,available/rows):std::min(66.f,std::max(36.f,available/rows)),compact,reduced};
}
inline SurvivalCardLayout survivalCardLayout(float cellWidth,float cellHeight,bool compact=false,bool reduced=false) {
    if(compact){
        float const width=cellWidth-4,height=cellHeight-4;
        float const avatar=std::max(2.f,std::min({height-6,26.f,width*.32f})),textWidth=std::max(2.f,width-avatar-10);
        return {width,height,height/2,height/2,0,std::max(1.f,std::min(12.f,height-4)),0,avatar,
            avatar+7+textWidth/2,3+avatar/2,textWidth};
    }
    if(reduced){
        float const width=cellWidth-4,height=cellHeight-4;
        float const textHeight=std::max(3.f,std::min(11.f,(height-8)/2)),avatar=textHeight;
        float const textWidth=std::max(4.f,width-avatar-12);
        return {width,height,height-3-textHeight/2,height-3-textHeight/2,3+textHeight/2,textHeight,textHeight,avatar,
            avatar+8+textWidth/2,4+avatar/2,textWidth};
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
