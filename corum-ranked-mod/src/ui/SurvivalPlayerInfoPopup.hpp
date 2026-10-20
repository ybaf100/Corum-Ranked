#pragma once
#include "../SurvivalRuntime.hpp"
#include "../RankedRuntime.hpp"
#include "../domain/PlayerInfoPresentation.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include "RankBadgeAssets.hpp"
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/UserInfoDelegate.hpp>
#include <Geode/ui/Popup.hpp>
namespace corum::ranked::ui {
using namespace geode::prelude;
/** Solo-style statistics, with Survival-owned identity and request lifetime.
 * It neither opens ProfilePage nor substitutes a real account for a bot. */
class SurvivalPlayerInfoPopup final:public Popup,public UserInfoDelegate {
    struct Statistics {int globalRank,stars,moons,demons;std::optional<int> extreme;};
    SurvivalStanding m_player;
    std::optional<WeakRef<CCNode>> m_listOwner;
    bool m_leaderboard=false,m_survivalLeaderboard=true;
    std::string m_matchId,m_session;
    bool m_closed=false,m_exited=false,m_inFlight=false,m_lookupFailed=false,m_forumBusy=false,m_forumFailed=false;
    int m_requests=0,m_forumReads=0;
    std::optional<Statistics> m_statistics;
    matjson::Value m_forum=nullptr;
    std::chrono::steady_clock::time_point m_deadline{},m_requestedAt{},m_nextRequest{},m_nextForumRead{};
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_forumRequest;
    CCNode* m_contents=nullptr;
    bool current()const{
        if(m_closed || RankedRuntime::get().spectatorLiveSessionToken()!=m_session)return false;
        if(m_leaderboard){if(!m_listOwner)return false;auto origin=m_listOwner->lock();return origin && origin->getParent();}
        return SurvivalRuntime::get().active() && SurvivalRuntime::get().view().id==m_matchId;
    }
    void releaseDelegate(){
        auto* manager=GameLevelManager::sharedState();if(manager && manager->m_userInfoDelegate==this)manager->m_userInfoDelegate=nullptr;m_inFlight=false;
    }
    void resolveStatistics(){
        if(m_player.bot || m_player.accountId<=0 || m_statistics || m_lookupFailed || !current())return;
        auto* manager=GameLevelManager::sharedState();if(!manager)return;
        if(auto* score=manager->userInfoForAccountID(m_player.accountId)){getUserInfoFinished(score);return;}
        auto const now=std::chrono::steady_clock::now();
        if(now>=m_deadline){m_lookupFailed=true;releaseDelegate();render();return;}
        if(m_inFlight && now-m_requestedAt<std::chrono::seconds(8))return;
        if(m_inFlight)releaseDelegate();
        if(m_requests>=3){m_lookupFailed=true;render();return;}
        if(now<m_nextRequest || (manager->m_userInfoDelegate && manager->m_userInfoDelegate!=this))return;
        manager->m_userInfoDelegate=this;m_inFlight=true;++m_requests;m_requestedAt=now;manager->getGJUserInfo(m_player.accountId);
    }
    void resolveForum(){
        if(!current() || m_player.bot || m_player.accountId<=0 || m_session.empty() || m_forumBusy || m_forumFailed || m_forumReads>=12)return;
        auto const now=std::chrono::steady_clock::now();if(now<m_nextForumRead)return;
        bool pending=m_forum.isNull();
        for(auto const& r:std::as_const(m_forum)["records"].asArray().unwrapOr(std::vector<matjson::Value>{}))pending|=r["status"].asString().unwrapOr("")=="PENDING";
        if(!pending)return;
        m_forumBusy=true;++m_forumReads;m_nextForumRead=now+std::chrono::seconds(1);
        auto request=web::WebRequest().timeout(std::chrono::seconds(8)).followRedirects(false);request.header("Authorization","Bearer "+m_session);
        // Participant IDs are server UUIDs or Debug's fixed human/bot identifiers.
        auto const path=m_leaderboard?"https://corum-ranked-production.up.railway.app/api/ranked/leaderboard/player-info/"+std::to_string(m_player.accountId):
            "https://corum-ranked-production.up.railway.app/api/ranked/survival/matches/"+m_matchId+"/player-info/"+m_player.id;
        m_forumRequest.spawn(request.get(path),[weak=WeakRef(this)](web::WebResponse response){
            auto owner=weak.lock();if(!owner || !owner->current() || !owner->getParent())return;
            owner->m_forumBusy=false;
            auto root=response.json().unwrapOr(matjson::Value());
            auto const account=numFromString<int>(root["gdAccountId"].asString().unwrapOr("0")).unwrapOr(0);
            if(response.code()<200 || response.code()>=300 || account!=owner->m_player.accountId ||
                (!owner->m_leaderboard && root["participantId"].asString().unwrapOr("")!=owner->m_player.id))owner->m_forumFailed=true;
            else owner->m_forum=std::move(root);
            owner->render();
        });
    }
    void row(std::string const& caption,std::string const& value,float y,ccColor3B color=Text){
        auto* label=menuLabel(caption,.34f,{18,y},TextMuted,FontWeight::SemiBold);label->setAnchorPoint({0,.5f});fitFullMenuLabel(label,156,18,.34f);m_contents->addChild(label,5);
        auto* number=menuLabel(value,.36f,{282,y},color,FontWeight::ExtraBold);number->setAnchorPoint({1,.5f});fitFullMenuLabel(number,104,18,.36f);m_contents->addChild(number,5);
    }
    void render(){
        if(m_contents)m_contents->removeFromParentAndCleanup(true);m_contents=CCNode::create();m_mainLayer->addChild(m_contents,10);
        auto* title=menuLabel(i18n::text("player-info","PLAYER INFO"),.48f,{138,284},Text,FontWeight::Black);fitFullMenuLabel(title,230,24,.48f);m_contents->addChild(title,5);
        auto* badge=createAnimatedRankBadge(m_player.tier,{26,26},RankBadgeDisplay::Compact);badge->setPosition({274,284});m_contents->addChild(badge,5);
        auto* name=playerNameLabel(m_player.name,.39f,{150,258},Gold,FontWeight::ExtraBold);fitFullMenuLabel(name,264,23,.39f);m_contents->addChild(name,5);
        row(m_leaderboard && !m_survivalLeaderboard?"SOLO RANKED":"SURVIVAL",m_player.bot?"BOT · "+m_player.tier:m_player.tier,234,Cyan);
        row(i18n::text("survival-rank","Rank"),m_player.rank>0?"#"+std::to_string(m_player.rank):i18n::text("survival-result-wait","PENDING"),213,Gold);
        float y=191;
        auto const unavailable=i18n::text("info-unavailable","Unavailable");auto const waiting=m_lookupFailed?unavailable:i18n::text("loading","Loading...");
        auto stat=[&](std::optional<int> value){return m_player.bot?std::string("BOT"):value?std::to_string(*value):waiting;};
        if(m_statistics && m_statistics->globalRank>0){row("GD Global Rank","#"+std::to_string(m_statistics->globalRank),y,Gold);y-=20;}
        row(i18n::text("info-stars","Stars"),stat(m_statistics?std::optional(m_statistics->stars):std::nullopt),y);y-=20;
        row(i18n::text("info-moons","Moons"),stat(m_statistics?std::optional(m_statistics->moons):std::nullopt),y);y-=20;
        row(i18n::text("info-demons","Demons"),stat(m_statistics?std::optional(m_statistics->demons):std::nullopt),y);y-=20;
        row("Extreme Demons",m_player.bot?"BOT":m_statistics?(m_statistics->extreme?std::to_string(*m_statistics->extreme):unavailable):waiting,y);y-=22;
        for(auto const* provider:{"GDKF","POINTERCRATE"}){
            std::string value=m_player.bot?"BOT":m_forumFailed?unavailable:m_forum.isNull()?i18n::text("loading","Loading..."):unavailable;
            auto color=TextMuted;
            for(auto const& record:std::as_const(m_forum)["records"].asArray().unwrapOr(std::vector<matjson::Value>{}))if(record["provider"].asString().unwrapOr("")==provider){
                auto const status=record["status"].asString().unwrapOr("");
                if(record["rank"].isNumber()){value="#"+std::to_string(record["rank"].asInt().unwrapOr(0));color=Gold;if(status=="ERROR")value+=" *";}
                else if(status=="UNRANKED")value=i18n::text("info-unranked","Not ranked");
                else if(status=="UNLINKED")value=i18n::text("info-unlinked","Not linked");
                else if(status=="PENDING")value=i18n::text("loading","Loading...");
            }
            row(std::string(provider)=="GDKF"?"GDKF":"Pointercrate",value,y,color);y-=20;
        }
    }
    bool init(SurvivalStanding const& player,std::string const& match){
        if(!Popup::init(300,310))return false;m_player=player;m_matchId=match;m_session=RankedRuntime::get().spectatorLiveSessionToken();
        if(m_player.bot)m_player.accountId=0;
        if(m_player.accountId<=0)m_lookupFailed=m_forumFailed=true;
        m_bgSprite->setVisible(false);m_closeBtn->setVisible(false);setID("survival-player-info-popup"_spr);
        auto const viewport=CCDirector::sharedDirector()->getWinSize();m_mainLayer->setScale(std::min({1.f,(viewport.width-24)/300,(viewport.height-24)/310}));
        m_mainLayer->addChild(surface({300,310},{150,155},Cyan,Surface,255,true));
        auto* face=surface({120,28},{},Blue,SurfaceRaised,255,true);auto* label=menuLabel(i18n::text("close","CLOSE"),.32f,{60,14},Text,FontWeight::ExtraBold);face->addChild(label,5);
        auto* close=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalPlayerInfoPopup::onClose));close->setPosition({150,20});m_buttonMenu->addChild(close);
        m_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(25);
        schedule(schedule_selector(SurvivalPlayerInfoPopup::refresh),.1f);render();return true;
    }
    void refresh(float){if(!current()){dismiss();return;}resolveStatistics();resolveForum();}
public:
    static SurvivalPlayerInfoPopup* create(SurvivalStanding const& player,std::string const& match){auto* p=new SurvivalPlayerInfoPopup;if(p->init(player,match)){p->autorelease();return p;}delete p;return nullptr;}
    static SurvivalPlayerInfoPopup* createForLeaderboard(SurvivalStanding const& player,CCNode* owner,bool survival){
        if(!owner || !owner->getParent())return nullptr;auto* p=new SurvivalPlayerInfoPopup;p->m_leaderboard=true;p->m_survivalLeaderboard=survival;p->m_listOwner.emplace(owner);
        if(p->init(player,"")){p->autorelease();return p;}delete p;return nullptr;
    }
    ~SurvivalPlayerInfoPopup()override{releaseDelegate();}
    void getUserInfoFinished(GJUserScore* score)override{
        if(!current() || !score || m_player.bot || score->m_accountID!=m_player.accountId)return;
        m_statistics=Statistics{score->m_globalRank,score->m_stars,score->m_moons,score->m_demons,extremeDemonCount(std::string_view(score->m_demonInfo.data(),score->m_demonInfo.size()))};releaseDelegate();render();
    }
    void getUserInfoFailed(int account)override{if(account!=m_player.accountId || !current())return;releaseDelegate();m_nextRequest=std::chrono::steady_clock::now()+std::chrono::seconds(1);}
    void userInfoChanged(GJUserScore* score)override{getUserInfoFinished(score);}
    void onClose(CCObject*)override{dismiss();}
    void dismiss(){if(m_closed)return;[[maybe_unused]]Ref<SurvivalPlayerInfoPopup> keepAlive(this);m_closed=true;releaseDelegate();m_forumRequest.cancel();unschedule(schedule_selector(SurvivalPlayerInfoPopup::refresh));Popup::onClose(nullptr);}
    void onExit()override{m_closed=true;releaseDelegate();m_forumRequest.cancel();unschedule(schedule_selector(SurvivalPlayerInfoPopup::refresh));if(!m_exited){m_exited=true;CCTouchDispatcher::get()->unregisterForcePrio(this);}Popup::onExit();}
};
}
