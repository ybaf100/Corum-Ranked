#include "SurvivalHud.hpp"
#include "RankedTranslations.hpp"
#include "../domain/TransitionTrace.hpp"
#include <Geode/cocos/actions/CCActionEase.h>
#include <cmath>

using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
ccColor3B color(SurvivalStanding const& row) {auto c=survivalRankColor(row.rank,row.passing);return ccc3(c.r,c.g,c.b);}
std::string score(double value,int decimals) {
    auto text=fmt::format("{:.{}f}",value,decimals);
    if(decimals) {while(!text.empty()&&text.back()=='0')text.pop_back();if(!text.empty()&&text.back()=='.')text.pop_back();}
    return text;
}
}
SurvivalHud* SurvivalHud::create() {auto* node=new SurvivalHud;if(node->init()){node->autorelease();return node;}delete node;return nullptr;}
bool SurvivalHud::init() {
    if(!CCNode::init())return false;setID("survival-play-hud"_spr);
    auto const size=CCDirector::sharedDirector()->getWinSize();setContentSize(size);
    m_round=label("",.27f,{size.width/2,size.height-16},Cyan,FontWeight::ExtraBold);addChild(m_round);
    m_timer=label("03:00",.52f,{size.width/2,size.height-38},Text,FontWeight::ExtraBold);addChild(m_timer);
    m_difference=label("",.29f,{size.width/2+82,size.height-34},Text,FontWeight::ExtraBold);addChild(m_difference);
    m_differenceCaption=label("",.17f,{size.width/2+82,size.height-49},TextMuted,FontWeight::SemiBold);addChild(m_differenceCaption);
    m_rank=label("",.85f,{12,12},Text,FontWeight::Black);m_rank->setAnchorPoint({0,0});addChild(m_rank);
    m_waiting=label("",.24f,{size.width/2,28},TextMuted,FontWeight::SemiBold);addChild(m_waiting);
    return true;
}
void SurvivalHud::present(SurvivalMatchView const& view,std::int64_t remainingMs,float dt) {
    auto const size=CCDirector::sharedDirector()->getWinSize();
    auto const context=view.id+":"+view.roundKey+":"+std::to_string(view.mapGeneration);
    if(m_context!=context) {for(auto& [id,row]:m_rows)row.node->removeFromParentAndCleanup(true);m_rows.clear();m_context=context;m_lastRank=0;}
    auto const deathmatch=view.phase=="TIEBREAK";
    auto const full=view.roundNumber>=3;
    auto const rows=survivalHudRows(view.standings,view.ownId,full);
    auto const gap=std::min(15.0f,(size.height*.60f)/std::max<std::size_t>(1,rows.size()));
    auto const maximumScale=std::min(.25f,gap/60.0f);
    // Keep every participant's node, including rows currently outside Top 3.
    // An incoming Top 3 row moves from its previous position instead of being
    // recreated at its destination. Numeric interpolation is independent of rank.
    for(std::size_t index=0;index<view.standings.size();++index) {
        auto const& value=view.standings[index];auto found=m_rows.find(value.id);
        if(found==m_rows.end()) {
            auto const y=size.height-34-static_cast<float>(index)*gap;
            auto* node=CCNode::create();node->setPosition({8,y});node->setVisible(false);addChild(node,5);
            auto* caption=label("",maximumScale,{0,0},color(value),FontWeight::SemiBold);caption->setAnchorPoint({0,1});caption->setOpacity(0);node->addChild(caption);
            found=m_rows.emplace(value.id,Row{node,caption,y,value,{0,0},false}).first;
        }
        found->second.value=value;found->second.tween.observe(value.score);found->second.tween.advance(dt);
    }
    for(auto& [id,row]:m_rows) {
        auto const visible=std::any_of(rows.begin(),rows.end(),[&](auto const& p){return p.id==id;});
        if(row.visible==visible)continue;row.visible=visible;row.label->stopActionByTag(7415);
        if(visible) {row.node->setVisible(true);auto* fade=CCFadeTo::create(.15f,255);fade->setTag(7415);row.label->runAction(fade);}
        else {auto* fade=CCSequence::create(CCFadeTo::create(.15f,0),CallFuncExt::create([node=row.node]{node->setVisible(false);}),nullptr);fade->setTag(7415);row.label->runAction(fade);}
    }
    for(std::size_t i=0;i<rows.size();++i) {
        auto const& value=rows[i];auto const y=size.height-34-static_cast<float>(i)*gap-(i==3&&!full?7:0);
        auto found=m_rows.find(value.id);
        if(found==m_rows.end())continue;
        auto& row=found->second;row.node->setVisible(true);row.value=value;
        if(row.targetY!=y) {
            row.targetY=y;row.node->stopActionByTag(7414);auto* movement=CCEaseSineOut::create(CCMoveTo::create(.30f,{8,y}));movement->setTag(7414);row.node->runAction(movement);
        }
        auto name=value.id==view.ownId?i18n::text("you","YOU"):value.name;
        row.label->setString(fmt::format("{}  {}  {}%",survivalOrdinal(value.rank),name,score(row.tween.displayed,deathmatch?3:value.bot?0:2)).c_str());
        row.label->setColor(color(value));fitLabelWidth(row.label,std::min(170.0f,size.width*.35f),maximumScale,std::min(.15f,maximumScale));
    }
    auto own=std::find_if(view.standings.begin(),view.standings.end(),[&](auto const& p){return p.id==view.ownId;});
    if(own!=view.standings.end()) {
        m_rank->setString(survivalOrdinal(own->rank).c_str());m_rank->setColor(color(*own));
        if(m_lastRank!=own->rank) {m_lastRank=own->rank;m_rank->stopAllActions();setLabelScale(m_rank,.85f);
            m_rank->runAction(CCSequence::create(CCEaseSineOut::create(CCScaleTo::create(.12f,fontScale(m_rank,1.02f))),CCEaseSineInOut::create(CCScaleTo::create(.22f,fontScale(m_rank,.85f))),nullptr));}
        auto reference=view.roundNumber>=4?view.first:view.cutoff;
        if(reference) {
            auto const difference=own->score-*reference;
            m_difference->setString(((difference>0?std::string("+"):std::string{})+score(difference,deathmatch?3:2)).c_str());
            m_difference->setColor(difference<0?Red:difference>0?Green:Text);
        } else m_difference->setString("-");
        m_waiting->setString(own->waiting?i18n::text("survival-attempts-finished","ATTEMPTS COMPLETE · WAITING FOR RESULTS").c_str():"");
    }
    m_differenceCaption->setString(i18n::text(view.roundNumber>=4?"survival-from-first":"survival-from-cutoff",view.roundNumber>=4?"From 1st":"From Cutoff").c_str());
    m_round->setString((deathmatch?std::string("DEATH MATCH"):view.phase=="FINAL"?i18n::text("final","FINAL"):fmt::format("{} {}",i18n::text("round","ROUND"),view.roundNumber)).c_str());
    auto const seconds=(std::max<std::int64_t>(0,remainingMs)+999)/1000;
    m_timer->setString(fmt::format("{:02}:{:02}",seconds/60,seconds%60).c_str());
}
}
