#include "SurvivalHud.hpp"
#include <Geode/binding/GameManager.hpp>
#include "RankedTranslations.hpp"
#include "../domain/HudPresentation.hpp"
#include "../domain/CompetitiveHudLayout.hpp"
#include "../domain/ScoreDisplay.hpp"
#include <Geode/cocos/actions/CCActionEase.h>
#include <cmath>

using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
ccColor3B color(SurvivalStanding const& row) {auto c=survivalRankColor(row.rank,row.passing && !row.eliminated);return ccc3(c.r,c.g,c.b);}
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
    m_round=rankedHudLabel(this,"",FontWeight::SemiBold,.527f,{.5f,1},{size.width/2,top-5},Gold);
    m_timer=rankedHudLabel(this,"03:00",FontWeight::Black,CompetitiveClockScale,{.5f,1},{size.width/2,top-18},Text);
    m_difference=rankedHudLabel(this,"0",FontWeight::ExtraBold,CompetitiveClockScale,{.5f,1},{size.width/2+34,top-18},Text);
    m_rank=rankedHudLabel(this,"",FontWeight::Black,1.68f,{0,0},{12,12},Text);
    m_waiting=rankedHudLabel(this,"",FontWeight::SemiBold,.30f,{.5f,0},{size.width/2,14},TextMuted);
    m_cutoffLine=CCDrawNode::create();addChild(m_cutoffLine,6);
    scheduleUpdate();return true;
}
void SurvivalHud::attachProgressBar(CCSprite* bar,CCSprite* fill,float width,float height) {
    if(!bar || !fill || width<=0 || height<=0)return;
    m_progressBar=WeakRef(bar);m_progressFill=WeakRef(fill);m_progressWidth=width;m_progressHeight=height;
    auto* lines=CCDrawNode::create();lines->setID("survival-progress-markers"_spr);bar->addChild(lines,50);
    m_progressLines=WeakRef(lines);
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
    if(m_context!=context){for(auto& [id,row]:m_rows)row.node->removeFromParentAndCleanup(true);m_rows.clear();m_context=context;m_lastRank=0;m_personalBest={};}
    bool const deathmatch=view.phase=="TIEBREAK";
    auto const rows=survivalHudRows(view.standings,view.ownId,view.targetQualifiers);
    bool const separateOwn=rows.size()>survivalHudLeaderCount(view.standings,view.targetQualifiers);
    auto const gap=std::clamp((size.height*.60f)/std::max<std::size_t>(1,rows.size()),12.0f,20.0f);
    auto const width=std::min(168.0f,size.width*.36f),scale=std::min(.36f,gap/50.0f);
    auto const top=layoutHud(size.width,size.height).topY-28;
    for(std::size_t i=0;i<view.standings.size();++i) {
        auto const& value=view.standings[i];auto found=m_rows.find(value.id);
        if(found==m_rows.end()) {
            Row row;row.node=CCNode::create();row.targetY=top-static_cast<float>(i)*gap;row.node->setPosition({8,row.targetY});
            row.rank=rankedHudLabel(row.node,"",FontWeight::Black,scale,{0,1},{0,-1},Text);
            row.name=rankedHudPlayerName(row.node,"",FontWeight::Black,scale,{0,1},{0,-1},Text);
            row.score=rankedHudLabel(row.node,"",FontWeight::Black,scale,{0,1},{0,-1},Text);
            for(auto const& label:{row.rank,row.name,row.score})label.opacity(0);
            row.node->setVisible(false);addChild(row.node,5);found=m_rows.emplace(value.id,std::move(row)).first;
        }
        found->second.value=value;found->second.tween.observe(value.bestProgress);found->second.tween.advance(dt);
    }
    for(auto& [id,row]:m_rows) {
        auto const visible=std::any_of(rows.begin(),rows.end(),[&](auto const& value){return value.id==id;});
        if(row.visible!=visible){row.visible=visible;row.node->setVisible(visible);for(auto const& label:{row.rank,row.name,row.score})fade(label,visible);}
    }
    float textWidth=0;
    for(std::size_t i=0;i<rows.size();++i) {
        auto const& value=rows[i];auto& row=m_rows.at(value.id);auto const y=top-static_cast<float>(i)*gap-(separateOwn && i+1==rows.size()?7:0);
        if(row.targetY!=y){row.targetY=y;row.node->stopActionByTag(7414);auto* move=CCEaseSineOut::create(CCMoveTo::create(.30f,{8,y}));move->setTag(7414);row.node->runAction(move);}
        auto const& name=value.name;
        row.rank.text(survivalOrdinal(value.rank));row.name.text(name);row.score.text(rankedScoreText(row.tween.displayed,deathmatch)+"%");
        for(auto const& label:{row.rank,row.name,row.score})label.color(color(value));
        row.rank.fit(31,scale,scale*.8f);row.score.fit(deathmatch?58:42,scale,scale*.8f);
        auto const rankWidth=row.rank.foreground->getContentSize().width*row.rank.foreground->getScale();
        auto const scoreWidth=row.score.foreground->getContentSize().width*row.score.foreground->getScale();
        row.name.fit(std::max(32.0f,width-rankWidth-scoreWidth-8),scale,scale*.7f);
        auto const nameWidth=row.name.foreground->getContentSize().width*row.name.foreground->getScale();
        textWidth=std::max(textWidth,rankWidth+nameWidth+scoreWidth+8);
        row.rank.position({0,-1});row.name.position({rankWidth+4,-1});row.score.position({rankWidth+4+nameWidth+4,-1});
    }
    m_cutoffLine->clear();auto const boundary=survivalHudCutoffBoundary(rows);
    if(boundary>0) {
        auto const y=top-static_cast<float>(boundary)*gap+gap*.15f;
        m_cutoffLine->drawSegment({8,y},{8+textWidth+3,y},.65f,{1,.32f,.41f,.95f});
    }
    auto personal=std::find_if(view.standings.begin(),view.standings.end(),[&](auto const& p){return p.id==view.ownId;});
    if(personal!=view.standings.end())m_personalBest.observe(personal->score);
    m_personalBest.advance(dt);
    presentProgressMarkers(view);
    auto own=std::find_if(view.standings.begin(),view.standings.end(),[&](auto const& p){return p.id==view.ownId;});
    auto const observer=SurvivalRuntime::get().observing();
    auto const rankVisible=!observer && !view.ownEliminated && view.state=="PLAYING" && own!=view.standings.end();
    m_rank.foreground->setVisible(rankVisible);m_rank.shadow->setVisible(rankVisible);
    if(observer){m_waiting.text("");m_difference.text(view.cutoff?rankedScoreText(*view.cutoff,deathmatch)+"%":"--%");m_difference.color(Cyan);}
    if(own!=view.standings.end() && !observer) {
        m_rank.text(survivalOrdinal(own->rank));m_rank.color(color(*own));
        if(m_lastRank!=own->rank){m_lastRank=own->rank;for(auto* label:{m_rank.foreground,m_rank.shadow}) {
            label->stopAllActions();label->runAction(CCSequence::create(
                CCEaseBackOut::create(CCScaleTo::create(.14f,fontScale(label,2.10f))),CCEaseSineInOut::create(CCScaleTo::create(.24f,fontScale(label,1.68f))),nullptr));}}
        auto const reference=view.roundNumber>=4?view.first:view.cutoff;
        if(reference){auto const delta=deathmatch?own->score-*reference:std::trunc(own->score)-std::trunc(*reference);
            m_difference.text(rankedScoreText(delta,deathmatch,true));m_difference.color(color(*own));}
        else {m_difference.text("-");m_difference.color(color(*own));}
        m_waiting.text(own->waiting?i18n::text("survival-attempts-finished","ATTEMPTS COMPLETE · WAITING FOR RESULTS"):"");
    }
    m_round.text(deathmatch?"DEATH MATCH":view.phase=="FINAL"?i18n::text("final","FINAL"):fmt::format("{} {}",i18n::text("round","ROUND"),view.roundNumber));
    auto const seconds=(std::max<std::int64_t>(0,remainingMs)+999)/1000;m_timer.text(fmt::format("{:02}:{:02}",seconds/60,seconds%60));
    auto const clockWidth=m_timer.foreground->getContentSize().width*m_timer.foreground->getScale();
    auto const deltaWidth=m_difference.foreground->getContentSize().width*m_difference.foreground->getScale();
    auto const pair=competitiveClockPair(size.width/2,clockWidth,deltaWidth);
    m_timer.position({pair.clockX,layoutHud(size.width,size.height).topY-18});
    m_difference.position({pair.deltaX,layoutHud(size.width,size.height).topY-18});
}
void SurvivalHud::presentProgressMarkers(SurvivalMatchView const& view) {
    auto bar=m_progressBar.lock();auto fill=m_progressFill.lock();auto lines=m_progressLines.lock();
    if(!lines)return;
    lines->clear();lines->setVisible(bar && fill && bar->isVisible() && fill->isVisible());
    if(!bar || !fill)return;
    auto const anchor=fill->getAnchorPoint();auto const origin=fill->getAnchorPointInPoints();
    auto draw=[&](double score,float radius,ccColor4F tint,float capDepth) {
        auto const x=origin.x+survivalProgressMarkerX(score,m_progressWidth)-anchor.x*m_progressWidth;
        auto const y=origin.y-anchor.y*m_progressHeight;
        auto const lower=bar->convertToNodeSpace(fill->convertToWorldSpace({x,y-1}));
        auto const upper=bar->convertToNodeSpace(fill->convertToWorldSpace({x,y+m_progressHeight+1}));
        lines->drawSegment(lower,upper,radius,tint);
        // Separate lower caps keep cutoff and leader readable at the same score.
        auto const left=bar->convertToNodeSpace(fill->convertToWorldSpace({x-2,y-capDepth}));
        auto const right=bar->convertToNodeSpace(fill->convertToWorldSpace({x+2,y-capDepth}));
        lines->drawSegment(left,right,.65f,tint);
    };
    if(view.phase!="FINAL" && view.phase!="TIEBREAK" && view.cutoff)draw(*view.cutoff,.95f,{1,.32f,.41f,1},2);
    if(!view.standings.empty())draw(view.standings.front().score,.5f,{1,.83f,.31f,1},4);
    auto const hasOwn=std::any_of(view.standings.begin(),view.standings.end(),[&](auto const& row){return row.id==view.ownId;});
    if(auto* game=GameManager::sharedState();game && hasOwn && view.phase!="FINAL" && view.phase!="TIEBREAK" && !SurvivalRuntime::get().observing()) {
        auto const tint=game->colorForIdx(game->getPlayerColor());
        auto const x=origin.x+survivalProgressMarkerX(m_personalBest.displayed,m_progressWidth)-anchor.x*m_progressWidth;
        auto const y=origin.y-anchor.y*m_progressHeight;
        auto lower=bar->convertToNodeSpace(fill->convertToWorldSpace({x,y-3}));
        auto upper=bar->convertToNodeSpace(fill->convertToWorldSpace({x,y+m_progressHeight+3}));
        lines->drawSegment(lower,upper,.8f,{0,0,0,1});
        lines->drawSegment(lower,upper,.55f,{tint.r/255.f,tint.g/255.f,tint.b/255.f,1});
        auto capLeft=bar->convertToNodeSpace(fill->convertToWorldSpace({x-2,y+m_progressHeight+3}));
        auto capRight=bar->convertToNodeSpace(fill->convertToWorldSpace({x+2,y+m_progressHeight+3}));
        lines->drawSegment(capLeft,capRight,.65f,{tint.r/255.f,tint.g/255.f,tint.b/255.f,1});
    }
}
} // namespace corum::ranked::ui
