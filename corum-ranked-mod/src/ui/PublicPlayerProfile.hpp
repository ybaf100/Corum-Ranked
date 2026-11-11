#pragma once
#include "RankBadgeAssets.hpp"
#include "RankedVisuals.hpp"
#include "AchievementEffects.hpp"
#include "RankedTranslations.hpp"
#include "../domain/AchievementStyle.hpp"
#include "../domain/RankTierPresentation.hpp"
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
        auto const marks=profile["promotion"]["results"].asArray().unwrapOr(std::vector<matjson::Value>{});
        for(auto const& mark:marks)value+=mark.asString().unwrapOr("")=="WIN"?" O":" X";
    }else if(auto lp=profile["lp"].asInt();lp.isOk())value=std::to_string(lp.unwrap())+" LP";
    auto* score=menuLabel(value,.30f,{282,y},profile["promotion"].isObject()?Green:Text,FontWeight::ExtraBold);score->setAnchorPoint({1,.5f});fitSingleLineMenuLabel(score,112,20,.30f);parent->addChild(score,5);
}
// Q2 tier panel (.panel #20232E): the mode's trophy, then the mode caption,
// the tier in its colour and "LP · record" (or the promotion marks).
inline void publicTierPanel(CCNode* parent,std::string const& caption,matjson::Value const& profile,std::string const& record,CCPoint center,CCSize size){
    parent->addChild(surface(size,center,SurfaceRaised,SurfaceRaised,255,false),1);
    auto const left=center.x-size.width/2,right=center.x+size.width/2;
    auto const tier=profile["tier"].asString().unwrapOr("");
    auto textX=left+10;
    if(!tier.empty()){
        auto* badge=createAnimatedRankBadge(tier,{38,38},RankBadgeDisplay::Leaderboard);badge->setPosition({left+27,center.y});parent->addChild(badge,5);textX=left+52;
    }
    auto const width=right-8-textX;
    auto put=[&](std::string const& text,float y,float scale,ccColor3B color,FontWeight weight){
        auto* label=menuLabel(text,scale,{textX,y},color,weight);label->setAnchorPoint({0,.5f});fitSingleLineMenuLabel(label,width,scale*48,scale);parent->addChild(label,5);return label;
    };
    put(caption,center.y+15,.19f,TextMuted,FontWeight::SemiBold);
    put(tier.empty()?i18n::text("info-unavailable","Unavailable"):rankTierDisplay(tier),center.y+1,.34f,tier.empty()?TextMuted:tierColor(tier),FontWeight::Black);
    std::string detail;
    if(profile["promotion"].isObject()){
        detail=i18n::text("promotion-match","PROMOTION MATCH");
        auto const marks=profile["promotion"]["results"].asArray().unwrapOr(std::vector<matjson::Value>{});
        for(auto const& mark:marks)detail+=mark.asString().unwrapOr("")=="WIN"?" O":" X";
    }else if(auto lp=profile["lp"].asInt();lp.isOk())detail=std::to_string(lp.unwrap())+" LP";
    if(!record.empty())detail+=detail.empty()?record:" · "+record;
    if(!detail.empty())put(detail,center.y-14,.19f,profile["promotion"].isObject()?Green:TextMuted,FontWeight::SemiBold);
}
// Board .xbtn: a square ghost button with an X.
inline CCNode* publicCloseFace(){
    auto* face=surface({20,20},{},Steel,SurfaceRaised,250,true);
    auto* glyph=menuLabel("X",.26f,{10,10},Text,FontWeight::Black);fitSingleLineMenuLabel(glyph,14,14,.26f);face->addChild(glyph,5);
    return face;
}
}
