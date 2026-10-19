#pragma once
#include "../SurvivalRuntime.hpp"
#include "PlayerIcon.hpp"
#include "RankBadgeAssets.hpp"
#include "SurvivalProfile.hpp"
#include "../domain/ScoreDisplay.hpp"
#include <Geode/ui/Popup.hpp>
#include <functional>
namespace corum::ranked::ui {
// All result surfaces and the local observer board share the same server roster.
inline std::vector<SurvivalStanding> survivalRoster(SurvivalMatchView const& view){
    std::vector<SurvivalStanding> rows;
    for(auto const& p:view.participants.asArray().unwrapOr(std::vector<matjson::Value>{})){
        SurvivalStanding row;row.id=p["participantId"].asString().unwrapOr("");row.name=p["name"].asString().unwrapOr(row.id);
        row.bot=p["kind"].asString().unwrapOr("")=="BOT";row.accountId=geode::utils::numFromString<int>(p["gdAccountId"].asString().unwrapOr("0")).unwrapOr(0);
        row.rank=static_cast<int>(p["finalRank"].asInt().unwrapOr(0));row.tier=p["displayedTier"].asString().unwrapOr("UNRANKED");row.eliminated=!p["eliminationStage"].isNull();
        for(auto const& live:view.roundPlayers)if(live.id==row.id){auto const finalRank=row.rank;row=live;if(finalRank>0)row.rank=finalRank;break;}
        for(auto const& result:view.result["standings"].asArray().unwrapOr(std::vector<matjson::Value>{}))if(result["participantId"].asString().unwrapOr("")==row.id){
            if(result["rank"].isNumber())row.rank=static_cast<int>(result["rank"].asInt().unwrapOr(0));
            if(result["tierAfter"].isString())row.tier=result["tierAfter"].asString().unwrapOr("UNRANKED");
        }
        rows.push_back(std::move(row));
    }
    std::stable_sort(rows.begin(),rows.end(),[](auto const& a,auto const& b){return (a.rank>0?a.rank:100)<(b.rank>0?b.rank:100);});
    if(rows.size()>16)rows.resize(16);return rows;
}
inline CCNode* survivalAvatar(SurvivalStanding const& row,std::string const& own,float extent){
    auto* icon=row.id==own?PlayerIcon::createLocal():row.bot?PlayerIcon::createBot(static_cast<int>(std::hash<std::string>{}(row.id)%15+1)):PlayerIcon::createRemote(row.accountId);
    if(icon)icon->setScale(extent/48.f);return icon;
}
class SurvivalStandingsPopup final:public geode::Popup {
    struct Card {SurvivalStanding row;CCLabelBMFont* title=nullptr;CCLabelBMFont* progress=nullptr;CCNode* red=nullptr;SurvivalProgressTween tween;};
    std::vector<Card> m_cards;std::string m_match;
    CCLabelBMFont* m_clock=nullptr;
    bool init() override{
        if(!Popup::init(490,282))return false;
        m_bgSprite->setVisible(false);setID("survival-standings-popup"_spr);
        auto const size=CCDirector::sharedDirector()->getWinSize();m_mainLayer->setScale(std::min({1.f,(size.width-16)/490,(size.height-16)/282}));
        m_mainLayer->addChild(surface({490,282},{245,141},Blue,Surface,255,true));
        auto& runtime=SurvivalRuntime::get();m_match=runtime.view().id;
        m_clock=menuLabel("SURVIVAL",.30f,{245,264},Cyan,FontWeight::Black);m_mainLayer->addChild(m_clock,5);
        auto const rows=survivalRoster(runtime.view());
        for(std::size_t i=0;i<rows.size();++i){
            auto const& row=rows[i];auto* face=surface({112,51},{},Blue,SurfaceRaised,250,true);
            auto* red=surface({112,51},{56,25.5f},Red,{42,13,28},255,true);face->addChild(red,1);
            auto* name=playerNameLabel(row.name,.22f,{56,43},Text,FontWeight::Black);face->addChild(name,5);
            if(auto* icon=survivalAvatar(row,runtime.view().ownId,15)){icon->setPosition({44,26});face->addChild(icon,5);}
            auto* badge=createAnimatedRankBadge(row.tier,{17,17},RankBadgeDisplay::Leaderboard);badge->setPosition({68,26});face->addChild(badge,5);
            auto* progress=menuLabel("",.16f,{56,9},TextMuted,FontWeight::ExtraBold);face->addChild(progress,5);
            auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalStandingsPopup::onPlayer));item->setTag(static_cast<int>(i));item->setPosition({65.f+120*(i%4),224.f-56*(i/4)});m_buttonMenu->addChild(item);
            m_cards.push_back({row,name,progress,red,{}});
        }
        scheduleUpdate();update(0);return true;
    }
    void onPlayer(CCObject* sender){if(auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(sender);item && item->getTag()>=0 && static_cast<std::size_t>(item->getTag())<m_cards.size())openSurvivalProfile(this,m_cards[item->getTag()].row);}
public:
    static SurvivalStandingsPopup* create(){auto* p=new SurvivalStandingsPopup;if(p->init()){p->autorelease();return p;}delete p;return nullptr;}
    void update(float dt) override{
        auto& r=SurvivalRuntime::get();r.tick();auto const& v=r.view();if(v.id!=m_match){onClose(nullptr);return;}
        auto const rows=survivalRoster(v);
        auto const remaining=r.roundRemainingMs();auto const seconds=remaining?(*remaining+999)/1000:0;
        m_clock->setString(fmt::format("SURVIVAL · {} · {} · {}",v.phase,remaining?fmt::format("{:02}:{:02}",seconds/60,seconds%60):"--:--",v.cutoff?rankedScoreText(*v.cutoff,v.phase=="TIEBREAK")+"%":"--%").c_str());fitFullMenuLabel(m_clock,440,18,.30f);
        for(auto& card:m_cards){auto it=std::find_if(rows.begin(),rows.end(),[&](auto const& row){return row.id==card.row.id;});if(it==rows.end())continue;card.row=*it;
            auto const& row=card.row;card.tween.observe(v.revision,row.attemptId,row.attemptActive,row.bestProgress,row.currentProgress);card.tween.advance(dt);
            card.title->setString(fmt::format("{} {}",row.rank>0?survivalOrdinal(row.rank):"--",row.name).c_str());fitFullMenuLabel(card.title,102,12,.22f);
            card.progress->setString(fmt::format("BEST {}% · NOW {}%",rankedScoreText(card.tween.best.displayed,false),rankedScoreText(card.tween.now,false)).c_str());fitFullMenuLabel(card.progress,104,11,.16f);
            card.red->setVisible(row.eliminated || (row.rank>0 && !row.passing && v.state=="PLAYING"));
        }
    }
};
inline void showSurvivalStandings(){
    auto* scene=CCDirector::sharedDirector()->getRunningScene();if(!scene || scene->getChildByIDRecursive("survival-standings-popup"_spr))return;
    if(auto* popup=SurvivalStandingsPopup::create())popup->show();
}
class SurvivalOutcomePanel final:public cocos2d::CCLayerColor {
    std::function<void(int)> m_action;CCMenu* m_menu=nullptr;CCMenuItemSpriteExtra* m_watch=nullptr;
    std::string m_key,m_identity;std::vector<SurvivalStanding> m_profiles;
    std::vector<std::pair<CCNode*,float>> m_stages;double m_elapsed=0;
    CCLabelBMFont *m_watchText=nullptr,*m_detail=nullptr;
    CCNode* m_halo=nullptr;
    void onAction(CCObject* object){if(auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(object);item && item->isEnabled()){
        auto tag=item->getTag();if(tag>=100){if(static_cast<std::size_t>(tag-100)<m_profiles.size())openSurvivalProfile(this,m_profiles[tag-100]);}
        else if(tag==11)showSurvivalStandings();else if(m_action)m_action(tag);
    }}
    CCMenuItemSpriteExtra* button(std::string text,CCPoint at,float width,int tag,ccColor3B accent=Blue){
        auto* face=surface({width,30},{},accent,SurfaceRaised,255,true);auto* label=menuLabel(text,.29f,{width/2,15},Text,FontWeight::ExtraBold);fitFullMenuLabel(label,width-12,22,.29f);face->addChild(label,5);
        auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalOutcomePanel::onAction));item->setPosition(at);item->setTag(tag);m_menu->addChild(item);if(tag==13)m_watchText=label;return item;
    }
public:
    static SurvivalOutcomePanel* create(std::function<void(int)> action){auto* p=new SurvivalOutcomePanel;if(p->initWithColor({4,8,16,255})){p->m_action=std::move(action);p->setID("survival-outcome-panel"_spr);p->autorelease();return p;}delete p;return nullptr;}
    void present(SurvivalMatchView const& view,SurvivalFlowStage stage,float dt){
        auto const choice=stage==SurvivalFlowStage::EliminatedChoice,ceremony=stage==SurvivalFlowStage::Ceremony;
        auto const key=view.id+":"+std::to_string(static_cast<int>(stage));
        // Settlement arriving updates the labels/tiers without resetting a podium animation.
        auto const settled=view.result.isObject();std::string buildKey=key+(settled?":settled":":pending");
        for(auto const& p:view.result["standings"].asArray().unwrapOr(std::vector<matjson::Value>{}))
            buildKey+=":"+p["participantId"].asString().unwrapOr("")+":"+p["tierAfter"].asString().unwrapOr("")+":"+(p["lpAfter"].isNumber()?std::to_string(p["lpAfter"].asInt().unwrapOr(0)):"pending");
        auto const size=CCDirector::sharedDirector()->getWinSize();
        if(m_key!=buildKey){auto const elapsed=m_identity==key?m_elapsed:0.;m_identity=key;m_key=buildKey;removeAllChildrenWithCleanup(true);m_profiles=survivalRoster(view);m_stages.clear();m_elapsed=elapsed;m_watch=nullptr;m_detail=m_watchText=nullptr;m_halo=nullptr;
            m_menu=CCMenu::create();m_menu->setPosition(CCPointZero);addChild(m_menu,20);
            auto heading=menuLabel(i18n::text(choice?"survival-eliminated":ceremony?"survival-podium":"survival-personal-result",choice?"ELIMINATED":ceremony?"FINAL PODIUM":"PERSONAL RESULT"),.60f,{size.width/2,size.height-30},ceremony?Gold:Cyan,FontWeight::Black);fitFullMenuLabel(heading,size.width-30,28,.60f);addChild(heading,5);
            if(choice){
                auto* detail=menuLabel(i18n::text("survival-observer-description","Play the selected map locally without official records."),.25f,{size.width/2,size.height*.57f},TextMuted,FontWeight::SemiBold);fitFullMenuLabel(detail,size.width-40,40,.25f);addChild(detail,5);
                auto const width=std::min(180.f,(size.width-64)/2);
                m_watch=button("",{size.width/2-width/2-8,size.height*.37f},width,13,Cyan);
                button(i18n::text("survival-view-results","VIEW RESULTS"),{size.width/2+width/2+8,size.height*.37f},width,14,Gold);
            }else if(ceremony){
                std::array<int,3> ranks{2,1,3};auto const width=std::min(138.f,(size.width-48)/3),base=80.f;
                for(int n=0;n<3;++n){int rank=ranks[n];auto it=std::find_if(m_profiles.begin(),m_profiles.end(),[&](auto const& p){return p.rank==rank;});if(it==m_profiles.end())continue;
                    auto const h=rank==1?95.f:rank==2?70.f:51.f;auto const x=size.width/2+(n-1)*(width+8);auto const tint=rank==1?Gold:rank==2?ccc3(201,215,235):ccc3(208,137,70);
                    auto* group=CCLayerColor::create({0,0,0,0});group->setCascadeOpacityEnabled(true);addChild(group,5);
                    group->addChild(surface({width,h},{x,base+h/2},tint,SurfaceRaised,255,true));
                    std::string after;std::optional<std::int64_t> lpAfter;
                    for(auto const& p:view.result["standings"].asArray().unwrapOr(std::vector<matjson::Value>{}))if(p["participantId"].asString().unwrapOr("")==it->id){
                        after=p["tierAfter"].asString().unwrapOr("");if(p["lpAfter"].isNumber())lpAfter=p["lpAfter"].asInt().unwrapOr(0);
                    }
                    if(!after.empty()){
                        auto const extent=std::min(28.f,h*.36f);auto* badge=createAnimatedRankBadge(after,{extent,extent},RankBadgeDisplay::Leaderboard);badge->setPosition({x,base+h*.72f});group->addChild(badge,5);
                        auto* tier=menuLabel(after,.18f,{x,base+h*.42f},tint,FontWeight::ExtraBold);fitFullMenuLabel(tier,width-12,h*.20f,.18f);group->addChild(tier,5);
                    }
                    auto const score=lpAfter?fmt::format("{} LP",*lpAfter):i18n::text("survival-result-wait","PENDING");
                    auto* points=menuLabel(score,.21f,{x,base+h*.17f},lpAfter?Text:TextMuted,FontWeight::ExtraBold);fitFullMenuLabel(points,width-12,h*.22f,.21f);points->setID("survival-podium-lp-after"_spr);group->addChild(points,5);
                    auto* face=CCNode::create();face->setContentSize({width,70});if(auto* icon=survivalAvatar(*it,view.ownId,34)){icon->setPosition({width/2,45});face->addChild(icon,5);}
                    auto* label=playerNameLabel(it->name,.26f,{width/2,14},tint,FontWeight::Black);fitFullMenuLabel(label,width-8,16,.26f);face->addChild(label,5);
                    auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalOutcomePanel::onAction));item->setTag(100+static_cast<int>(it-m_profiles.begin()));item->setPosition({x,base+h+36});m_menu->addChild(item);
                    auto* place=menuLabel(survivalOrdinal(rank),.28f,{x,base-13},tint,FontWeight::Black);group->addChild(place,5);
                    m_stages.push_back({group,rank==3?.15f:rank==2?.65f:1.15f});m_stages.push_back({item,rank==3?.15f:rank==2?.65f:1.15f});
                    if(rank==1){auto* glow=createRankAmbientFx("GOLD",{58,58},RankBadgeDisplay::Compact);glow->setPosition({x,base+h+46});glow->setID("survival-podium-gold-ambient"_spr);group->addChild(glow,0);m_halo=glow;}
                }
            }else{
                auto it=std::find_if(m_profiles.begin(),m_profiles.end(),[&](auto const& row){return row.id==view.ownId;});
                if(it!=m_profiles.end()){
                    auto* face=CCNode::create();face->setContentSize({260,85});if(auto* icon=survivalAvatar(*it,view.ownId,43)){icon->setPosition({130,59});face->addChild(icon,5);}
                    auto* name=playerNameLabel(it->name,.37f,{130,15},Cyan,FontWeight::Black);fitFullMenuLabel(name,248,22,.37f);face->addChild(name,5);
                    auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalOutcomePanel::onAction));item->setTag(100+static_cast<int>(it-m_profiles.begin()));item->setPosition({size.width/2,size.height*.65f});m_menu->addChild(item);
                    auto const rank=it->rank>0?survivalOrdinal(it->rank):i18n::text("survival-result-wait","PENDING");
                    auto* label=menuLabel(rank,.48f,{size.width/2,size.height*.42f},Gold,FontWeight::Black);addChild(label,5);
                }
                auto const lp=settled?fmt::format("{:+} LP · {} LP · {}",view.result["actualDelta"].asInt().unwrapOr(0),view.result["profileAfter"]["lp"].asInt().unwrapOr(0),view.result["profileAfter"]["tier"].asString().unwrapOr("")):i18n::text("survival-result-wait","SETTLEMENT PENDING");
                m_detail=menuLabel(lp,.30f,{size.width/2,size.height*.28f},Text,FontWeight::ExtraBold);fitFullMenuLabel(m_detail,size.width-30,24,.30f);addChild(m_detail,5);
                if(settled){auto* badge=createAnimatedRankBadge(view.result["profileAfter"]["tier"].asString().unwrapOr("UNRANKED"),{40,40},RankBadgeDisplay::Leaderboard);badge->setPosition({size.width/2+110,size.height*.43f});addChild(badge,5);}
            }
            button(i18n::text("survival-view-board","VIEW STATUS"),{size.width/2,24},std::min(140.f,(size.width-48)/3),11,Blue);
            if(!choice){button(i18n::text("survival-lp-details","LP DETAILS"),{16+std::min(130.f,(size.width-48)/3)/2,24},std::min(130.f,(size.width-48)/3),12,Gold)->setEnabled(settled);button(i18n::text("close","CLOSE"),{size.width-16-std::min(112.f,(size.width-48)/3)/2,24},std::min(112.f,(size.width-48)/3),1,Blue)->setEnabled(settled);}
        }
        m_elapsed+=std::max(0.f,dt);
        for(auto const& [node,start]:m_stages){auto const t=std::clamp((m_elapsed-start)/.40,0.,1.);node->setVisible(t>0);node->setScale(static_cast<float>(.80+.20*(1-std::pow(1-t,3))));}
        if(m_watch){auto const lock=SurvivalRuntime::get().watchLockSeconds();m_watch->setEnabled(lock==0);m_watchText->setString((i18n::text("survival-watch","WATCH")+(lock>0?fmt::format(" ({})",lock):std::string{})).c_str());m_watchText->setColor(lock>0?TextMuted:Cyan);fitFullMenuLabel(m_watchText,m_watch->getContentSize().width-12,22,.29f);}
    }
};
}
