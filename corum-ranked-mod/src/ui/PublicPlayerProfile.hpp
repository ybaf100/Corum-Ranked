#pragma once
#include "RankBadgeAssets.hpp"
#include "RankedVisuals.hpp"
#include "AchievementEffects.hpp"
#include "RankedTranslations.hpp"
#include "../domain/AchievementStyle.hpp"
#include <matjson.hpp>
namespace corum::ranked::ui {
using namespace geode::prelude;
inline std::string publicTitleText(matjson::Value const& title){
    if(!title.isObject())return {};
    auto value=title[i18n::isKorean()?"nameKo":"nameEn"].asString().unwrapOr("");
    auto const level=title["displayLabel"].asString().unwrapOr("");if(!value.empty()&&!level.empty())value+=" "+level;return value;
}
inline CCLabelBMFont* publicPlayerTitle(CCNode* parent,matjson::Value const& title,CCPoint at,float width,float scale=.22f){
    auto const value=publicTitleText(title);if(value.empty())return nullptr;
    auto* label=menuLabel(value,scale,at,Text,FontWeight::ExtraBold);fitSingleLineMenuLabel(label,width,18,scale);
    auto const rarity=title["rarity"].asString().unwrapOr("");
    auto tier=static_cast<int>(title["level"].asInt().unwrapOr(0));
    if(tier<=0)tier=rarity=="MYTHIC"?5:rarity=="LEGENDARY"?4:rarity=="EPIC"?3:rarity=="RARE"?2:1;
    styleAchievementText(label,tier,true);parent->addChild(label,6);return label;
}
inline void publicRankLine(CCNode* parent,std::string const& caption,matjson::Value const& profile,float y){
    auto* text=menuLabel(caption,.27f,{18,y},TextMuted,FontWeight::SemiBold);text->setAnchorPoint({0,.5f});fitSingleLineMenuLabel(text,118,20,.27f);parent->addChild(text,5);
    auto const tier=profile["tier"].asString().unwrapOr("");
    if(!tier.empty()){auto* badge=createAnimatedRankBadge(tier,{25,25},RankBadgeDisplay::Leaderboard);badge->setPosition({151,y});parent->addChild(badge,5);}
    auto value=i18n::text("info-unavailable","Unavailable");
    if(profile["promotion"].isObject()){
        value=i18n::text("promotion-match","PROMOTION MATCH");
        for(auto const& mark:profile["promotion"]["results"].asArray().unwrapOr(std::vector<matjson::Value>{}))value+=mark.asString().unwrapOr("")=="WIN"?" O":" X";
    }else if(auto lp=profile["lp"].asInt();lp.isOk())value=std::to_string(lp.unwrap())+" LP";
    auto* score=menuLabel(value,.30f,{282,y},profile["promotion"].isObject()?Green:Text,FontWeight::ExtraBold);score->setAnchorPoint({1,.5f});fitSingleLineMenuLabel(score,112,20,.30f);parent->addChild(score,5);
}
}
