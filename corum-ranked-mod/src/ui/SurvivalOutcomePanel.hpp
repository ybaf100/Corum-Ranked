#include "SurvivalRoundBadge.hpp"
#include "../SurvivalSpectatorRuntime.hpp"
#pragma once
#include "../domain/SurvivalRoundPresentation.hpp"
#include "../SurvivalRuntime.hpp"
#include "PlayerIcon.hpp"
#include "RankBadgeAssets.hpp"
#include "SurvivalProfile.hpp"
#include "ModeLobbyMotion.hpp"
#include "AchievementEffects.hpp"
#include "../domain/AchievementStyle.hpp"
#include "../domain/ScoreDisplay.hpp"
#include <Geode/cocos/misc_nodes/CCClippingNode.h>
#include "../domain/SurvivalBoardPresentation.hpp"
#include "../domain/SurvivalPodiumMotion.hpp"
#include "../domain/SurvivalBoardPresentation.hpp"
#include <Geode/ui/Popup.hpp>
#include <functional>
namespace corum::ranked::ui {
// All result surfaces and the local observer board share the same server roster.
inline std::vector<SurvivalStanding> survivalRoster(SurvivalMatchView const& view){
    std::vector<SurvivalStanding> rows;
    auto const results=view.result["standings"].asArray().unwrapOr(std::vector<matjson::Value>{});
    auto const participants=view.participants.asArray().unwrapOr(std::vector<matjson::Value>{});
    for(auto const& p:participants){
        SurvivalStanding row;row.id=p["participantId"].asString().unwrapOr("");row.name=p["name"].asString().unwrapOr(row.id);
        row.bot=p["kind"].asString().unwrapOr("")=="BOT";row.accountId=geode::utils::numFromString<int>(p["gdAccountId"].asString().unwrapOr("0")).unwrapOr(0);
        row.rank=static_cast<int>(p["finalRank"].asInt().unwrapOr(0));row.tier=p["displayedTier"].asString().unwrapOr("UNRANKED");row.eliminated=!p["eliminationStage"].isNull();
        for(auto const& live:view.roundPlayers)if(live.id==row.id){auto const finalRank=row.rank;row=live;if(finalRank>0)row.rank=finalRank;break;}
        for(auto const& result:results)if(result["participantId"].asString().unwrapOr("")==row.id){
            if(result["rank"].isNumber())row.rank=static_cast<int>(result["rank"].asInt().unwrapOr(0));
            if(result["tierAfter"].isString())row.tier=result["tierAfter"].asString().unwrapOr("UNRANKED");
        }
        rows.push_back(std::move(row));
    }
    std::stable_sort(rows.begin(),rows.end(),[](auto const& a,auto const& b){return (a.rank>0?a.rank:100)<(b.rank>0?b.rank:100);});
    auto const maximum=view.matchPlayerCount==64?64u:16u;
    if(rows.size()>maximum)rows.resize(maximum);
    return rows;
}
inline CCNode* survivalAvatar(SurvivalStanding const& row,std::string const& own,float extent){
    auto* icon=row.id==own?PlayerIcon::createLocal():row.bot?PlayerIcon::createBot(static_cast<int>(std::hash<std::string>{}(row.id)%15+1)):PlayerIcon::createRemote(row.accountId);
    if(icon)icon->setScale(extent/48.f);return icon;
}
class SurvivalStandingsPopup final:public geode::Popup {
    struct Card {SurvivalStanding row;CCLabelBMFont* title=nullptr;CCLabelBMFont* progress=nullptr;CCNode* red=nullptr;SurvivalProgressTween tween;CCMenuItemSpriteExtra* item=nullptr;CCNode* face=nullptr;SurvivalHudColor accent{};};
    std::vector<Card> m_cards;std::string m_match;
    CCLabelBMFont *m_clock=nullptr,*m_pageLabel=nullptr;
    bool m_compact=false,m_reduced=false;std::size_t m_page=0,m_pages=1,m_pageSize=16;
    bool init() override{
        if(!Popup::init(490,282))return false;
        m_bgSprite->setVisible(false);setID("survival-standings-popup"_spr);
        auto const size=CCDirector::sharedDirector()->getWinSize();m_mainLayer->setScale(std::min({1.f,(size.width-16)/490,(size.height-16)/282}));
        m_mainLayer->addChild(surface({490,282},{245,141},Steel,Surface,255,true));
        auto& runtime=SurvivalRuntime::get();m_match=runtime.view().id;
        m_clock=menuLabel("SURVIVAL",.30f,{245,264},Accent,FontWeight::Black);m_mainLayer->addChild(m_clock,5);
        auto const rows=survivalRoster(runtime.view());
        auto const density=survivalPhaseCardDensity(runtime.view().matchPlayerCount,runtime.view().phase);
        m_compact=density==SurvivalCardDensity::Compact;m_reduced=density==SurvivalCardDensity::Reduced;
        m_pageSize=m_compact?32:m_reduced?24:16;m_pages=std::max<std::size_t>(1,(rows.size()+m_pageSize-1)/m_pageSize);
        for(std::size_t i=0;i<rows.size();++i){
            auto const& row=rows[i];float const height=m_compact?21.f:m_reduced?32.f:51.f;
            auto* face=surface({112,height},{},Steel,SurfaceRaised,250,true);
            auto* red=surface({112,height},{56,height/2},Red,{42,13,28},255,true);face->addChild(red,1);
            auto* name=playerNameLabel(row.name,.22f,m_compact?CCPoint{68,10.5f}:m_reduced?CCPoint{67,23}:CCPoint{56,43},Text,FontWeight::Black);face->addChild(name,5);
            if(auto* icon=survivalAvatar(row,runtime.view().ownId,15)){icon->setPosition(m_compact?CCPoint{13,10.5f}:m_reduced?CCPoint{13,16}:CCPoint{44,26});face->addChild(icon,5);}
            CCLabelBMFont* progress=nullptr;
            if(!m_compact){
                if(!m_reduced){auto* badge=createAnimatedRankBadge(row.tier,{17,17},RankBadgeDisplay::Leaderboard);badge->setPosition({68,26});face->addChild(badge,5);}
                progress=menuLabel("",.16f,m_reduced?CCPoint{67,9}:CCPoint{56,9},TextMuted,FontWeight::ExtraBold);face->addChild(progress,5);
            }
            auto const cell=i%m_pageSize;
            auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalStandingsPopup::onPlayer));item->setTag(static_cast<int>(i));
            item->setPosition({65.f+120*(cell%4),m_compact?237.f-25*(cell/4):m_reduced?233.f-36*(cell/4):224.f-56*(cell/4)});m_buttonMenu->addChild(item);
            m_cards.push_back({row,name,progress,red,{},item,face});
        }
        if(m_pages>1){
            m_pageLabel=menuLabel("",.24f,{245,22},TextMuted);m_mainLayer->addChild(m_pageLabel,5);
            for(int direction:{-1,1}){
                auto* face=surface({44,24},{},Steel,SurfaceRaised,255,true);face->addChild(menuLabel(direction<0?"<":">",.28f,{22,12},Text),5);
                auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalStandingsPopup::onPage));item->setTag(direction);item->setPosition({245.f+direction*64,22});m_buttonMenu->addChild(item);
            }
        }
        if(runtime.officialRoundCleared() && !runtime.freePlaying()){
            auto* face=surface({112,22},{},Accent,SurfaceRaised,255,true);face->addChild(menuLabel(i18n::text("survival-watch","WATCH"),.22f,{56,11},Accent));
            auto* watch=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalStandingsPopup::onWatch));watch->setPosition({411,22});m_buttonMenu->addChild(watch);
        }
        applyPage();scheduleUpdate();update(0);return true;
    }
    void applyPage(){
        for(std::size_t i=0;i<m_cards.size();++i){auto const shown=i/m_pageSize==m_page;m_cards[i].item->setVisible(shown);m_cards[i].item->setEnabled(shown);}
        if(m_pageLabel)m_pageLabel->setString(fmt::format("{} / {}",m_page+1,m_pages).c_str());
    }
    void onPage(CCObject* sender){if(auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(sender)){m_page=item->getTag()<0?(m_page+m_pages-1)%m_pages:(m_page+1)%m_pages;applyPage();}}
    void onWatch(CCObject*){onClose(nullptr);SurvivalSpectatorRuntime::get().openTargets();}
    void onPlayer(CCObject* sender){if(auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(sender);item && item->getTag()>=0 && static_cast<std::size_t>(item->getTag())<m_cards.size())openSurvivalProfile(this,m_cards[item->getTag()].row);}
public:
    static SurvivalStandingsPopup* create(){auto* p=new SurvivalStandingsPopup;if(p->init()){p->autorelease();return p;}delete p;return nullptr;}
    void update(float dt) override{
        auto& r=SurvivalRuntime::get();r.tick();auto const& v=r.view();if(v.id!=m_match){onClose(nullptr);return;}
        auto const rows=survivalRoster(v);
        auto const remaining=r.roundRemainingMs();auto const seconds=remaining?(*remaining+999)/1000:0;
        m_clock->setString(fmt::format("SURVIVAL · {} · {} · {}",survivalRoundLabel(v.phase,v.roundNumber),remaining?fmt::format("{:02}:{:02}",seconds/60,seconds%60):"--:--",v.cutoff?rankedScoreText(*v.cutoff,v.phase=="TIEBREAK")+"%":"--%").c_str());fitFullMenuLabel(m_clock,440,18,.30f);
        for(auto& card:m_cards){auto it=std::find_if(rows.begin(),rows.end(),[&](auto const& row){return row.id==card.row.id;});if(it==rows.end())continue;card.row=*it;
            auto const& row=card.row;
            if(m_compact){card.title->setString(row.name.c_str());fitSingleLineMenuLabel(card.title,80,13,.22f);}
            else {
                card.tween.observe(v.revision,row.attemptId,row.attemptActive,row.bestProgress,row.currentProgress);card.tween.advance(dt);
                card.title->setString(fmt::format("{} {}",row.rank>0?survivalOrdinal(row.rank):"--",row.name).c_str());fitSingleLineMenuLabel(card.title,m_reduced?80:102,12,.22f);
                card.progress->setString(fmt::format("BEST {}% · NOW {}%",rankedScoreText(card.tween.best.displayed,false),rankedScoreText(card.tween.now,false)).c_str());fitSingleLineMenuLabel(card.progress,m_reduced?80:104,11,.16f);
            }
            auto const tint=row.eliminated?survivalRankColor(row.rank,false):row.rank>0?survivalRankColor(row.rank,row.passing):SurvivalHudColor{77,135,255};
            if(card.accent!=tint){card.accent=tint;setSurfaceAccent(card.face,{tint.r,tint.g,tint.b});}
            card.red->setVisible(row.eliminated || (row.rank>0 && !row.passing && v.state=="PLAYING"));
        }
    }
};
inline void showSurvivalStandings(){
    auto* scene=CCDirector::sharedDirector()->getRunningScene();if(!scene || scene->getChildByIDRecursive("survival-standings-popup"_spr))return;
    if(auto* popup=SurvivalStandingsPopup::create())popup->show();
}
class SurvivalOutcomePanel final:public cocos2d::CCLayerColor {
    std::function<void(int)> m_action;CCMenu* m_menu=nullptr;CCMenuItemSpriteExtra* m_watch=nullptr;CCMenuItemSpriteExtra* m_freeChoice=nullptr;CCMenuItemSpriteExtra* m_resultChoice=nullptr;CCMenuItemSpriteExtra* m_close=nullptr;
    std::string m_key,m_identity;std::vector<SurvivalStanding> m_profiles;
    struct Podium {CCNode* pedestal;CCMenuItemSpriteExtra* avatar;CCNode* stamp;CCNode* place;CCNode* glow;float x,base,height;int rank;};
    // Pedestals lean 8° (SkewX): a point at height y sits y·tan 8° to the right.
    static constexpr float kPodiumLean=0.14054083f;
    std::vector<CCNode*> m_badges;
    std::string m_badgeMatch;std::size_t m_badgePage=0,m_badgeCount=0;
    std::vector<Podium> m_stages;double m_elapsed=0;
    CCLabelBMFont *m_watchText=nullptr,*m_detail=nullptr;
    CCNode* m_halo=nullptr;
    // Personal result: the lime header is revealed by a diagonal wipe from the
    // left (a sheared stencil sliding right), driven by m_elapsed so a rebuild
    // of the same stage does not replay it.
    CCNode* m_headerWipe=nullptr;float m_headerWipeFrom=0,m_headerWipeTo=0;
    void onAction(CCObject* object){if(auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(object);item && item->isEnabled()){
        auto const& current=SurvivalRuntime::get().view();if(!m_identity.starts_with(current.id+":"+current.ownId+":"))return;
        auto tag=item->getTag();if(tag>=100){if(static_cast<std::size_t>(tag-100)<m_profiles.size())openSurvivalProfile(this,m_profiles[tag-100]);}
        else if(tag==31||tag==32){
            // S6 round table pages; the render clamps the page to the rows.
            m_badgePage=tag==31?(m_badgePage>0?m_badgePage-1:0):m_badgePage+1;
            m_key.clear();m_elapsed=0;
        }else if(tag==11)showSurvivalStandings();else if(m_action)m_action(tag);
    }}
    // Board label scale: CSS px of the 1136x640 boards / 64.
    static float css(float value){return value/64.f;}
    // .btn slabs: filled lime for the primary action, outlined (ghost) else.
    CCMenuItemSpriteExtra* button(std::string text,CCPoint at,float width,int tag,bool filled=false,float height=21,float cssPx=16){
        auto* face=slab({width,height},{},filled?AccentFill:ccc3(58,63,78),!filled,Ink);
        auto* label=menuLabel(text,css(cssPx),{width/2,height/2},filled?AccentInk:Text,filled?FontWeight::Black:FontWeight::ExtraBold);fitFullMenuLabel(label,width-12,height-6,css(cssPx));face->addChild(label,5);
        auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalOutcomePanel::onAction));item->setPosition(at);item->setTag(tag);m_menu->addChild(item);if(tag==13)m_watchText=label;return item;
    }
    // .hdr: the .num chip, the leaning brand and the caption after it.
    void header(std::string const& number,std::string const& brand,std::string const& caption){
        auto const size=CCDirector::sharedDirector()->getWinSize();auto const y=size.height-18;
        auto* num=menuLabel(number,css(15),{0,0},AccentInk,FontWeight::Black);auto const numW=num->getScaledContentSize().width+8;
        auto* chip=CCLayerColor::create({AccentFill.r,AccentFill.g,AccentFill.b,255},numW,12);chip->setPosition({14,y-6});addChild(chip,5);
        num->setPosition({14+numW/2,y});addChild(num,6);
        auto* title=menuLabel(brand,css(30),{14+numW+8,y},Text,FontWeight::Black);title->setAnchorPoint({0,.5f});leanLabel(title);fitMenuLabelWidth(title,size.width*.5f,css(30),.2f);addChild(title,6);
        auto* cap=menuLabel(caption,css(13),{14+numW+8+title->getScaledContentSize().width+8,y},TextMuted,FontWeight::SemiBold);cap->setAnchorPoint({0,.5f});fitMenuLabelWidth(cap,size.width*.35f,css(13),.1f);addChild(cap,6);
    }
public:
    static SurvivalOutcomePanel* create(std::function<void(int)> action){auto* p=new SurvivalOutcomePanel;if(p->initWithColor({12, 13, 17,255})){p->m_action=std::move(action);p->setID("survival-outcome-panel"_spr);p->autorelease();return p;}delete p;return nullptr;}
    void present(SurvivalMatchView const& view,SurvivalFlowStage stage,float dt){
        auto const badgeMatch=view.id+":"+view.ownId;if(m_badgeMatch!=badgeMatch){m_badgeMatch=badgeMatch;m_badgePage=m_badgeCount=0;}
        auto const choice=stage==SurvivalFlowStage::EliminatedChoice,ceremony=stage==SurvivalFlowStage::Ceremony;
        auto const key=view.id+":"+view.ownId+":"+(choice?view.roundKey+":"+std::to_string(view.mapGeneration):std::string{})+":"+std::to_string(static_cast<int>(stage));
        // Settlement arriving updates the labels/tiers without resetting a podium animation.
        auto const settled=view.result.isObject();std::string buildKey=key+(settled?":settled":":pending")+":"+std::to_string(view.result["actualDelta"].asInt().unwrapOr(0))+":badges:"+std::to_string(m_badgePage);
        for(auto const& p:view.result["standings"].asArray().unwrapOr(std::vector<matjson::Value>{}))
            buildKey+=":"+p["participantId"].asString().unwrapOr("")+":"+p["tierAfter"].asString().unwrapOr("")+":"+(p["lpAfter"].isNumber()?std::to_string(p["lpAfter"].asInt().unwrapOr(0)):"pending")+":"+std::to_string(p["delta"].asInt().unwrapOr(0));
        auto const size=CCDirector::sharedDirector()->getWinSize();
        if(m_key!=buildKey){auto const elapsed=m_identity==key?m_elapsed:0.;m_identity=key;m_key=buildKey;removeAllChildrenWithCleanup(true);m_profiles=survivalRoster(view);m_stages.clear();m_badges.clear();m_elapsed=elapsed;m_watch=nullptr;m_freeChoice=m_resultChoice=m_close=nullptr;m_detail=m_watchText=nullptr;m_halo=nullptr;m_headerWipe=nullptr;
            m_menu=CCMenu::create();m_menu->setPosition(CCPointZero);addChild(m_menu,20);
            auto const own=std::find_if(m_profiles.begin(),m_profiles.end(),[&](auto const& row){return row.id==view.ownId;});
            if(choice){
                // S4 board: the red band leaning up to the right with the round
                // line and the 96 px ELIMINATED, the own row under it, and the
                // ruled panel with KEEP PLAYING / PERSONAL RESULT at the bottom.
                auto* band=CCLayerColor::create({Red.r,Red.g,Red.b,255},size.width+60,75);band->ignoreAnchorPointForPosition(false);band->setAnchorPoint({.5f,.5f});
                band->setPosition({size.width/2,size.height-60-37.5f});band->setSkewY(3);band->setID("survival-eliminated-band"_spr);addChild(band,1);
                auto const roundLine=own!=m_profiles.end() && own->rank>0?fmt::format("ROUND {} · {}",std::max(1,view.roundNumber),survivalOrdinal(own->rank)):fmt::format("ROUND {}",std::max(1,view.roundNumber));
                auto* line=menuLabel(roundLine,css(16),{size.width/2,size.height-64-5},Ink,FontWeight::Black);fitMenuLabelWidth(line,size.width-40,css(16),.12f);addChild(line,5);
                auto* heading=menuLabel(i18n::text("survival-eliminated","ELIMINATED"),css(96),{size.width/2,size.height-64-10-24},Ink,FontWeight::Black);
                fitMenuLabelWidth(heading,size.width-40,css(96),.6f);heading->setID("survival-eliminated-heading"_spr);addChild(heading,5);
                if(own!=m_profiles.end()){
                    auto const rowY=size.height-150-14;
                    auto* name=playerNameLabel(own->name,css(34),{0,0},Text,FontWeight::Black);fitFullMenuLabel(name,200,20,css(34));
                    auto const total=40+7+std::max(name->getScaledContentSize().width,120.f);auto const left=size.width/2-total/2;
                    if(auto* icon=survivalAvatar(*own,view.ownId,30)){icon->setPosition({left+20,rowY});propagateModeOpacity(icon,153);addChild(icon,5);}
                    name->setAnchorPoint({0,.5f});name->setPosition({left+47,rowY+6});addChild(name,5);
                    auto const detail=fmt::format("{} · {:.1f}% · {}",own->rank>0?survivalOrdinal(own->rank):std::string("--"),own->bestProgress,
                        settled?i18n::text("survival-rank-final","RANK FINAL"):i18n::text("survival-lp-applying","APPLYING LP..."));
                    auto* cap=menuLabel(detail,css(13),{left+47,rowY-9},TextMuted,FontWeight::SemiBold);cap->setAnchorPoint({0,.5f});fitMenuLabelWidth(cap,220,css(13),.1f);addChild(cap,5);
                }
                constexpr float panelW=310,panelH=70;auto const panelX=size.width/2-panelW/2,panelY=20.f;
                auto* panel=CCLayerColor::create({Surface.r,Surface.g,Surface.b,255},panelW,panelH);panel->setPosition({panelX,panelY});addChild(panel,2);
                auto* rule=CCLayerColor::create({Accent.r,Accent.g,Accent.b,255},panelW,1);rule->setPosition({panelX,panelY+panelH-1});addChild(rule,3);
                auto* goes=menuLabel(i18n::text("survival-match-continues","THE MATCH GOES ON"),css(18),{panelX+11,panelY+panelH-14},Text,FontWeight::Black);goes->setAnchorPoint({0,.5f});addChild(goes,5);
                auto const inner=panelW-22-6,playW=inner*1.3f/2.3f,resultW=inner-playW,buttonY=panelY+panelH-14-9-6-10.5f;
                m_freeChoice=button(i18n::text("survival-continue-play","KEEP PLAYING"),{panelX+11+playW/2,buttonY},playW,18,true);
                if(settled)button(i18n::text("survival-view-result","VIEW RESULT"),{panelX+11+playW+6+resultW/2,buttonY},resultW,21);
                else{auto* wait=button(i18n::text("survival-view-result","VIEW RESULT"),{panelX+11+playW+6+resultW/2,buttonY},resultW,-1);grayButtonVisual(wait->getNormalImage());wait->setEnabled(false);}
                auto* detail=menuLabel(i18n::text("survival-continue-detail","The match goes on. Keep playing this map without official records."),css(13),{panelX+11,panelY+8},TextMuted,FontWeight::SemiBold);
                detail->setAnchorPoint({0,.5f});fitMenuLabelWidth(detail,panelW-22,css(13),.09f);addChild(detail,5);
            }else if(ceremony){
                // S5 board: header, three pedestals (2 · 1 · 3) on the footer
                // line with the tier, LP and the own promotion state, and
                // CONTINUE · MY RESULT. The rise / drop / stamp motion is kept.
                header("01",i18n::text("survival-podium","FINAL PODIUM"),fmt::format("SURVIVAL · {} · {}",
                    fmt::format(fmt::runtime(i18n::text("players-count-format","{} PLAYERS")),std::max(view.matchPlayerCount,static_cast<int>(m_profiles.size()))),
                    fmt::format(fmt::runtime(i18n::text("survival-rounds-count","{} ROUNDS")),std::max(1,view.roundNumber))));
                std::array<int,3> ranks{2,1,3};auto const base=46.f;
                for(int n=0;n<3;++n){int rank=ranks[n];auto it=std::find_if(m_profiles.begin(),m_profiles.end(),[&](auto const& p){return p.rank==rank;});if(it==m_profiles.end())continue;
                    auto const width=rank==1?150.f:135.f;auto const h=rank==1?145.f:rank==2?115.f:100.f;
                    auto const x=size.width/2+(n==0?-(75.f+9.f+67.5f):n==2?75.f+9.f+67.5f:0.f);
                    auto const tint=rank==1?Gold:rank==2?ccc3(201,215,235):ccc3(208,137,70);
                    auto const ground=rank==1?ccc3(42,38,20):SurfaceRaised;
                    // .sk pedestal: a leaning fill with a 4 px top rule in the medal colour.
                    auto* pedestal=CCNode::create();pedestal->setContentSize({width,h});pedestal->setAnchorPoint({.5f,0});pedestal->setPosition({x,base});addChild(pedestal,5);
                    auto* fill=CCLayerColor::create({ground.r,ground.g,ground.b,255},width,h);fill->setSkewX(8);pedestal->addChild(fill,0);
                    // SkewX shifts a point right by y·tan 8°, so the fill's top edge sits
                    // (h-2)·tan 8° to the right of its base. The rule (only 2 pt tall) and
                    // everything stacked on the pedestal follow that lean.
                    constexpr float lean=kPodiumLean;
                    auto* top=CCLayerColor::create({tint.r,tint.g,tint.b,255},width,2);top->setPosition({(h-2)*lean,h-2});top->setSkewX(8);pedestal->addChild(top,1);
                    // A plain node has no fill and respects its anchor. A transparent CCLayerColor
                    // ignores that anchor by default; fading its own opacity also paints a black slab.
                    auto* stamp=CCNode::create();stamp->setContentSize({width,h});stamp->setAnchorPoint({.5f,.5f});stamp->setPosition({x,base+h/2});addChild(stamp,8);
                    std::string after;std::optional<std::int64_t> lpAfter,delta;
                    for(auto const& p:view.result["standings"].asArray().unwrapOr(std::vector<matjson::Value>{}))if(p["participantId"].asString().unwrapOr("")==it->id){
                        after=p["tierAfter"].asString().unwrapOr("");if(p["lpAfter"].isNumber())lpAfter=p["lpAfter"].asInt().unwrapOr(0);
                        if(p["delta"].isNumber())delta=p["delta"].asInt().unwrapOr(0);
                        if(it->id==view.ownId && view.result["actualDelta"].isNumber())delta=view.result["actualDelta"].asInt().unwrapOr(0);
                    }
                    auto const placeY=h-5-(rank==1?17.f:14.f);
                    if(!after.empty()){
                        auto const extent=rank==1?34.f:28.f;auto* badge=createAnimatedRankBadge(after,{extent,extent},RankBadgeDisplay::Leaderboard);badge->setPosition({width/2,placeY-(rank==1?17.f:14.f)-4-extent/2});stamp->addChild(badge,5);
                        auto* tier=menuLabel(after,css(15),{width/2,placeY-(rank==1?17.f:14.f)-8-extent-5},tierColor(after),FontWeight::Black);fitFullMenuLabel(tier,width-12,10,css(15));stamp->addChild(tier,5);
                    }
                    auto const extent=rank==1?34.f:28.f;
                    auto const score=lpAfter?(delta?fmt::format("{} LP {:+}",*lpAfter,*delta):fmt::format("{} LP",*lpAfter)):i18n::text("survival-result-wait","PENDING");
                    auto* points=menuLabel(score,css(15),{width/2,placeY-(rank==1?17.f:14.f)-8-extent-19},lpAfter?Text:TextMuted,FontWeight::ExtraBold);fitFullMenuLabel(points,width-12,10,css(15));points->setID("survival-podium-lp-after"_spr);stamp->addChild(points,5);
                    auto* face=CCNode::create();face->setContentSize({width,70});if(auto* icon=survivalAvatar(*it,view.ownId,rank==1?44.f:34.f)){icon->setPosition({width/2,rank==1?44.f:42.f});face->addChild(icon,5);}
                    auto const isOwn=it->id==view.ownId;
                    auto* label=playerNameLabel(isOwn?it->name+" · "+i18n::text("you","YOU"):it->name,rank==1?css(34):css(24),{width/2,10},isOwn?Accent:Text,FontWeight::Black);fitFullMenuLabel(label,width-8,16,rank==1?css(34):css(24));face->addChild(label,5);
                    if(rank==1){auto* winnerText=menuLabel(i18n::text("survival-winner","WINNER"),css(15),{0,0},AccentInk,FontWeight::Black);auto const chipW=winnerText->getScaledContentSize().width+12;
                        auto* winner=CCLayerColor::create({Gold.r,Gold.g,Gold.b,255},chipW,12);winner->setPosition({width/2-chipW/2,76});winner->setSkewX(8);face->addChild(winner,5);
                        winnerText->setPosition({width/2,82});face->addChild(winnerText,6);}
                    auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalOutcomePanel::onAction));item->setTag(100+static_cast<int>(it-m_profiles.begin()));item->setPosition({x+h*lean,base+h+36});m_menu->addChild(item);
                    auto* place=menuLabel(std::to_string(rank),rank==1?css(68):css(56),{x+3+placeY*lean,base+placeY},tint,FontWeight::Black);addChild(place,9);
                    // S5: the own pedestal also names the promotion state the server sent for us.
                    if(isOwn){auto const& r=view.result;std::string state;ccColor3B tone=Text;
                        if(r["promotionSucceeded"].asBool().unwrapOr(false)||r["promoted"].asBool().unwrapOr(false)){state=i18n::text("promoted","PROMOTED");tone=Green;}
                        else if(r["promotionFailed"].asBool().unwrapOr(false)){state=i18n::text("promotion-failed","PROMOTION FAILED");tone=Red;}
                        else if(r["promotionStarted"].asBool().unwrapOr(false)){state=i18n::text("promotion-match","PROMOTION MATCH");tone=Gold;}
                        else if(r["demoted"].asBool().unwrapOr(false)){state=i18n::text("demoted","DEMOTED");tone=Red;}
                        if(!state.empty()){auto* chip=menuLabel(state,css(13),{width/2,placeY-(rank==1?17.f:14.f)-8-extent-32},tone,FontWeight::Black);fitFullMenuLabel(chip,width-8,9,css(13));chip->setID("survival-podium-own-state"_spr);stamp->addChild(chip,5);}
                    }
                    // Text inside the pedestal is centred on its leaning middle at each height.
                    for(auto* child:CCArrayExt<CCNode*>(stamp->getChildren()))child->setPositionX(child->getPositionX()+child->getPositionY()*lean);
                    CCNode* glow=nullptr;
                    if(rank==1){glow=createRankAmbientFx("GOLD",{68,68},RankBadgeDisplay::Matched);glow->setPosition({x+h*lean,base+h+46});glow->setID("survival-podium-gold-ambient"_spr);addChild(glow,4);m_halo=glow;}
                    m_stages.push_back({pedestal,item,stamp,place,glow,x,base,h,rank});
                }
            }else if(stage==SurvivalFlowStage::ScoreAnimation){
                addChild(menuLabel(i18n::text("settlement-pending","SETTLEMENT PENDING"),css(20),{size.width/2,size.height/2},TextMuted,FontWeight::Black));
                button(i18n::text("retry","RETRY"),{size.width/2,44},132,12);
            }else{
                // S6 board: the lime band (bottom edge rising to the right) with
                // the place, the round table on the left and the hatched LP
                // panel on the right.
                CCNode* header=this;
                {   // The bottom edge runs from the left corner up 20 points to the right
                    // as one straight line: a single band sheared along Y (it used to be
                    // 2 pt strips of growing width, which read as a staircase). The
                    // sheared top edge rises past the top of the screen.
                    constexpr float bandH=75,cut=20;
                    auto* band=CCLayerColor::create({AccentFill.r,AccentFill.g,AccentFill.b,255},size.width,bandH);
                    band->setAnchorPoint({0,0});band->setPosition({0,size.height-bandH});
                    band->setSkewY(std::atan2(cut,size.width)*57.29578f);
                    // Diagonal wipe: a stencil leaning 30° whose leading edge crosses
                    // the header from left to right, drawing band and text together.
                    constexpr float wipeH=bandH+cut+12,lean=0.57735027f; // tan 30°
                    auto const wipeW=size.width+2*wipeH*lean+8;
                    auto* stencil=CCLayerColor::create({255,255,255,255},wipeW,wipeH);
                    stencil->setAnchorPoint({0,0});stencil->setSkewX(30);
                    m_headerWipeTo=-wipeH*lean-4;m_headerWipeFrom=m_headerWipeTo-wipeW;
                    stencil->setPosition({m_headerWipeFrom,size.height-wipeH});
                    auto* clip=CCClippingNode::create(stencil);clip->setID("survival-result-header-wipe"_spr);addChild(clip,1);
                    m_headerWipe=stencil;header=clip;
                    clip->addChild(band,1);
                }
                auto* eyebrow=menuLabel(fmt::format("SURVIVAL · {}",i18n::text("survival-personal-result","PERSONAL RESULT")),css(15),{14,size.height-8-5},AccentInk,FontWeight::Black);eyebrow->setAnchorPoint({0,.5f});header->addChild(eyebrow,5);
                auto const placeText=own!=m_profiles.end() && own->rank>0?survivalOrdinal(own->rank):i18n::text("survival-result-wait","PENDING");
                auto* placeLabel=menuLabel(placeText,css(84),{14,size.height-8-12-21},AccentInk,FontWeight::Black);placeLabel->setAnchorPoint({0,.5f});fitMenuLabelWidth(placeLabel,200,css(84),.5f);header->addChild(placeLabel,5);
                auto const field=std::max(view.matchPlayerCount,static_cast<int>(m_profiles.size()));
                auto* ofField=menuLabel(fmt::format(fmt::runtime(i18n::text("survival-of-field","OF {}")),field),css(28),{14+placeLabel->getScaledContentSize().width+8,size.height-8-12-27},AccentInk,FontWeight::Black);
                ofField->setAnchorPoint({0,.5f});fitMenuLabelWidth(ofField,size.width*.4f,css(28),.2f);header->addChild(ofField,5);
                if(own!=m_profiles.end()){
                    // The own icon on the band opens the profile (as before).
                    auto* face=CCNode::create();face->setContentSize({40,40});if(auto* icon=survivalAvatar(*own,view.ownId,30)){icon->setPosition({20,20});face->addChild(icon,5);}
                    auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalOutcomePanel::onAction));item->setTag(100+static_cast<int>(own-m_profiles.begin()));item->setPosition({size.width-14-20,size.height-8-20});m_menu->addChild(item);
                }
                // Right: the hatched LP panel (trophy, tier, +LP, before -> after).
                constexpr float panelW=175;auto const panelX=size.width-14-panelW,panelTop=size.height-75-3,panelBottom=46.f+4;
                auto* panel=CCLayerColor::create({Surface.r,Surface.g,Surface.b,255},panelW,panelTop-panelBottom);panel->setPosition({panelX,panelBottom});addChild(panel,2);
                auto const tierAfter=view.result["profileAfter"]["tier"].asString().unwrapOr("UNRANKED");
                if(settled){auto* badge=createAnimatedRankBadge(tierAfter,{40,40},RankBadgeDisplay::Leaderboard);badge->setPosition({panelX+10+20,panelTop-8-20});addChild(badge,5);}
                auto* tierLabel=menuLabel(settled?tierAfter:i18n::text("settlement-pending","SETTLEMENT PENDING"),css(15),{panelX+10+40+7,panelTop-8-10},settled?tierColor(tierAfter):TextMuted,FontWeight::Black);
                tierLabel->setAnchorPoint({0,.5f});fitMenuLabelWidth(tierLabel,panelW-67,css(15),.1f);addChild(tierLabel,5);
                auto const delta=view.result["actualDelta"].asInt().unwrapOr(0);
                m_detail=menuLabel(settled?fmt::format("{:+} LP",delta):std::string("- LP"),css(48),{panelX+10+40+7,panelTop-8-28},delta<0?Red:Accent,FontWeight::Black);
                m_detail->setAnchorPoint({0,.5f});leanLabel(m_detail);fitMenuLabelWidth(m_detail,panelW-67,css(48),.3f);addChild(m_detail,5);
                if(settled){
                    auto const lpAfter=view.result["profileAfter"]["lp"].asInt().unwrapOr(0);
                    auto* change=menuLabel(fmt::format("{} → {} LP",lpAfter-delta,lpAfter),css(13),{panelX+10,panelBottom+10},TextMuted,FontWeight::SemiBold);
                    change->setAnchorPoint({0,.5f});fitMenuLabelWidth(change,panelW-20,css(13),.09f);addChild(change,5);
                }
            }
            if(stage==SurvivalFlowStage::PersonalResult){
                // S6 left: ROUND / MAP / RANK / RESULT rows from the official
                // round badges (the badge face sits in the RESULT column).
                auto rows=view.roundBadges.asArray().unwrapOr(std::vector<matjson::Value>{});
                std::erase_if(rows,[&](auto const& badge){return badge["participantId"].asString().unwrapOr("")!=view.ownId||!survivalBadgeKnown(badge["grade"].asString().unwrapOr(""));});
                m_badgeCount=rows.size();
                auto const tableX=14.f,tableR=size.width-14-175-10,headY=size.height-75-3-6;
                auto const head=[&](std::string const& text,float x,CCPoint anchor){auto* l=menuLabel(text,css(13),{x,headY},TextMuted,FontWeight::SemiBold);l->setAnchorPoint(anchor);addChild(l,5);};
                head(i18n::text("round-short","ROUND"),tableX+6,{0,.5f});head(i18n::text("map","MAP"),tableX+36,{0,.5f});
                head(i18n::text("rank","RANK"),tableR-56,{1,.5f});head(i18n::text("result","RESULT"),tableR-22,{.5f,.5f});
                // Achievements unlocked by this match sit under the round
                // table in the same row style (at most three rows).
                struct Unlock{std::string name;int tier=0;std::int64_t ap=0;};std::vector<Unlock> unlocks;
                for(auto const& value:view.result["achievementUnlocks"].asArray().unwrapOr(std::vector<matjson::Value>{})){
                    auto const type=value["type"].asString().unwrapOr("");auto const level=static_cast<int>(value["level"].asInt().unwrapOr(0));
                    auto const tier=type=="PROGRESSIVE"?level:achievementRarityTier(value["rarity"].asString().unwrapOr(""));
                    auto name=i18n::text("achievement."+value["achievementId"].asString().unwrapOr("")+".name",value[i18n::isKorean()?"nameKo":"nameEn"].asString().unwrapOr(""));
                    if(name.empty()||tier<1||tier>5)continue;auto const stageLabel=value["displayLabel"].asString().unwrapOr("");if(!stageLabel.empty())name+=" - "+stageLabel;
                    unlocks.push_back({name,tier,value["rewardAp"].asInt().unwrapOr(0)});if(unlocks.size()==3)break;}
                constexpr float rowH=23;auto const achievementBlock=unlocks.empty()?0.f:14.f+static_cast<float>(unlocks.size())*(rowH+2);
                auto const visible=static_cast<std::size_t>(std::max(1.f,(headY-8-46-achievementBlock)/(rowH+2)));
                auto const pages=std::max<std::size_t>(1,(rows.size()+visible-1)/visible);m_badgePage=std::min(m_badgePage,pages-1);
                auto const begin=m_badgePage*visible,count=std::min(visible,rows.size()-std::min(rows.size(),begin));
                for(std::size_t i=0;i<count;++i){auto const& badge=rows[begin+i];auto const y=headY-8-rowH/2-static_cast<float>(i)*(rowH+2);
                    auto* stripe=CCLayerColor::create({SurfaceRaised.r,SurfaceRaised.g,SurfaceRaised.b,255},tableR-tableX,rowH);stripe->setPosition({tableX,y-rowH/2});addChild(stripe,2);
                    auto const label=badge["roundLabel"].asString().unwrapOr("");
                    auto* roundLabel=menuLabel(label,css(15),{tableX+6,y},label=="FINAL"?Gold:TextMuted,FontWeight::Black);roundLabel->setAnchorPoint({0,.5f});addChild(roundLabel,5);
                    auto const difficulty=recordedMapDifficulty(badge["difficulty"].asString().unwrapOr(""));
                    auto const mapText=fmt::format("GD #{}{}",badge["levelId"].asInt().unwrapOr(0),difficulty?fmt::format(" · {}",difficulty->label):std::string{});
                    auto* mapLabel=menuLabel(mapText,css(15),{tableX+36,y},Text,FontWeight::ExtraBold);mapLabel->setAnchorPoint({0,.5f});fitMenuLabelWidth(mapLabel,tableR-tableX-36-90,css(15),.1f);addChild(mapLabel,5);
                    auto const rank=badge["rank"].asInt().unwrapOr(0);
                    auto* rankLabel=menuLabel(rank>0?fmt::format("#{}",rank):std::string("-"),css(15),{tableR-56,y},TextMuted,FontWeight::ExtraBold);rankLabel->setAnchorPoint({1,.5f});addChild(rankLabel,5);
                    auto const survived=badge["grade"].asString().unwrapOr("")!="ELIMINATED";
                    // The pop-in motion scales the holder; the face keeps its row size.
                    if(auto* card=survivalRoundBadge(badge,{rowH+20,rowH+20},false)){auto* holder=CCNode::create();holder->setPosition({tableR-22,y});addChild(holder,5);
                        card->setScale(rowH/(rowH+20));card->setPosition({0,0});holder->addChild(card);m_badges.push_back(holder);}
                    else{auto* result=menuLabel(survived?"PASS":"OUT",css(13),{tableR-22,y},survived?Accent:Red,FontWeight::Black);addChild(result,5);}
                }
                if(pages>1){
                    // On the header line when achievements take the space below.
                    auto const center=(tableX+tableR)/2,pagerY=unlocks.empty()?56.f:headY;
                    auto* caption=menuLabel(fmt::format("{} / {}",m_badgePage+1,pages),css(13),{center,pagerY},TextMuted,FontWeight::ExtraBold);addChild(caption,5);
                    button("<",{center-40,pagerY},24,31,false,16)->setEnabled(m_badgePage>0);
                    button(">",{center+40,pagerY},24,32,false,16)->setEnabled(m_badgePage+1<pages);
                }
                if(!unlocks.empty()){
                    auto const shown=std::max<std::size_t>(1,count);
                    auto y=headY-8-static_cast<float>(shown)*(rowH+2)-8;
                    auto* heading=menuLabel(i18n::text("achievement-unlocked","ACHIEVEMENT UNLOCKED"),css(13),{tableX+6,y},TextMuted,FontWeight::SemiBold);heading->setAnchorPoint({0,.5f});addChild(heading,5);
                    y-=6+rowH/2;
                    for(std::size_t i=0;i<unlocks.size();++i,y-=rowH+2){
                        auto const& unlock=unlocks[i];auto const tone=achievementColor(unlock.tier);
                        // Each row enters from the page's left edge, one after another.
                        auto* row=CCNode::create();row->setPosition({-(tableR+20),0});addChild(row,5);
                        auto* stripe=CCLayerColor::create({SurfaceRaised.r,SurfaceRaised.g,SurfaceRaised.b,255},tableR-tableX,rowH);stripe->setPosition({tableX,y-rowH/2});row->addChild(stripe,2);
                        auto* rule=CCLayerColor::create({tone.r,tone.g,tone.b,255},3,rowH);rule->setPosition({tableX,y-rowH/2});row->addChild(rule,3);
                        auto* name=menuLabel(unlock.name,css(15),{tableX+10,y},tone,FontWeight::Black);name->setAnchorPoint({0,.5f});fitMenuLabelWidth(name,tableR-tableX-90,css(15),.1f);styleAchievementText(name,unlock.tier);row->addChild(name,5);
                        auto* ap=menuLabel(fmt::format("+{} AP",unlock.ap),css(15),{tableR-8,y},Gold,FontWeight::Black);ap->setAnchorPoint({1,.5f});row->addChild(ap,5);
                        row->runAction(CCSequence::create(CCDelayTime::create(.45f+.32f*static_cast<float>(i)),CCEaseSineOut::create(CCMoveTo::create(.38f,{0,0})),nullptr));
                    }
                }
                // Header chip: the round badges earned in this match.
                if(m_badgeCount>0){auto* text=menuLabel(fmt::format(fmt::runtime(i18n::text("survival-badges-earned","ROUND BADGES +{}")),m_badgeCount),css(15),{0,0},Accent,FontWeight::Black);
                    auto const chipW=text->getScaledContentSize().width+12;auto* chip=CCLayerColor::create({Ink.r,Ink.g,Ink.b,255},chipW,13);chip->setPosition({size.width-14-40-8-chipW,size.height-8-5-6.5f});chip->setSkewX(8);header->addChild(chip,5);
                    text->setPosition({chip->getPositionX()+chipW/2,size.height-8-5});header->addChild(text,6);}
            }
            if(ceremony)button(i18n::text("survival-continue-result","CONTINUE · MY RESULT"),{size.width/2,23},150,20,true,27,20)->setEnabled(settled);
            else if(!choice && stage!=SurvivalFlowStage::ScoreAnimation){
                // S6 footer: QUEUE AGAIN is not offered here (the board's lobby
                // flow owns it); CLOSE returns to the lobby.
                // A tap meant for the score screen right before must not close
                // the result the moment it appears.
                m_close=button(i18n::text("close","CLOSE"),{size.width/2,23},150,1,true,29,20);m_close->setEnabled(false);
            }
        }
        m_elapsed+=std::max(0.f,dt);
        if(m_headerWipe){auto const t=std::clamp(static_cast<float>(m_elapsed)/.55f,0.f,1.f),eased=1-(1-t)*(1-t)*(1-t);
            m_headerWipe->setPositionX(m_headerWipeFrom+(m_headerWipeTo-m_headerWipeFrom)*eased);}
        for(std::size_t i=0;i<m_badges.size();++i){auto const frame=survivalBadgeMotion(i,m_elapsed);m_badges[i]->setScale(static_cast<float>(frame.scale));propagateModeOpacity(m_badges[i],static_cast<GLubyte>(frame.opacity*255));}
        for(auto const& p:m_stages){auto const frame=survivalPodiumFrame(p.rank,m_elapsed);
            p.pedestal->setVisible(frame.rise>0);p.pedestal->setScaleY(static_cast<float>(frame.rise));
            p.avatar->setVisible(frame.avatar);p.avatar->setEnabled(frame.interactive);
            p.avatar->setPosition({p.x+p.height*kPodiumLean,p.base+p.height+36+static_cast<float>(frame.drop*58)});
            p.stamp->setVisible(frame.stamp>0);p.stamp->setScale(static_cast<float>(frame.stampScale));propagateModeOpacity(p.stamp,static_cast<GLubyte>(frame.stampOpacity*255));
            p.place->setVisible(frame.stamp>0);
            if(p.glow){p.glow->setVisible(frame.glowOpacity>0);
                // The existing zero-sized RGBA root cascades this envelope while
                // its child sprites keep their gold ambient pulse/rotation.
                if(auto* rgba=dynamic_cast<CCRGBAProtocol*>(p.glow))rgba->setOpacity(static_cast<GLubyte>(std::lround(frame.glowOpacity*255)));
            }
        }
        if(m_close)m_close->setEnabled(view.result.isObject() && m_elapsed>=1.0);
        if(!m_watch){auto const lock=SurvivalRuntime::get().watchLockSeconds();if(m_freeChoice)m_freeChoice->setEnabled(lock==0);if(m_resultChoice)m_resultChoice->setEnabled(lock==0);}
        if(m_watch){auto const lock=SurvivalRuntime::get().watchLockSeconds();m_watch->setEnabled(lock==0);if(m_freeChoice)m_freeChoice->setEnabled(lock==0);if(m_resultChoice)m_resultChoice->setEnabled(lock==0);m_watchText->setString((i18n::text("survival-watch","WATCH")+(lock>0?fmt::format(" ({})",lock):std::string{})).c_str());m_watchText->setColor(lock>0?TextMuted:Accent);fitFullMenuLabel(m_watchText,m_watch->getContentSize().width-12,22,.29f);}
    }
};
}
