#pragma once
#include "SurvivalCollectionBadge.hpp"
#include "../domain/MapDifficulty.hpp"
#include <matjson.hpp>
namespace corum::ranked::ui {
// The grade is an official server value; presentation never derives an award
// from a local rank, map feature grade, replay or cached player statistics.
inline CCNode* survivalRoundBadge(matjson::Value const& badge,CCSize size,bool heading=true){
    auto const grade=badge["grade"].asString().unwrapOr("");if(!survivalBadgeKnown(grade))return nullptr;
    auto const eliminated=grade=="ELIMINATED";
    auto const difficulty=recordedMapDifficulty(badge["difficulty"].asString().unwrapOr(""));
    auto const side=std::min(size.width,size.height);
    auto* card=survivalCollectionBadge(difficulty?difficulty->nativeCode:0,grade,side,side-22);
    card->setAnchorPoint({.5f,.5f});
    auto const round=badge["roundLabel"].asString().unwrapOr("");
    if(heading){auto* label=menuLabel(round,.22f,{side/2,side-10},Text,FontWeight::ExtraBold);fitFullMenuLabel(label,side-8,12,.22f);card->addChild(label,5);}
    auto const rank=badge["rank"].asInt().unwrapOr(0);
    auto const caption=rank>0?fmt::format("#{}",rank):eliminated?grade:std::string{};
    auto* label=menuLabel(caption,.22f,{side/2,10},Text,FontWeight::ExtraBold);fitFullMenuLabel(label,side-8,12,.22f);card->addChild(label,5);
    return card;
}
}
