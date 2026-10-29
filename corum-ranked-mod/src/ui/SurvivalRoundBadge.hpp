#pragma once
#include "RankedVisuals.hpp"
#include "../domain/SurvivalBadgePresentation.hpp"
#include "../domain/SurvivalHudPresentation.hpp"
#include <matjson.hpp>
namespace corum::ranked::ui {
inline ccColor3B survivalBadgeColor(std::string_view grade){
    auto const rank=grade=="MYTHIC"?1:grade=="LEGENDARY"?2:grade=="EPIC"?3:grade=="FEATURED"?4:6;
    auto const color=survivalRankColor(rank,grade!="ELIMINATED");return {color.r,color.g,color.b};
}
// The grade is an official server value; presentation never derives an award
// from a local rank, map feature grade, replay or cached player statistics.
inline CCNode* survivalRoundBadge(matjson::Value const& badge,CCSize size,bool heading=true){
    auto const grade=badge["grade"].asString().unwrapOr("");if(!survivalBadgeKnown(grade))return nullptr;
    auto const color=survivalBadgeColor(grade);auto const eliminated=grade=="ELIMINATED";
    auto const metal=grade=="MYTHIC"||grade=="LEGENDARY"||grade=="EPIC";
    auto const fill=grade=="MYTHIC"?ccc3(220,168,47):grade=="LEGENDARY"?ccc3(184,195,209):grade=="EPIC"?ccc3(179,113,63):
        eliminated?ccc3(43,13,24):ccc3(color.r/4,color.g/4,color.b/4);
    auto const ink=metal?Ink:Text;
    auto* card=surface(size,{},color,fill,255,true);
    card->setAnchorPoint({.5f,.5f});
    // Restrained metallic highlight, confined inside the existing surface.
    if(!eliminated){auto* shine=CCLayerColor::create({color.r,color.g,color.b,48},size.width-6,2);shine->setPosition({3,size.height-5});card->addChild(shine,2);}
    auto const round=badge["roundLabel"].asString().unwrapOr("");
    if(heading){auto* label=menuLabel(round,.22f,{size.width/2,size.height-12},ink,FontWeight::ExtraBold);fitFullMenuLabel(label,size.width-8,16,.22f);card->addChild(label,5);}
    auto* mark=menuLabel(eliminated?"X":grade,eliminated?.72f:.24f,{size.width/2,size.height*.52f},metal?Ink:color,FontWeight::Black);
    fitFullMenuLabel(mark,size.width-8,size.height*.35f,eliminated?.72f:.24f);card->addChild(mark,5);
    auto const rank=badge["rank"].asInt().unwrapOr(0);
    auto const caption=rank>0?fmt::format("#{}",rank):eliminated?grade:std::string{};
    auto* label=menuLabel(caption,.22f,{size.width/2,12},ink,FontWeight::ExtraBold);fitFullMenuLabel(label,size.width-8,14,.22f);card->addChild(label,5);
    return card;
}
}
