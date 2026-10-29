#pragma once
#include "RankedVisuals.hpp"
#include "../domain/SurvivalBadgePresentation.hpp"
#include <matjson.hpp>
namespace corum::ranked::ui {
inline ccColor3B survivalBadgeColor(std::string_view grade){
    return grade=="MYTHIC"?ccc3(255,211,72):grade=="LEGENDARY"?ccc3(240,247,255):grade=="EPIC"?ccc3(208,137,70):
        grade=="FEATURED"?ccc3(65,226,232):grade=="RATED"?ccc3(156,235,96):ccc3(240,64,85);
}
// The grade is an official server value; presentation never derives an award
// from a local rank, map feature grade, replay or cached player statistics.
inline CCNode* survivalRoundBadge(matjson::Value const& badge,CCSize size,bool heading=true){
    auto const grade=badge["grade"].asString().unwrapOr("");if(!survivalBadgeKnown(grade))return nullptr;
    auto const color=survivalBadgeColor(grade);auto const eliminated=grade=="ELIMINATED";
    auto* card=surface(size,{},color,eliminated?ccc3(43,13,24):ccc3(color.r/4,color.g/4,color.b/4),255,true);
    card->setAnchorPoint({.5f,.5f});
    // Restrained metallic highlight, confined inside the existing surface.
    if(!eliminated){auto* shine=CCLayerColor::create({color.r,color.g,color.b,48},size.width-6,2);shine->setPosition({3,size.height-5});card->addChild(shine,2);}
    auto const round=badge["roundLabel"].asString().unwrapOr("");
    if(heading){auto* label=menuLabel(round,.22f,{size.width/2,size.height-12},Text,FontWeight::ExtraBold);fitFullMenuLabel(label,size.width-8,16,.22f);card->addChild(label,5);}
    auto* mark=menuLabel(eliminated?"X":grade,eliminated?.72f:.24f,{size.width/2,size.height*.52f},color,FontWeight::Black);
    fitFullMenuLabel(mark,size.width-8,size.height*.35f,eliminated?.72f:.24f);card->addChild(mark,5);
    auto const rank=badge["rank"].asInt().unwrapOr(0);
    auto const caption=rank>0?fmt::format("#{}",rank):eliminated?grade:std::string{};
    auto* label=menuLabel(caption,.22f,{size.width/2,12},Text,FontWeight::ExtraBold);fitFullMenuLabel(label,size.width-8,14,.22f);card->addChild(label,5);
    return card;
}
}
