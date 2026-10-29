#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
namespace corum::ranked {
inline constexpr std::array<std::string_view,6> SurvivalBadgeGrades{"MYTHIC","LEGENDARY","EPIC","FEATURED","RATED","ELIMINATED"};
inline bool survivalBadgeKnown(std::string_view grade){return std::find(SurvivalBadgeGrades.begin(),SurvivalBadgeGrades.end(),grade)!=SurvivalBadgeGrades.end();}
struct SurvivalBadgeMotion {double scale=1,opacity=0;};
inline SurvivalBadgeMotion survivalBadgeMotion(std::size_t index,double elapsed){
    auto const t=elapsed-.16*index-.10;if(t<=0)return {.78,0};
    auto const p=std::clamp(t/.30,0.,1.),u=p-1;
    return {1+2.70158*u*u*u+1.70158*u*u,std::clamp(t/.08,0.,1.)};
}
inline float survivalBadgeGalleryOffset(float offset,std::size_t count,float width){return std::clamp(offset,0.f,std::max(0.f,(static_cast<float>(count)-5)*width/5));}
// A result is presented only after authoritative settlement. Screen progress
// is independent of financial settlement and scoped to account + match.
enum class SurvivalResultScreen {Pending,Ceremony,Score,Personal};
inline SurvivalResultScreen survivalResultScreen(bool settled,bool eligible,int completed){
    if(!settled)return SurvivalResultScreen::Pending;
    if(completed>=2)return SurvivalResultScreen::Personal;
    if(eligible&&completed<1)return SurvivalResultScreen::Ceremony;
    return SurvivalResultScreen::Score;
}
}
