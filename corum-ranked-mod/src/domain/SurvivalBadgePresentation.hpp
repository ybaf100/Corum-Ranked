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
struct SurvivalBadgeCardLayout {float x,y,width,height;};
struct SurvivalBadgePage {std::size_t page,pages,begin,count;};
inline SurvivalBadgePage survivalBadgePage(std::size_t count,std::size_t page,float width,float height){
    auto const columns=std::max<std::size_t>(1,std::min<std::size_t>(3,static_cast<std::size_t>(width*.52f/66)));
    auto const capacity=columns*(height<290?1u:2u);
    auto const pages=std::max<std::size_t>(1,(count+capacity-1)/capacity);
    page=std::min(page,pages-1);auto const begin=page*capacity;
    return {page,pages,begin,std::min<std::size_t>(capacity,count-begin)};
}
inline SurvivalBadgeCardLayout survivalBadgeCardLayout(float width,float height,std::size_t count,std::size_t index){
    auto const area=width*.52f;
    auto const columns=std::max<std::size_t>(1,std::min<std::size_t>({3,count,static_cast<std::size_t>(area/66)}));
    auto const rows=std::max<std::size_t>(1,(count+columns-1)/columns),row=index/columns;
    auto const inRow=std::min(columns,count-row*columns);auto const slot=area/columns;
    auto const h=std::min({78.f,slot-6,(height-126-8*(rows-1))/rows});
    auto const top=std::min(height-64,height*.53f+(h*rows+8*(rows-1))/2);
    return {width*.48f+area/2+(static_cast<float>(index%columns)-(inRow-1)/2.f)*slot,
        top-h/2-row*(h+8),h,h};
}
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
