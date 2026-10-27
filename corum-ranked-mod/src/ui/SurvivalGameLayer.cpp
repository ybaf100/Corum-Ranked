#include "../SurvivalSpectatorRuntime.hpp"
#include "../SurvivalRuntime.hpp"
#include "../SurvivalSceneBridge.hpp"
#include "../RankedRoundPreparation.hpp"
#include "../RankedRuntime.hpp"
#include "../RankedAudioManager.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "RankTransitionLayer.hpp"
#include "ResultScoreLayer.hpp"
#include "RankedConfirmPopup.hpp"
#include "RankedJukeboxPopup.hpp"
#include "PlayerIcon.hpp"
#include "SurvivalOutcomePanel.hpp"
#include "AchievementUnlockToast.hpp"
#include "ModeLobbyMotion.hpp"
#include "ModeLobbySurfaceFade.hpp"
#include "RankBadgeAssets.hpp"
#include "../domain/CompetitiveHudLayout.hpp"
#include "../domain/ScoreDisplay.hpp"
#include "../domain/SurvivalRoundMenuPolicy.hpp"
#include "../domain/SurvivalBoardPresentation.hpp"
#include "../domain/SurvivalNativeOptions.hpp"
#include "../SurvivalOptionsPreferences.hpp"
#include "SurvivalMapSelectionLayer.hpp"
#include <array>
#include "../RankedMapDetails.hpp"
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/cocos/actions/CCActionEase.h>
#include <unordered_map>

using namespace geode::prelude;
namespace corum::ranked {
namespace {
class SurvivalGameLayer final : public CCLayerColor {
    struct Tile {CCNode* root=nullptr;CCLabelBMFont* name=nullptr;CCLabelBMFont* record=nullptr;CCPoint destination;CCNode* icon=nullptr;CCNode* tierBadge=nullptr;std::string tier="";CCNode* eliminatedBackground=nullptr;std::string titleText,recordText;std::array<CCNode*,3> medals{};int infoTag=0;SurvivalProgressTween progress{};SurvivalHudColor accent{};bool hasAccent=false;};
    std::unordered_map<std::string,Tile> m_tiles;
    std::unordered_map<int,SurvivalStanding> m_infoByTag;
    int m_nextInfoTag=1000;
    CCNode* m_grid=nullptr;
    CCNode* m_pager=nullptr;CCLabelBMFont* m_pageLabel=nullptr;
    std::size_t m_cardPage=0,m_cardPages=1;
    ui::SurvivalMapSelectionLayer* m_selection=nullptr;
    ui::SurvivalOutcomePanel* m_outcome=nullptr;
    LoadingSpinner* m_spinner=nullptr;
    CCLabelBMFont* m_boardClock=nullptr;
    CCMenuItemSpriteExtra *m_backToMap=nullptr,*m_viewBoard=nullptr;
    std::string m_navigationContext,m_replayRequestedContext;
    std::optional<std::int64_t> m_entrySyncAfter;
    bool m_browsingBoard=false,m_manualMap=false;
    std::uint64_t m_boardVisit=0;
    CCLabelBMFont *m_heading=nullptr,*m_status=nullptr,*m_map=nullptr,*m_surrenderText=nullptr;
    CCMenuItemSpriteExtra *m_resume=nullptr,*m_continue=nullptr,*m_surrender=nullptr;
    CCMenuItemSpriteExtra* m_force=nullptr;
    CCMenuItemSpriteExtra* m_freePlay=nullptr;
    CCMenuItemSpriteExtra* m_liveWatch=nullptr;
    std::optional<WeakRef<ui::RankedConfirmPopup>> m_confirmation;
    std::optional<WeakRef<ui::RankedJukeboxPopup>> m_jukebox;
    std::string m_launchedContext,m_resultMotion,m_gridContext,m_confirmationContext;
    std::string m_eliminationKey,m_pendingLaunch;
    std::string m_jukeboxContext;
    std::chrono::steady_clock::time_point m_eliminationUntil{};
    std::chrono::steady_clock::time_point m_nextMapAt{},m_introUntil{},m_returnAt{};
    std::optional<RankedAudioMode> m_audioMode;
    bool m_closed=false,m_motion=false;float m_frameDelta=0;
    static std::string text(char const* key,char const* fallback) {return i18n::text(key,fallback);}
    CCMenuItemSpriteExtra* button(CCMenu* menu,std::string const& caption,CCPoint position,float width,int tag,ccColor3B color=ui::Blue,CCLabelBMFont** saved=nullptr) {
        auto* face=ui::surface({width,28},{},color,ui::SurfaceRaised,255,true);
        auto* label=ui::menuLabel(caption,.29f,{width/2,14},ui::Text,ui::FontWeight::SemiBold);
        ui::fitMenuLabelWidth(label,width-12,.29f,.20f);face->addChild(label,5);if(saved)*saved=label;
        auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalGameLayer::onAction));item->setTag(tag);item->setPosition(position);menu->addChild(item);return item;
    }
    bool init() override {
        if(!CCLayerColor::initWithColor({0,0,0,255}))return false;setID("survival-game-coordinator"_spr);setKeypadEnabled(true);
        auto const size=CCDirector::sharedDirector()->getWinSize();setContentSize(size);
        m_heading=ui::menuLabel("SURVIVAL",.60f,{size.width/2,size.height-29},ui::Cyan,ui::FontWeight::Black);addChild(m_heading);
        m_boardClock=ui::menuLabel("",.28f,{size.width-12,size.height-22},ui::Cyan,ui::FontWeight::ExtraBold);
        m_boardClock->setAnchorPoint({1,.5f});m_boardClock->setID("survival-board-clock"_spr);addChild(m_boardClock,10);
        m_map=ui::menuLabel("",.27f,{size.width/2,size.height-48},ui::TextMuted,ui::FontWeight::SemiBold);addChild(m_map);
        m_status=ui::menuLabel("",.28f,{size.width/2,80},ui::TextMuted,ui::FontWeight::SemiBold);addChild(m_status);
        m_grid=CCNode::create();addChild(m_grid,5);
        m_spinner=LoadingSpinner::create(20);m_spinner->setPosition({size.width/2-113,size.height-49});m_spinner->setVisible(false);addChild(m_spinner,8);
        m_introUntil=std::chrono::steady_clock::now()+std::chrono::milliseconds(SurvivalRosterMilliseconds);
        auto* menu=CCMenu::create();menu->setPosition(CCPointZero);addChild(menu,20);
        m_pager=CCNode::create();addChild(m_pager,20);m_pager->setVisible(false);
        auto* pageMenu=CCMenu::create();pageMenu->setPosition(CCPointZero);m_pager->addChild(pageMenu);
        button(pageMenu,"<",{size.width/2-70,58},48,16);button(pageMenu,">",{size.width/2+70,58},48,17);
        m_pageLabel=ui::menuLabel("",.24f,{size.width/2,58},ui::TextMuted);m_pager->addChild(m_pageLabel);
        m_resume=button(menu,text("survival-reenter","RE-ENTER"),{size.width/2,22},132,15,ui::Cyan);
        m_continue=button(menu,text("close","CLOSE"),{size.width/2,22},132,1);
        m_surrender=button(menu,text("surrender","SURRENDER"),{size.width-68,22},116,2,ui::Red,&m_surrenderText);
        m_freePlay=button(menu,text("survival-free-play","FREE PLAY"),{68,22},116,7,ui::Green);
        m_liveWatch=button(menu,text("survival-watch","WATCH"),{size.width/2,22},132,19,ui::Cyan);m_liveWatch->setVisible(false);
        m_freePlay->setVisible(false);
        m_backToMap=button(menu,text("survival-back-map","BACK TO MAP"),{size.width/2,22},132,8,ui::Cyan);
        m_viewBoard=button(menu,text("survival-view-board","VIEW STATUS"),{68,22},116,9,ui::Blue);
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        m_force=button(menu,"Force n",{68,22},116,3,ui::Red);
#endif
        m_selection=ui::SurvivalMapSelectionLayer::create([weak=WeakRef(this)](int tag){if(auto owner=weak.lock())owner->handleAction(tag);});
        if(!m_selection)return false;m_selection->setVisible(false);addChild(m_selection,15);
        m_outcome=ui::SurvivalOutcomePanel::create([weak=WeakRef(this)](int tag){if(auto owner=weak.lock())owner->handleAction(tag);});
        if(!m_outcome)return false;m_outcome->setVisible(false);addChild(m_outcome,50);
        scheduleUpdate();return true;
    }
    void confirmSurrender() {
        if(m_confirmation || !SurvivalRuntime::get().canSurrender())return;
        auto const& view=SurvivalRuntime::get().view();m_confirmationContext=view.id+":"+view.roundKey+":"+std::to_string(view.mapGeneration);auto const id=view.id,round=view.roundKey;auto const generation=view.mapGeneration;
        auto* popup=ui::RankedConfirmPopup::create(text("surrender-confirm","CONFIRM SURRENDER"),
            SurvivalRuntime::get().surrenderMessage(),
            [weak=WeakRef(this),id,round,generation](bool accepted){
                auto owner=weak.lock();log::info("[SurvivalSurrender] board confirm accepted={} match={} round={} generation={} owner={}",accepted,id,round,generation,static_cast<bool>(owner));if(!owner || !owner->getParent())return;owner->m_confirmation.reset();
                auto const& current=SurvivalRuntime::get().view();
                if(accepted && current.id==id && current.roundKey==round && current.mapGeneration==generation &&
                    SurvivalRuntime::get().canSurrender())SurvivalRuntime::get().surrender();
            });
        if(popup){m_confirmation.emplace(popup);popup->show();}
    }
    void onAction(CCObject* object) {if(auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(object))handleAction(item->getTag());}
    void onPlayerInfo(CCObject* object) {
        auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(object);
        if(!item || m_closed || m_motion)return;
        auto const found=m_infoByTag.find(item->getTag());
        if(found==m_infoByTag.end())return;
        auto const& runtime=SurvivalRuntime::get();
        if(!runtime.active())return;
        ui::openSurvivalProfile(this,found->second);
    }
    void handleAction(int tag) {
        if(m_closed || m_motion || (tag<11 && std::chrono::steady_clock::now()<m_eliminationUntil))return;
        auto& runtime=SurvivalRuntime::get();auto* level=RankedLevelPreparation::get().level();
        if((tag==0 || tag==4 || tag==5 || tag==6 || tag==10) && (m_confirmation || runtime.surrenderPending()))return;
        if(tag==13 || tag==19){m_browsingBoard=true;if(runtime.officialRoundCleared() && !runtime.freePlaying()){runtime.chooseOutcome(SurvivalOutcomeChoice::LiveSpectate);SurvivalSpectatorRuntime::get().openTargets();}return;}
        if(tag==18){runtime.chooseOutcome(SurvivalOutcomeChoice::Observe);m_launchedContext.clear();m_manualMap=false;m_browsingBoard=false;return;}
        if(tag==14){SurvivalSpectatorRuntime::get().leave();runtime.leaveObserver();runtime.chooseOutcome(SurvivalOutcomeChoice::PersonalResult);return;}
        if(tag==12){beginResultMotion();return;}
        if(tag==11){ui::showSurvivalStandings();return;}
        if(tag==10)RankedRoundPreparation::get().retry();
        if(tag==0)queueNativeLaunch();
        if(tag==15)requestClearedReplay();
        if(tag==16 && m_cardPages>1)m_cardPage=(m_cardPage+m_cardPages-1)%m_cardPages;
        if(tag==17 && m_cardPages>1)m_cardPage=(m_cardPage+1)%m_cardPages;
        if(tag==1)runtime.acknowledge();
        if(tag==2){if(runtime.outcomeChoice()==SurvivalOutcomeChoice::LiveSpectate){SurvivalSpectatorRuntime::get().leave();runtime.chooseOutcome(SurvivalOutcomeChoice::PersonalResult);}else if(runtime.observing())runtime.leaveObserver();else confirmSurrender();}
        if(tag==7){if(runtime.view().ownEliminated){SurvivalSpectatorRuntime::get().leave();runtime.chooseOutcome(SurvivalOutcomeChoice::Observe);m_browsingBoard=false;m_manualMap=true;}else runtime.beginFreePlay();}
        if(tag==8){m_browsingBoard=false;m_manualMap=runtime.view().state=="PLAYING" || runtime.previouslyEntered();m_entrySyncAfter=runtime.serverSyncMillis();runtime.refreshRound();}
        if(tag==9){++m_boardVisit;m_browsingBoard=true;m_manualMap=true;}
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        if(tag==3)showSurvivalForceRankPopup();
#endif
        auto& preferences=SurvivalOptionsPreferences::get();auto const optionsContext=runtime.optionsContext();auto const options=preferences.current(optionsContext);
        if(tag==4 && survivalSupportsLowDetail(level,runtime.view().map.levelId,RankedRoundPreparation::get().view().mapReady)){
            preferences.setRoundLowDetail(optionsContext,!options.lowDetail);preferences.apply(level,optionsContext);
        }
        if(tag==5 && level && static_cast<int>(level->m_levelID)==runtime.view().map.levelId){preferences.setRoundDisableShake(optionsContext,!options.disableShake);preferences.apply(level,optionsContext);}
        if(tag==6 && level){
            if(!ui::rankedJukeboxLoaded()) {
                if(auto* popup=ui::RankedConfirmPopup::create("Jukebox",text("jukebox-required","The Jukebox mod is required!"),[](bool){}))popup->show();
            } else if(!m_jukebox || !m_jukebox->lock() || !m_jukebox->lock()->getParent()) {
                if(auto* popup=ui::RankedJukeboxPopup::create(level)){m_jukeboxContext=runtime.view().id+":"+runtime.view().roundKey+":"+std::to_string(runtime.view().mapGeneration);m_jukebox.emplace(popup);popup->show();popup->openWhenReady();}
            }
        }
    }
    void requestClearedReplay() {
        auto& runtime=SurvivalRuntime::get();if(!runtime.canReplayCleared())return;
        m_browsingBoard=false;m_manualMap=true;m_replayRequestedContext=preparationContextText(runtime.preparationContext());
        m_entrySyncAfter=runtime.serverSyncMillis();runtime.refreshRound();
    }
    void queueNativeLaunch(bool automatic=false) {
        auto& runtime=SurvivalRuntime::get();auto const context=runtime.preparationContext();
        if(m_closed || !m_pendingLaunch.empty() || !m_selection->settled() || runtime.surrenderPending() || !(runtime.canEnter() || runtime.canEnterObserver() || runtime.canReplayCleared()) ||
            (m_entrySyncAfter && runtime.serverSyncMillis()<=*m_entrySyncAfter))return;
        m_pendingLaunch=preparationContextText(context);
        auto const navigation=m_boardVisit;
        // Native object setup/texture loading must run outside the coordinator's
        // scheduler callback. Recheck scene, generation and modal ownership after
        // the previous PlayLayer's deferred teardown/autoreleases have drained.
        queueInMainThread([weak=WeakRef(this),context,automatic,navigation] {
            auto owner=weak.lock();if(!owner)return;owner->m_pendingLaunch.clear();
            auto* scene=CCDirector::sharedDirector()->getRunningScene();
            auto& runtime=SurvivalRuntime::get();
            auto const jukeboxOpen=owner->m_jukebox && owner->m_jukebox->lock() && owner->m_jukebox->lock()->getParent();
            if(CCDirector::sharedDirector()->getNextScene() || jukeboxOpen || owner->m_closed || !owner->isRunning() || owner->getParent()!=scene || owner->m_motion ||
                owner->m_confirmation || owner->m_browsingBoard || (automatic && (owner->m_manualMap || navigation!=owner->m_boardVisit)) || !owner->m_selection->settled() || runtime.surrenderPending() || context!=runtime.preparationContext() || !(runtime.canEnter() || runtime.canEnterObserver() || runtime.canReplayCleared()) ||
                (owner->m_entrySyncAfter && runtime.serverSyncMillis()<=*owner->m_entrySyncAfter))return;
            if(RankedRoundPreparation::get().launch(scene)){owner->m_launchedContext=preparationContextText(context);owner->m_replayRequestedContext.clear();}
        });
    }
    void showParticipants(SurvivalMatchView const& view,bool result) {
        std::vector<SurvivalStanding> players=view.roundPlayers.empty()?view.standings:view.roundPlayers;
        if(result){players.clear();for(auto const& p:view.result["standings"].asArray().unwrapOr(std::vector<matjson::Value>{}))
            players.push_back({p["participantId"].asString().unwrapOr(""),p["name"].asString().unwrapOr(""),static_cast<int>(p["rank"].asInt().unwrapOr(0)),0,0,0,true,p["kind"].asString().unwrapOr("")=="BOT",false,p["displayedTier"].asString().unwrapOr("UNRANKED"),numFromString<int>(p["gdAccountId"].asString().unwrapOr("0")).unwrapOr(0)});}
        auto const completed=view.roundKey.ends_with(":elimination");
        auto const survivors=result?1:std::count_if(players.begin(),players.end(),[completed](auto const& p){return completed?p.passing:!p.eliminated;});
        auto const size=getContentSize();auto const grid=survivalGridLayout(size.width,size.height,players.size(),static_cast<int>(survivors),view.phase,view.matchPlayerCount);
        auto const gridContext=view.id+":"+view.roundKey+":"+std::to_string(view.mapGeneration)+(result?":result":":live")+(grid.compact?":compact":grid.reduced?":reduced":":full");
        if(m_gridContext!=gridContext){m_gridContext=gridContext;m_grid->removeAllChildrenWithCleanup(true);m_tiles.clear();m_infoByTag.clear();m_nextInfoTag=1000;m_cardPage=0;}
        auto const columns=grid.columns;auto const width=grid.cellWidth,height=grid.cellHeight;auto const compact=grid.compact;
        auto const reduced=grid.reduced;m_status->setPositionY(compact?50:80);
        m_cardPages=grid.pages;m_cardPage=std::min(m_cardPage,m_cardPages-1);
        m_pageLabel->setString(fmt::format("{} / {}",m_cardPage+1,m_cardPages).c_str());m_pager->setVisible(m_cardPages>1 && m_grid->isVisible());
        auto const left=size.width/2-(columns-1)*width/2,top=size.height-65-height/2;
        auto const card=survivalCardLayout(width,height,compact,reduced);
        for(auto& [id,tile]:m_tiles)tile.root->setVisible(false);
        auto const first=m_cardPage*grid.pageSize,last=std::min(players.size(),first+grid.pageSize);
        for(std::size_t i=first;i<last;i++){
            auto const& value=players[i];auto found=m_tiles.find(value.id);auto const cell=i-first;
            auto const destination=CCPoint{left+(cell%columns)*width,top-(cell/columns)*height};
            if(found==m_tiles.end()){
                auto* root=ui::surface({card.width,card.height},{},ui::Blue,ui::SurfaceRaised,230,true);root->setPosition(destination);m_grid->addChild(root);
                auto* nickname=ui::playerNameLabel(value.name,rankedMenuTextScale(.25f),{card.titleX,card.titleY},value.id==view.ownId?ui::Cyan:ui::Text,ui::FontWeight::Black);
                ui::fitSingleLineMenuLabel(nickname,card.titleWidth,card.titleHeight,.25f);root->addChild(nickname,5);
                int index=1;auto dash=value.id.rfind('-');if(dash!=std::string::npos)index=numFromString<int>(value.id.substr(dash+1)).unwrapOr(1);
                auto* avatarSlot=CCNode::create();avatarSlot->setPosition({card.avatarX,card.avatarY});root->addChild(avatarSlot,5);
                auto* iconMenu=CCMenu::create();iconMenu->setPosition(CCPointZero);avatarSlot->addChild(iconMenu,8);
                auto* iconFace=CCNode::create();iconFace->setContentSize({card.avatarSize,card.avatarSize});
                auto* icon=value.id==view.ownId?ui::PlayerIcon::createLocal():value.bot?ui::PlayerIcon::createBot(index):ui::PlayerIcon::createRemote(value.accountId);
                if(icon){icon->setScale(std::min(.65f,card.avatarSize/48.0f));icon->setPosition({card.avatarSize/2-(compact||reduced?0:10),card.avatarSize/2});iconFace->addChild(icon,5);}
                auto* iconButton=CCMenuItemSpriteExtra::create(iconFace,this,menu_selector(SurvivalGameLayer::onPlayerInfo));
                iconButton->setTag(m_nextInfoTag++);iconButton->setPosition(CCPointZero);iconMenu->addChild(iconButton);
                auto* record=compact?nullptr:ui::menuLabel("",.23f,{card.width/2,card.recordY},ui::TextMuted,ui::FontWeight::ExtraBold);if(record)root->addChild(record,5);
                found=m_tiles.emplace(value.id,Tile{root,nickname,record,destination,avatarSlot}).first;
                auto& tile=found->second;tile.infoTag=iconButton->getTag();
                tile.eliminatedBackground=ui::surface({card.width,card.height},{card.width/2,card.height/2},ui::Red,{42,13,28},250,true);
                tile.eliminatedBackground->setVisible(false);root->addChild(tile.eliminatedBackground,4);
                iconButton->setEnabled(false);
                auto* cardMenu=CCMenu::create();cardMenu->setPosition(CCPointZero);root->addChild(cardMenu,12);
                auto* hit=CCNode::create();hit->setContentSize({card.width,card.height});
                auto* cardButton=CCMenuItemSpriteExtra::create(hit,this,menu_selector(SurvivalGameLayer::onPlayerInfo));
                cardButton->setTag(tile.infoTag);cardButton->setPosition({card.width/2,card.height/2});cardMenu->addChild(cardButton);
            }
            auto& tile=found->second;tile.root->setVisible(true);m_infoByTag[tile.infoTag]=value;
            if(!compact){tile.progress.observe(view.revision,value.attemptId,value.attemptActive,value.bestProgress,value.currentProgress);tile.progress.advance(m_frameDelta);}
            if(view.roundKey.ends_with(":elimination"))tile.progress.best.displayed=tile.progress.best.target;
            if(!compact && !reduced && tile.tier!=value.tier){
                tile.tier=value.tier;if(tile.tierBadge)tile.tierBadge->removeFromParentAndCleanup(true);
                auto const badgeSize=std::min(20.0f,card.avatarSize);
                tile.tierBadge=ui::createAnimatedRankBadge(value.tier,{badgeSize,badgeSize},ui::RankBadgeDisplay::Leaderboard);
                tile.tierBadge->setPosition({12,0});tile.icon->addChild(tile.tierBadge,5);
            }
            if(tile.destination!=destination){tile.destination=destination;tile.root->stopActionByTag(7465);auto* move=CCEaseSineOut::create(CCMoveTo::create(.25f,destination));move->setTag(7465);tile.root->runAction(move);}
            auto rank=value.rank>0?(i18n::isKorean()?fmt::format(fmt::runtime(i18n::text("player-rank","{}")),value.rank):survivalOrdinal(value.rank)):std::string("-");
            std::string record;auto color=survivalRankColor(value.rank,value.passing);
            if(compact){
                if(tile.titleText!=value.name){tile.titleText=value.name;tile.name->setString(value.name.c_str());ui::fitSingleLineMenuLabel(tile.name,card.titleWidth,card.titleHeight,.25f);}
                tile.name->setColor(value.eliminated?ui::Red:value.id==view.ownId?ui::Cyan:ui::Text);
            }else if(result){int delta=0;for(auto const& p:view.result["standings"].asArray().unwrapOr(std::vector<matjson::Value>{}))if(p["participantId"].asString().unwrapOr("")==value.id)delta=static_cast<int>(p["delta"].asInt().unwrapOr(0));
                if(value.id==view.ownId)delta=static_cast<int>(view.result["actualDelta"].asInt().unwrapOr(delta));
                record=value.rank>0?fmt::format("{} · {:+} LP",rank,delta):text("waiting","WAITING");color=value.rank>0?survivalResultRankColor(value.rank,delta):survivalRankColor(10,true);
            }else {
                auto const status=value.eliminated?text("survival-eliminated","ELIMINATED"):value.cleared?text("survival-cleared","CLEARED"):
                    fmt::format("NOW {}%",rankedScoreText(tile.progress.now,view.phase=="TIEBREAK"));
                record=reduced?fmt::format("BEST {}% · NOW {}%",rankedScoreText(tile.progress.best.displayed,view.phase=="TIEBREAK"),rankedScoreText(tile.progress.now,view.phase=="TIEBREAK")):
                    fmt::format("BEST {}% · {}",rankedScoreText(tile.progress.best.displayed,view.phase=="TIEBREAK"),status);
                auto const title=compact?value.name:fmt::format("{} {}",rank,value.name);
                if(tile.titleText!=title){tile.titleText=title;tile.name->setString(title.c_str());ui::fitSingleLineMenuLabel(tile.name,card.titleWidth,card.titleHeight,.25f);}
                tile.name->setColor(value.eliminated?ui::Red:value.id==view.ownId?ui::Cyan:ui::Text);
            }
            auto const tint=value.eliminated?survivalRankColor(value.rank,false):
                (result||completed||view.state=="PLAYING")&&value.rank>0?survivalRankColor(value.rank,result||value.passing):SurvivalHudColor{77,135,255};
            if(!tile.hasAccent||tile.accent!=tint){tile.hasAccent=true;tile.accent=tint;ui::setSurfaceAccent(tile.root,{tint.r,tint.g,tint.b});}
            tile.eliminatedBackground->setVisible(value.eliminated || (!result && value.rank>0 && !value.passing));
            if(tile.record && tile.recordText!=record){tile.recordText=record;tile.record->setString(record.c_str());
                if(reduced)ui::fitSingleLineMenuLabel(tile.record,card.width-10,card.recordHeight,.21f);
                else ui::fitFullMenuLabel(tile.record,card.width-10,card.recordHeight,.21f);}
            if(tile.record)tile.record->setColor(ccc3(color.r,color.g,color.b));
        }
    }
    void syncAudio(SurvivalMatchView const& view,bool elimination=false) {
        auto& audio=RankedAudioManager::get();
        if(SurvivalRuntime::get().observing() && view.state!="FINAL_RESULT" && view.state!="CANCELLED"){
            auto const desired=(view.phase=="FINAL" || view.phase=="TIEBREAK")?RankedAudioMode::Round3DeathmatchWait:RankedAudioMode::Round12;
            if(m_audioMode!=desired){m_audioMode=desired;audio.setMode(desired);}audio.tick();return;
        }
        bool ownCompleted=false,qualified=false;
        for(auto const& player:view.completedRound["players"].asArray().unwrapOr(std::vector<matjson::Value>{}))if(player["participantId"].asString().unwrapOr("")==view.ownId){
            ownCompleted=true;qualified=player["qualified"].asBool().unwrapOr(false) && !player["forfeited"].asBool().unwrapOr(false);break;
        }
        auto const presenting=elimination || (view.state=="ROUND_RESULT" && view.completedRound["key"].asString().unwrapOr("")==view.roundKey);
        auto const music=survivalMenuMusic(view.phase,view.result.isObject() || view.state=="FINAL_RESULT",
            view.result["won"].asBool().unwrapOr(view.ownWinner),view.ownEliminated,presenting,
            view.completedRound["phase"].asString().unwrapOr(""),ownCompleted,qualified,view.matchPlayerCount);
        auto const desired=music==SurvivalMenuMusic::Win?RankedAudioMode::ResultWin:music==SurvivalMenuMusic::Lose?RankedAudioMode::ResultLose:
            music==SurvivalMenuMusic::Final?RankedAudioMode::Round3DeathmatchWait:RankedAudioMode::Round12;
        if(m_audioMode!=desired){m_audioMode=desired;audio.setMode(desired);}
        audio.tick();
    }
    bool presentEliminations(SurvivalMatchView const& view) {
        auto const now=std::chrono::steady_clock::now();
        auto const& completed=view.completedRound;
        if(!completed.isObject())return false;
        auto const key=view.id+":"+completed["key"].asString().unwrapOr("");
        if(key==m_eliminationKey){
            if(now<m_eliminationUntil)return true;
            Mod::get()->setSavedValue("survival-elimination-animation",key);
            if(now<m_nextMapAt)return true;
            if(view.presentationGate=="ROUND_RESULT" && view.state=="ROUND_RESULT"){
                auto* scene=CCDirector::sharedDirector()->getRunningScene();
                if(!m_confirmation && !(scene && scene->getChildByIDRecursive("survival-debug-config"_spr)))SurvivalRuntime::get().presentationComplete();
                return true;
            }
            return false;
        }
        if(key==Mod::get()->getSavedValue<std::string>("survival-elimination-animation") && view.presentationGate!="ROUND_RESULT")return false;
        m_eliminationKey=key;
        auto preview=view;preview.roundKey=completed["key"].asString().unwrapOr("")+":elimination";
        preview.mapGeneration=static_cast<int>(completed["mapGeneration"].asInt().unwrapOr(0));
        preview.phase=completed["phase"].asString().unwrapOr("");preview.standings.clear();preview.roundPlayers.clear();preview.aliveIds.clear();
        for(auto const& p:completed["players"].asArray().unwrapOr(std::vector<matjson::Value>{})) {
            auto const id=p["participantId"].asString().unwrapOr("");auto const passing=p["qualified"].asBool().unwrapOr(false);
            preview.aliveIds.push_back(id);preview.standings.push_back({id,p["name"].asString().unwrapOr(id),
                static_cast<int>(p["rank"].asInt().unwrapOr(0)),0,0,p["score"].asDouble().unwrapOr(0),passing,p["kind"].asString().unwrapOr("")=="BOT",false,p["displayedTier"].asString().unwrapOr("UNRANKED"),numFromString<int>(p["gdAccountId"].asString().unwrapOr("0")).unwrapOr(0)});
        }
        for(auto& row:preview.standings)row.bestProgress=row.score;
        std::stable_sort(preview.standings.begin(),preview.standings.end(),[](auto const& a,auto const& b){return a.rank<b.rank;});
        showParticipants(preview,false);
        auto const alreadyPresented=key==Mod::get()->getSavedValue<std::string>("survival-elimination-animation");
        for(std::size_t i=0;i<preview.standings.size();i++) {
            auto const& value=preview.standings[i];if(value.passing)continue;
            auto found=m_tiles.find(value.id);if(found==m_tiles.end())continue;auto& tile=found->second;
            auto const size=tile.root->getContentSize();
            auto* wrap=CCLayerColor::create({0,0,0,255},0,0);wrap->setCascadeOpacityEnabled(true);
            wrap->addChild(ui::surface(size,{size.width/2,size.height/2},ui::Red,{42,13,28},250,true));tile.root->addChild(wrap,4);
            ui::setModeLobbyOpacity(wrap,0);
            auto const lead=alreadyPresented?0.0f:SurvivalBeforeEliminationMilliseconds/1000.0f;
            wrap->runAction(CCSequence::create(CCDelayTime::create(lead),CallFuncExt::create([weak=WeakRef(wrap)]{
                if(auto layer=weak.lock())ui::fadeModeLobbySurfaces(layer.data(),.24f,true);
            }),CCEaseSineOut::create(ui::ModeLobbyFade::create(.24f,255)),nullptr));tile.name->setColor(ui::Red);
            if(alreadyPresented){if(tile.icon)tile.icon->setVisible(false);continue;}
            if(tile.icon)tile.icon->runAction(CCSequence::create(CCDelayTime::create(lead+.18f),CCSpawn::create(
                CCEaseSineIn::create(CCMoveBy::create(.66f,{i%4<2?-90.0f:90.0f,-getContentSize().height})),
                CCRotateBy::create(.66f,i%2?270.0f:-270.0f),CCScaleTo::create(.66f,.18f),nullptr),CCHide::create(),nullptr));
        }
        auto const phase=preview.phase=="TIEBREAK"?std::string("DEATH MATCH"):preview.phase=="FINAL"?std::string("FINAL"):
            "ROUND "+std::to_string(completed["ordinal"].asInt().unwrapOr(1));
        m_heading->setString(fmt::format("SURVIVAL · {} · {}",phase,text("result","RESULT")).c_str());
        m_eliminationUntil=now+std::chrono::milliseconds(alreadyPresented?0:SurvivalBeforeEliminationMilliseconds+SurvivalEliminationMilliseconds);
        m_nextMapAt=now+std::chrono::milliseconds(SurvivalRosterMilliseconds);return true;
    }
    void scoreMotion(ScoreAnimationInput input) {
        auto const weak=WeakRef<SurvivalGameLayer>(this);
        queueInMainThread([weak,input=std::move(input)]() mutable {
            auto owner=weak.lock();if(!owner || !owner->isRunning())return;
            auto* score=ui::ResultScoreLayer::create(std::move(input),owner.data(),[weak] {if(auto layer=weak.lock()){layer->m_motion=false;layer->setKeypadEnabled(true);}});
            if(score)owner->addChild(score,1100);else{owner->m_motion=false;owner->setKeypadEnabled(true);}
        });
    }
    void beginResultMotion() {
        auto& runtime=SurvivalRuntime::get();auto const& view=runtime.view();
        if(!view.result.isObject() || m_resultMotion==view.id)return;
        if(Mod::get()->getSavedValue<std::string>("survival-result-animation")==view.id)return;
        auto input=runtime.scoreAnimation();if(!input)return;m_resultMotion=view.id;Mod::get()->setSavedValue("survival-result-animation",view.id);m_motion=true;setKeypadEnabled(false);
        auto const weak=WeakRef<SurvivalGameLayer>(this);auto finish=[weak,input=*input]{if(auto owner=weak.lock())owner->scoreMotion(input);};
        if((input->promoted || input->demoted) && input->beforeTier!=input->afterTier){
            auto* transition=ui::RankTransitionLayer::create(input->beforeTier,input->afterTier,input->promoted?ui::RankTransitionType::Promotion:ui::RankTransitionType::Demotion,
                text(input->promoted?"promoted":"demoted",input->promoted?"PROMOTED":"DEMOTED"),text("skip","SKIP"),text("continue","CONTINUE"),finish,true);
            if(transition){addChild(transition,1200);return;}
        }finish();
    }
public:
    static SurvivalGameLayer* create(){auto* layer=new SurvivalGameLayer;if(layer->init()){layer->autorelease();return layer;}delete layer;return nullptr;}
    void update(float dt) override {
        m_frameDelta=dt;
        if(m_closed)return;auto& runtime=SurvivalRuntime::get();
        auto const preparationBegan=std::chrono::steady_clock::now();runtime.tick();
        auto const presentationBegan=std::chrono::steady_clock::now();
        auto const preparationMillis=std::chrono::duration<double,std::milli>(presentationBegan-preparationBegan).count();
        if(preparationMillis>50)log::info("[RankedEntryTiming] stage=runtime-preparation elapsedMs={}",preparationMillis);
        if(m_confirmation){auto dialog=m_confirmation->lock();if(!dialog || !dialog->getParent())m_confirmation.reset();}
        if(!runtime.active() && !runtime.creating()){m_closed=true;queueInMainThread([]{CCDirector::sharedDirector()->popScene();});return;}
        auto const& view=runtime.view();auto const& preparation=RankedRoundPreparation::get().view();auto const watching=runtime.outcomeChoice()==SurvivalOutcomeChoice::LiveSpectate;auto const result=view.result.isObject() && !runtime.observing() && !watching;
        auto const stage=runtime.flowStage();auto const outcome=stage==SurvivalFlowStage::EliminatedChoice || stage==SurvivalFlowStage::PersonalResult || stage==SurvivalFlowStage::Ceremony;
        if(outcome){
            if(stage==SurvivalFlowStage::PersonalResult || stage==SurvivalFlowStage::Ceremony)beginResultMotion();
            syncAudio(view);if(m_selection->isVisible())m_selection->hideSelection();if(view.nextMap["map"].isObject())m_selection->prepareServerMap(runtime.preparationView());m_grid->setVisible(false);m_pager->setVisible(false);m_heading->setVisible(false);m_map->setVisible(false);m_status->setVisible(false);m_boardClock->setVisible(false);m_spinner->setVisible(false);
            for(auto* button:{m_liveWatch,m_resume,m_continue,m_surrender,m_freePlay,m_backToMap,m_viewBoard,m_force})if(button)button->setVisible(false);
            m_outcome->setVisible(!m_motion);if(!m_motion){m_outcome->present(view,stage,dt);
                ui::showAchievementUnlocks(this,view.id+":"+view.ownId,view.result["achievementUnlocks"]);
            }
            return;
        }
        m_outcome->setVisible(false);
        auto const elimination=!runtime.observing() && presentEliminations(view);
        syncAudio(view,elimination);
        auto const intro=view.presentationGate=="INTRO";
        auto const nextRound=runtime.observing() || survivalCanPrepareNext(view.state,view.ownEliminated,view.resultPending,result);
        auto const navigationContext=view.id+":"+view.roundKey+":"+std::to_string(view.mapGeneration);
        if(m_navigationContext!=navigationContext){m_navigationContext=navigationContext;m_browsingBoard=false;m_manualMap=false;m_entrySyncAfter.reset();m_replayRequestedContext.clear();}
        auto const liveBoard=(watching || m_browsingBoard || (runtime.officialRoundCleared() && !m_manualMap)) && !result && !elimination;
        auto const clock=survivalBoardClock(view.state,runtime.countdownMs(),runtime.roundRemainingMs());
        m_boardClock->setString(i18n::text(clock,clock).c_str());m_boardClock->setVisible(!result && !elimination && !clock.empty());
        auto const headingWidth=std::max(80.f,getContentSize().width-24-(m_boardClock->isVisible()?2*144.f:0.f));
        m_heading->setPosition({getContentSize().width/2,getContentSize().height-29});
        ui::fitMenuLabelWidth(m_boardClock,140,.28f,.18f);
        auto const mapWindow=nextRound && !elimination && !intro && !liveBoard && view.map.levelId>0 && view.state!="ROUND_RESULT";
        auto const searching=nextRound && !elimination && !intro && !liveBoard && view.state=="MAP_SELECTION";
        auto const selectionWindow=mapWindow || searching;
        auto const now=std::chrono::steady_clock::now();
        auto const debugPopup=CCDirector::sharedDirector()->getRunningScene()->getChildByIDRecursive("survival-debug-config"_spr)!=nullptr;
        m_grid->setVisible(!selectionWindow);m_pager->setVisible(!selectionWindow && m_cardPages>1);
        m_heading->setVisible(!selectionWindow);m_map->setVisible(!selectionWindow);m_status->setVisible(!selectionWindow);
        if(selectionWindow)m_boardClock->setVisible(false);
        else if(view.nextMap["map"].isObject())m_selection->prepareServerMap(runtime.preparationView());
        else if(intro && nextRound && !elimination)m_selection->prepareServerMap(view);
        else m_selection->hideSelection();
        m_spinner->setVisible(false);m_spinner->setPosition({getContentSize().width/2,getContentSize().height/2});m_spinner->setScale(2);
        auto const context=preparationContextText(runtime.preparationContext());auto* level=RankedLevelPreparation::get().level();
        m_resume->setVisible(liveBoard && runtime.canReplayCleared());m_resume->setEnabled(!m_motion && !elimination && !m_confirmation && (!m_entrySyncAfter || runtime.serverSyncMillis()>*m_entrySyncAfter));
        m_continue->setVisible(result);m_continue->setEnabled(!m_motion && !elimination && !runtime.creating());
        auto const surrenderCaption=watching?text("survival-view-results","VIEW RESULTS"):runtime.observing()?text("survival-leave-observer","LEAVE FREE PLAY"):text("surrender","SURRENDER");
        if(surrenderCaption!=m_surrenderText->getString()){m_surrenderText->setString(surrenderCaption.c_str());ui::fitFullMenuLabel(m_surrenderText,104,22,.29f);}
        m_surrender->setVisible(watching || (nextRound && (runtime.observing() || runtime.canSurrender() || runtime.surrenderPending())));m_surrender->setEnabled(!elimination && !m_motion && (watching || runtime.observing() || runtime.canSurrender()));
        m_liveWatch->setVisible((watching || liveBoard) && runtime.officialRoundCleared() && !runtime.freePlaying());
        m_backToMap->setVisible(!watching && liveBoard && nextRound && !runtime.canReplayCleared());m_backToMap->setEnabled(!m_motion && !elimination && !runtime.surrenderPending());
        m_viewBoard->setVisible(selectionWindow);m_viewBoard->setEnabled(!m_motion && !elimination);
        m_freePlay->setVisible(watching || (liveBoard && runtime.canFreePlay()));m_freePlay->setEnabled(!m_motion && !m_confirmation);
        if(m_force){m_force->setVisible(view.debug && !view.online && nextRound && !selectionWindow && !liveBoard);m_force->setEnabled(!m_motion && now>=m_eliminationUntil && !runtime.creating());}
        if(!elimination)m_heading->setString(result?fmt::format("SURVIVAL · {}",i18n::isKorean()?fmt::format(fmt::runtime(i18n::text("player-rank","{}")),view.result["rank"].asInt().unwrapOr(0)):survivalOrdinal(static_cast<int>(view.result["rank"].asInt().unwrapOr(0)))).c_str():
            (intro?fmt::format("SURVIVAL · {} {}",view.playerCount,corum::ranked::i18n::literal("inline.players")):
            fmt::format("SURVIVAL · {}",view.phase=="TIEBREAK"?"DEATH MATCH":view.phase=="FINAL"?"FINAL":("ROUND "+std::to_string(view.roundNumber)))).c_str());
        ui::fitFullMenuLabel(m_heading,headingWidth,26,.60f);
        auto const group=view.group=="RED_BRONZE"?std::string("RED-BRONZE"):view.group;
        // The intro roster must not reveal the server's preselected map.
        m_map->setString((!intro && view.map.levelId>0?fmt::format("{} · GD #{}",group,view.map.levelId):group).c_str());
        std::string status;
        if(view.state=="CANCELLED")status=text("survival-cancelled","MATCH CANCELLED");
        else if(result)status="";
        else if(intro)status=fmt::format("{} · {} / {}",text("matchmaking","MATCHMAKING"),view.playerCount,view.playerCount);
        else if(view.resultPending || view.state=="FINAL_RESULT" || view.ownEliminated)status=text("survival-result-pending","RANK FIXED · SETTLING LP...");
        else if(!view.error.empty())status=i18n::text(view.error,view.error);
        else if(liveBoard)status=text("survival-live-board","LIVE ROUND STATUS");
        else if(view.state=="MAP_SELECTION")status=view.mapError.empty()?text("survival-selecting-map","SELECTING A SERVER MAP..."):text("survival-map-retry","MAP SEARCH FAILED · RETRYING...");
        else if(!preparation.mapReady)status=text("downloading-map","DOWNLOADING MAP...");
        else if(!preparation.audioReady)status=text("downloading-audio","DOWNLOADING AUDIO...");
        else status=(m_manualMap || m_launchedContext==context)?text("survival-game-running","THE SERVER TIMER CONTINUES. YOU CAN RE-ENTER."):text("survival-auto-entry","READY · ENTERING AUTOMATICALLY...");
        if(elimination && view.state!="CANCELLED"){
            if(now<m_eliminationUntil)status=text("result","RESULT");
            else if(nextRound)status=text("survival-next-map-wait","NEXT MAP SOON...");
        }
        m_status->setString(status.c_str());ui::fitMenuLabelWidth(m_status,getContentSize().width-24,.28f,.20f);
        if(selectionWindow){
            auto const interactive=!m_motion && !elimination && !runtime.surrenderPending() && !m_confirmation;
            m_selection->present(view,preparation,SurvivalOptionsPreferences::get().current(runtime.optionsContext()),
                survivalSupportsLowDetail(level,view.map.levelId,preparation.mapReady),interactive,
                (runtime.canEnter() || runtime.canEnterObserver() || runtime.canReplayCleared()) && interactive && (!m_entrySyncAfter || runtime.serverSyncMillis()>*m_entrySyncAfter),runtime.countdownMs(),runtime.roundRemainingMs());
        }else if(!elimination)showParticipants(view,result);
        if(m_confirmation && (result || view.state=="CANCELLED" || m_confirmationContext!=view.id+":"+view.roundKey+":"+std::to_string(view.mapGeneration))){if(auto popup=m_confirmation->lock())popup->cancel();m_confirmation.reset();}
        runtime.selectedPanelReady(selectionWindow && m_selection->settled() && preparation.mapReady && preparation.audioReady && preparation.error.empty());
        if(RankedRoundPreparation::get().ready()){
            if(runtime.observing())runtime.validateNative(level);
            else if(!view.ownAssetsReady)runtime.assetsReady(level);
        }
        if(m_jukebox && m_jukeboxContext!=view.id+":"+view.roundKey+":"+std::to_string(view.mapGeneration)){if(auto popup=m_jukebox->lock())popup->dismiss();m_jukebox.reset();}
        auto const jukeboxOpen=m_jukebox && m_jukebox->lock() && m_jukebox->lock()->getParent();
        if(jukeboxOpen)if(auto popup=m_jukebox->lock())popup->setCountdown(i18n::text(clock,clock));
        // INTRO and round-result gates advance on the authoritative server clock.
        if(!m_motion && !elimination && !m_confirmation && !jukeboxOpen && !debugPopup && !runtime.creating() &&
            mapWindow && view.state!="PLAYING" && !m_manualMap && m_selection->settled() && !runtime.surrenderPending() &&
            (runtime.canEnter() || runtime.canEnterObserver()) && m_launchedContext!=context)queueNativeLaunch(true);
        if(m_replayRequestedContext==context && selectionWindow && !m_motion && !m_confirmation && !m_browsingBoard && runtime.canReplayCleared())queueNativeLaunch();
        if(view.state=="CANCELLED" && !result && !runtime.creating())runtime.acknowledge();
        auto const presentationMillis=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-presentationBegan).count();
        if(presentationMillis>50)log::info("[RankedEntryTiming] stage=coordinator-presentation elapsedMs={} level={}",presentationMillis,view.map.levelId);
    }
    void onEnter() override {
        CCLayerColor::onEnter();SurvivalRuntime::get().observerReturned();
        // Native Quit/Resume may have paused the FMOD output while this retained
        // scene was covered. Reclaim menu/result audio after native teardown.
        m_audioMode.reset();RankedAudioManager::get().onApplicationResume();syncAudio(SurvivalRuntime::get().view());
    }
    void keyBackClicked() override {
        if(m_closed || m_motion || std::chrono::steady_clock::now()<m_eliminationUntil)return;
        auto& runtime=SurvivalRuntime::get();
        if(runtime.flowStage()==SurvivalFlowStage::EliminatedChoice || runtime.outcomeChoice()==SurvivalOutcomeChoice::LiveSpectate || runtime.observing()){
            runtime.leaveObserver();runtime.chooseOutcome(SurvivalOutcomeChoice::PersonalResult);return;
        }
        if(runtime.view().result.isObject()){runtime.acknowledge();return;}
        // Both gameplay exit and this first Back are navigation only. The
        // retained coordinator owns the match/polling and never pops here.
        if(!m_browsingBoard && !(SurvivalRuntime::get().officialRoundCleared() && !m_manualMap)){
            ++m_boardVisit;m_browsingBoard=true;m_manualMap=true;return;
        }
        if(!SurvivalRuntime::get().view().ownEliminated)confirmSurrender();
    }
    void onExit() override {
        if(m_confirmation){if(auto popup=m_confirmation->lock())popup->cancel();m_confirmation.reset();}
        if(m_jukebox){if(auto popup=m_jukebox->lock())popup->dismiss();m_jukebox.reset();}
        CCLayerColor::onExit();
    }
};
}
void showSurvivalGame() {
    auto* director=CCDirector::sharedDirector();auto* current=director->getRunningScene();
    if(!SurvivalRuntime::get().active() || (current && current->getChildByID("survival-game-coordinator"_spr)))return;
    // A retired native PlayLayer may still be the GD singleton for a frame
    // after its scene has been popped. Only a *running* official game in the
    // active scene should prevent restoring this match's coordinator.
    auto* currentPlay=PlayLayer::get();
    if(currentPlay && currentPlay->isRunning() &&
        currentPlay->getParent()==current && isOfficialSurvivalPlayLayer(currentPlay))return;
    auto* layer=SurvivalGameLayer::create();if(!layer)return;auto* scene=CCScene::create();scene->addChild(layer);director->pushScene(scene);
}
}
