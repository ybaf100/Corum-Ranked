#include "SurvivalHud.hpp"
#include "RankedTranslations.hpp"
#include "../domain/HudPresentation.hpp"
#include <Geode/cocos/actions/CCActionEase.h>
#include <cmath>

using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
ccColor3B color(SurvivalStanding const& row) {auto c=survivalRankColor(row.rank,row.passing);return ccc3(c.r,c.g,c.b);}
std::string score(double value,int decimals) {
    auto text=fmt::format("{:.{}f}",value,decimals);
    if(decimals){while(!text.empty()&&text.back()=='0')text.pop_back();if(!text.empty()&&text.back()=='.')text.pop_back();}
    return text;
}
void fade(RankedHudLabel const& label,bool visible) {
    for(auto* node:{label.foreground,label.shadow}) {
        node->stopActionByTag(7415);auto* action=CCFadeTo::create(.15f,visible?(node==label.foreground?255:205):0);
        action->setTag(7415);node->runAction(action);
    }
}
}
SurvivalHud* SurvivalHud::create() {auto* node=new SurvivalHud;if(node->init()){node->autorelease();return node;}delete node;return nullptr;}
bool SurvivalHud::init() {
    if(!CCNode::init())return false;setID("survival-play-hud"_spr);
    auto const size=CCDirector::sharedDirector()->getWinSize();setContentSize(size);
    auto const top=layoutHud(size.width,size.height).topY;
    m_round=rankedHudLabel(this,"",FontWeight::SemiBold,.527f,{.5f,1},{size.width/2,top-1},Gold);
    m_timer=rankedHudLabel(this,"03:00",FontWeight::Black,1.02f,{.5f,1},{size.width/2,top-18},Text);
    m_difference=rankedHudLabel(this,"",FontWeight::Black,.48f,{.5f,1},{size.width/2+96,top-23},Text);
    m_differenceCaption=rankedHudLabel(this,"",FontWeight::SemiBold,.26f,{.5f,1},{size.width/2+96,top-43},TextMuted);
    m_rank=rankedHudLabel(this,"",FontWeight::Black,1.12f,{0,0},{12,12},Text);
    m_waiting=rankedHudLabel(this,"",FontWeight::SemiBold,.30f,{.5f,0},{size.width/2,14},TextMuted);
    scheduleUpdate();return true;
}
void SurvivalHud::update(float dt) {
    // Native Pause/Resume may replace PlayLayer's selectors. The HUD owns its
    // presentation heartbeat and always reads the authoritative server clock.
    auto& runtime=SurvivalRuntime::get();runtime.tick();
    if(isRunning() && runtime.active())present(runtime.view(),runtime.remainingMs(),dt);
}
void SurvivalHud::present(SurvivalMatchView const& view,std::int64_t remainingMs,float dt) {
    auto const size=CCDirector::sharedDirector()->getWinSize();
    auto const context=view.id+":"+view.roundKey+":"+std::to_string(view.mapGeneration);
    if(m_context!=context){for(auto& [id,row]:m_rows)row.node->removeFromParentAndCleanup(true);m_rows.clear();m_context=context;m_lastRank=0;}
    bool const deathmatch=view.phase=="TIEBREAK",full=view.roundNumber>=3;
    auto const rows=survivalHudRows(view.standings,view.ownId,full);
    auto const gap=std::clamp((size.height*.60f)/std::max<std::size_t>(1,rows.size()),12.0f,20.0f);
    auto const width=std::min(182.0f,size.width*.36f),scale=std::min(.36f,gap/50.0f);
    auto const top=layoutHud(size.width,size.height).topY-28;
    for(std::size_t i=0;i<view.standings.size();++i) {
        auto const& value=view.standings[i];auto found=m_rows.find(value.id);
        if(found==m_rows.end()) {
            Row row;row.node=CCNode::create();row.targetY=top-static_cast<float>(i)*gap;row.node->setPosition({8,row.targetY});
            auto* plate=CCLayerColor::create({5,10,22,145},width,gap-1);plate->setPosition({0,-gap+1});row.node->addChild(plate);
            row.rank=rankedHudLabel(row.node,"",FontWeight::Black,scale,{0,1},{5,-1},Text);
            row.name=rankedHudLabel(row.node,"",FontWeight::Black,scale,{0,1},{39,-1},Text);
            row.score=rankedHudLabel(row.node,"",FontWeight::Black,scale,{1,1},{width-5,-1},Text);
            for(auto const& label:{row.rank,row.name,row.score})label.opacity(0);
            row.node->setVisible(false);addChild(row.node,5);found=m_rows.emplace(value.id,std::move(row)).first;
        }
        found->second.value=value;found->second.tween.observe(value.score);found->second.tween.advance(dt);
    }
    for(auto& [id,row]:m_rows) {
        auto const visible=std::any_of(rows.begin(),rows.end(),[&](auto const& value){return value.id==id;});
        if(row.visible!=visible){row.visible=visible;row.node->setVisible(visible);for(auto const& label:{row.rank,row.name,row.score})fade(label,visible);}
    }
    for(std::size_t i=0;i<rows.size();++i) {
        auto const& value=rows[i];auto& row=m_rows.at(value.id);auto const y=top-static_cast<float>(i)*gap-(i==3&&!full?7:0);
        if(row.targetY!=y){row.targetY=y;row.node->stopActionByTag(7414);auto* move=CCEaseSineOut::create(CCMoveTo::create(.30f,{8,y}));move->setTag(7414);row.node->runAction(move);}
        auto const name=value.id==view.ownId?i18n::text("you","YOU"):value.name;
        row.rank.text(survivalOrdinal(value.rank));row.name.text(name);row.score.text(score(row.tween.displayed,deathmatch?3:value.bot?0:2)+"%");
        for(auto const& label:{row.rank,row.name,row.score})label.color(color(value));
        row.rank.fit(31,scale,scale*.8f);row.name.fit(width-101,scale,scale*.8f);row.score.fit(52,scale,scale*.8f);
    }
    auto own=std::find_if(view.standings.begin(),view.standings.end(),[&](auto const& p){return p.id==view.ownId;});
    if(own!=view.standings.end()) {
        m_rank.text(survivalOrdinal(own->rank));m_rank.color(color(*own));
        if(m_lastRank!=own->rank){m_lastRank=own->rank;for(auto* label:{m_rank.foreground,m_rank.shadow}) {
            label->stopAllActions();setLabelScale(label,1.12f);label->runAction(CCSequence::create(
                CCEaseSineOut::create(CCScaleTo::create(.12f,fontScale(label,1.28f))),CCEaseSineInOut::create(CCScaleTo::create(.22f,fontScale(label,1.12f))),nullptr));}}
        auto const reference=view.roundNumber>=4?view.first:view.cutoff;
        if(reference){auto const delta=own->score-*reference;m_difference.text((delta>0?"+":"")+score(delta,deathmatch?3:2));m_difference.color(delta<0?Red:delta>0?Green:Text);}
        else m_difference.text("-");
        m_waiting.text(own->waiting?i18n::text("survival-attempts-finished","ATTEMPTS COMPLETE · WAITING FOR RESULTS"):"");
    }
    m_differenceCaption.text(i18n::text(view.roundNumber>=4?"survival-from-first":"survival-from-cutoff",view.roundNumber>=4?"From 1st":"From Cutoff"));
    m_round.text(deathmatch?"DEATH MATCH":view.phase=="FINAL"?i18n::text("final","FINAL"):fmt::format("{} {}",i18n::text("round","ROUND"),view.roundNumber));
    auto const seconds=(std::max<std::int64_t>(0,remainingMs)+999)/1000;m_timer.text(fmt::format("{:02}:{:02}",seconds/60,seconds%60));
}
} // namespace corum::ranked::ui
