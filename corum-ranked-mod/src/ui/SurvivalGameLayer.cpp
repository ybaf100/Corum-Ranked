#include "../SurvivalRuntime.hpp"
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
#include "ModeLobbyMotion.hpp"
#include "ModeLobbySurfaceFade.hpp"
#include "RankedMapHeading.hpp"
#include "RankBadgeAssets.hpp"
#include "../domain/CompetitiveHudLayout.hpp"
#include "../domain/ScoreDisplay.hpp"
#include "../RankedMapDetails.hpp"
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/cocos/actions/CCActionEase.h>
#include <unordered_map>

using namespace geode::prelude;
namespace corum::ranked {
namespace {
class SurvivalGameLayer final : public CCLayerColor {
    struct Tile {CCNode* root=nullptr;CCLabelBMFont* name=nullptr;CCLabelBMFont* record=nullptr;CCPoint destination;CCNode* icon=nullptr;CCNode* tierBadge=nullptr;std::string tier="";};
    std::unordered_map<std::string,Tile> m_tiles;
    CCNode* m_grid=nullptr;
    CCNode* m_mapPanel=nullptr;
    LoadingSpinner* m_spinner=nullptr;
    CCLabelBMFont *m_mapReady=nullptr,*m_audioReady=nullptr;
    CCLabelBMFont *m_heading=nullptr,*m_status=nullptr,*m_map=nullptr,*m_ldmLabel=nullptr,*m_shakeLabel=nullptr;
    CCMenuItemSpriteExtra *m_resume=nullptr,*m_continue=nullptr,*m_surrender=nullptr,*m_ldm=nullptr,*m_shake=nullptr,*m_jukeboxButton=nullptr;
    CCMenuItemSpriteExtra* m_force=nullptr;
    std::optional<WeakRef<ui::RankedConfirmPopup>> m_confirmation;
    std::optional<WeakRef<ui::RankedJukeboxPopup>> m_jukebox;
    std::string m_launchedContext,m_resultMotion,m_optionsContext,m_gridContext,m_confirmationContext;
    std::string m_eliminationKey;
    std::string m_previewKey;
    std::chrono::steady_clock::time_point m_eliminationUntil{};
    std::chrono::steady_clock::time_point m_nextMapAt{},m_introUntil{},m_mapShownAt{};
    bool m_closed=false,m_motion=false,m_lowDetail=false,m_disableShake=false;
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
        m_lowDetail=Mod::get()->getSavedValue<bool>("survival-low-detail",false);
        m_disableShake=Mod::get()->getSavedValue<bool>("survival-disable-shake",false);
        m_heading=ui::menuLabel("SURVIVAL",.60f,{size.width/2,size.height-23},ui::Cyan,ui::FontWeight::Black);addChild(m_heading);
        m_map=ui::menuLabel("",.27f,{size.width/2,size.height-49},ui::TextMuted,ui::FontWeight::SemiBold);addChild(m_map);
        m_status=ui::menuLabel("",.28f,{size.width/2,80},ui::TextMuted,ui::FontWeight::SemiBold);addChild(m_status);
        m_grid=CCNode::create();addChild(m_grid,5);
        m_spinner=LoadingSpinner::create(20);m_spinner->setPosition({size.width/2-113,size.height-49});m_spinner->setVisible(false);addChild(m_spinner,8);
        m_introUntil=std::chrono::steady_clock::now()+std::chrono::milliseconds(SurvivalRosterMilliseconds);
        auto* menu=CCMenu::create();menu->setPosition(CCPointZero);addChild(menu,20);
        m_resume=button(menu,text("survival-reenter","RE-ENTER"),{size.width/2,22},132,0,ui::Cyan);
        m_continue=button(menu,text("close","CLOSE"),{size.width/2,22},132,1);
        m_surrender=button(menu,text("surrender","SURRENDER"),{size.width-68,22},116,2,ui::Red);
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        m_force=button(menu,"Force n",{68,22},116,3,ui::Red);
#endif
        auto const optionWidth=std::min(130.0f,(size.width-44)/3);
        m_ldm=button(menu,"LDM",{size.width/2-optionWidth-8,55},optionWidth,4,ui::Blue,&m_ldmLabel);
        m_shake=button(menu,"Disable Shake",{size.width/2,55},optionWidth,5,ui::Blue,&m_shakeLabel);
        m_jukeboxButton=button(menu,"Jukebox",{size.width/2+optionWidth+8,55},optionWidth,6);
        scheduleUpdate();return true;
    }
    void confirmSurrender() {
        if(m_confirmation)return;
        auto const& view=SurvivalRuntime::get().view();m_confirmationContext=view.id+":"+view.roundKey+":"+std::to_string(view.mapGeneration);auto const id=view.id,round=view.roundKey;auto const generation=view.mapGeneration;
        auto* popup=ui::RankedConfirmPopup::create(text("surrender-confirm","CONFIRM SURRENDER"),
            text("survival-surrender-warning","End this match using this round's recorded ranking?"),
            [weak=WeakRef(this),id,round,generation](bool accepted){
                auto owner=weak.lock();if(!owner || !owner->getParent())return;owner->m_confirmation.reset();
                auto const& current=SurvivalRuntime::get().view();
                if(accepted && current.id==id && current.roundKey==round && current.mapGeneration==generation &&
                    current.state!="FINAL_RESULT" && current.state!="CANCELLED")SurvivalRuntime::get().surrender();
            });
        if(popup){m_confirmation.emplace(popup);popup->show();}
    }
    void onAction(CCObject* object) {
        if(m_closed || m_motion || std::chrono::steady_clock::now()<m_eliminationUntil)return;
        auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(object);if(!item)return;
        auto& runtime=SurvivalRuntime::get();auto* level=RankedLevelPreparation::get().level();
        if(item->getTag()==0) {if(runtime.canEnter() && RankedRoundPreparation::get().launch(CCDirector::sharedDirector()->getRunningScene()))m_launchedContext=preparationContextText(runtime.preparationContext());}
        if(item->getTag()==1)runtime.acknowledge();
        if(item->getTag()==2)confirmSurrender();
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        if(item->getTag()==3)showSurvivalForceRankPopup();
#endif
        if(item->getTag()==4 && level){m_lowDetail=!m_lowDetail;Mod::get()->setSavedValue("survival-low-detail",m_lowDetail);RankedRoundPreparation::get().setLowDetail(m_lowDetail);}
        if(item->getTag()==5 && level){m_disableShake=!m_disableShake;Mod::get()->setSavedValue("survival-disable-shake",m_disableShake);level->m_disableShakeToggled=m_disableShake;}
        if(item->getTag()==6 && level){
            if(!ui::rankedJukeboxLoaded()) {
                if(auto* popup=ui::RankedConfirmPopup::create("Jukebox",text("jukebox-required","The Jukebox mod is required!"),[](bool){}))popup->show();
            } else if(!m_jukebox || !m_jukebox->lock() || !m_jukebox->lock()->getParent()) {
                if(auto* popup=ui::RankedJukeboxPopup::create(level)){m_jukebox.emplace(popup);popup->show();}
            }
        }
    }
    void showParticipants(SurvivalMatchView const& view,bool result) {
        auto const gridContext=view.id+":"+view.roundKey+":"+std::to_string(view.mapGeneration)+(result?":result":":live");
        if(m_gridContext!=gridContext){m_gridContext=gridContext;m_grid->removeAllChildrenWithCleanup(true);m_tiles.clear();}
        std::vector<SurvivalStanding> players=view.standings;
        if(!result)std::erase_if(players,[&](auto const& player){return std::find(view.aliveIds.begin(),view.aliveIds.end(),player.id)==view.aliveIds.end();});
        if(result){players.clear();for(auto const& p:view.result["standings"].asArray().unwrapOr(std::vector<matjson::Value>{}))
            players.push_back({p["participantId"].asString().unwrapOr(""),p["name"].asString().unwrapOr(""),static_cast<int>(p["rank"].asInt().unwrapOr(0)),0,0,0,true,p["kind"].asString().unwrapOr("")=="BOT",false,p["displayedTier"].asString().unwrapOr("UNRANKED")});}
        auto const size=getContentSize();auto const columns=std::min<std::size_t>(4,std::max<std::size_t>(1,players.size()));
        auto const rows=(players.size()+columns-1)/columns;auto const width=std::min(size.width-36,480.0f)/columns;
        auto const height=std::min(66.0f,std::max(36.0f,(size.height-163)/std::max<std::size_t>(1,rows)));
        auto const left=size.width/2-(columns-1)*width/2,top=size.height-65-height/2;
        for(auto& [id,tile]:m_tiles)tile.root->setVisible(std::any_of(players.begin(),players.end(),[&](auto const& p){return p.id==id;}));
        for(std::size_t i=0;i<players.size();i++){
            auto const& value=players[i];auto found=m_tiles.find(value.id);
            auto const destination=CCPoint{left+(i%columns)*width,top-(i/columns)*height};
            if(found==m_tiles.end()){
                auto* root=ui::surface({width-8,height-4},{},ui::Blue,ui::SurfaceRaised,230,true);root->setPosition(destination);m_grid->addChild(root);
                auto* nickname=ui::menuLabel(value.name,.25f,{(width-8)/2,height-10},value.id==view.ownId?ui::Cyan:ui::Text,ui::FontWeight::Black);
                ui::fitMenuLabelWidth(nickname,width-20,.25f,.20f);root->addChild(nickname,5);
                int index=1;auto dash=value.id.rfind('-');if(dash!=std::string::npos)index=numFromString<int>(value.id.substr(dash+1)).unwrapOr(1);
                auto* avatarSlot=CCNode::create();avatarSlot->setPosition({(width-8)/2,(height-4)/2});root->addChild(avatarSlot,5);
                auto* icon=value.id==view.ownId?ui::PlayerIcon::createLocal():ui::PlayerIcon::createBot(index);
                if(icon){icon->setScale(std::clamp((height-27)/48.0f,.26f,.65f));icon->setPosition({-10,0});avatarSlot->addChild(icon,5);}
                auto* record=ui::menuLabel("",.23f,{(width-8)/2,7},ui::TextMuted,ui::FontWeight::ExtraBold);root->addChild(record,5);
                found=m_tiles.emplace(value.id,Tile{root,nickname,record,destination,avatarSlot}).first;
            }
            auto& tile=found->second;tile.root->setVisible(true);
            if(tile.tier!=value.tier){
                tile.tier=value.tier;if(tile.tierBadge)tile.tierBadge->removeFromParentAndCleanup(true);
                auto const badgeSize=std::clamp((height-4)*.36f,11.0f,20.0f);
                tile.tierBadge=ui::createAnimatedRankBadge(value.tier,{badgeSize,badgeSize},ui::RankBadgeDisplay::Leaderboard);
                tile.tierBadge->setPosition({12,0});tile.icon->addChild(tile.tierBadge,5);
            }
            if(tile.destination!=destination){tile.destination=destination;tile.root->stopActionByTag(7465);auto* move=CCEaseSineOut::create(CCMoveTo::create(.25f,destination));move->setTag(7465);tile.root->runAction(move);}
            auto rank=i18n::isKorean()?fmt::format("{}위",value.rank):survivalOrdinal(value.rank);
            std::string record;auto color=survivalRankColor(value.rank,value.passing);
            if(result){int delta=0;for(auto const& p:view.result["standings"].asArray().unwrapOr(std::vector<matjson::Value>{}))if(p["participantId"].asString().unwrapOr("")==value.id)delta=static_cast<int>(p["delta"].asInt().unwrapOr(0));
                if(value.id==view.ownId)delta=static_cast<int>(view.result["actualDelta"].asInt().unwrapOr(delta));
                record=fmt::format("{} · {:+} LP",rank,delta);color=survivalResultRankColor(value.rank,delta);
            }else record=fmt::format("{} · {}%",rank,rankedScoreText(value.score,view.phase=="TIEBREAK"));
            tile.record->setString(record.c_str());tile.record->setColor(ccc3(color.r,color.g,color.b));ui::fitMenuLabelWidth(tile.record,width-20,.23f,.18f);
        }
    }
    void showMapInformation(SurvivalMatchView const& view) {
        auto const size=getContentSize();auto* level=RankedLevelPreparation::get().level();
        auto const native=level && static_cast<int>(level->m_levelID)==view.map.levelId;
        auto const key=view.id+":"+view.roundKey+":"+std::to_string(view.mapGeneration)+(native?":native":":loading");
        if(m_previewKey!=key){
            m_previewKey=key;if(m_mapPanel)m_mapPanel->removeFromParentAndCleanup(true);
            auto const width=std::min(388.0f,size.width-74.0f);
            m_mapPanel=ui::surface({width,174},{size.width/2,size.height/2-5},ui::Blue,ui::Surface,244,true);addChild(m_mapPanel,5);
            if(native)RankedMapDetails::get().remember(level);
            auto metadata=RankedMapDetails::get().metadata(view.map.levelId);
            if(metadata.kind=="UNKNOWN"){metadata.kind=view.map.demon==SurvivalDemon::None?"NORMAL":"DEMON";metadata.stars=view.map.stars;}
            ui::addRankedMapHeading(m_mapPanel,view.map.levelId,native?std::string(level->m_levelName):text("downloading-map","DOWNLOADING MAP..."),metadata,{width/2,150});
            auto* creator=ui::menuLabel(native?fmt::format("{} · GD #{}",std::string(level->m_creatorName),view.map.levelId):fmt::format("GD #{}",view.map.levelId),
                .25f,{width/2,107},ui::TextMuted,ui::FontWeight::SemiBold);ui::fitMenuLabelWidth(creator,width-30,.25f,.19f);m_mapPanel->addChild(creator,5);
            m_mapPanel->addChild(ui::menuLabel("03:00",.62f,{width/2,74},ui::Text,ui::FontWeight::Black),5);
            m_mapPanel->addChild(ui::menuLabel(text("survival-map-rules","180s · BEST PROGRESS x1"),.27f,{width/2,46},ui::Cyan,ui::FontWeight::SemiBold),5);
            m_mapReady=ui::menuLabel("",.25f,{width/2-80,19},ui::TextMuted,ui::FontWeight::SemiBold);m_mapPanel->addChild(m_mapReady,5);
            m_audioReady=ui::menuLabel("",.25f,{width/2+80,19},ui::TextMuted,ui::FontWeight::SemiBold);m_mapPanel->addChild(m_audioReady,5);
            m_mapShownAt=native?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        }
        auto const& prep=RankedRoundPreparation::get().view();
        m_mapReady->setString(fmt::format("{}: {}",text("map","MAP"),text(prep.mapReady?"ready":"loading",prep.mapReady?"READY":"LOADING")).c_str());
        m_audioReady->setString(fmt::format("{}: {}",text("audio","AUDIO"),text(prep.audioReady?"ready":"loading",prep.audioReady?"READY":"LOADING")).c_str());
        m_mapReady->setColor(prep.mapReady?ui::Green:ui::Cyan);m_audioReady->setColor(prep.audioReady?ui::Green:ui::Cyan);
        m_mapPanel->setVisible(true);
    }
    void syncAudio(SurvivalMatchView const& view) {
        auto& audio=RankedAudioManager::get();
        if(view.result.isObject())audio.setMode(view.result["actualDelta"].asInt().unwrapOr(0)>0?RankedAudioMode::ResultWin:RankedAudioMode::ResultLose);
        else if(view.ownEliminated)audio.setMode(RankedAudioMode::ResultLose);
        else audio.setMode(view.phase=="FINAL" || view.phase=="TIEBREAK"?RankedAudioMode::Round3DeathmatchWait:RankedAudioMode::Round12);
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
        preview.phase=completed["phase"].asString().unwrapOr("");preview.standings.clear();preview.aliveIds.clear();
        for(auto const& p:completed["players"].asArray().unwrapOr(std::vector<matjson::Value>{})) {
            auto const id=p["participantId"].asString().unwrapOr("");auto const passing=p["qualified"].asBool().unwrapOr(false);
            preview.aliveIds.push_back(id);preview.standings.push_back({id,p["name"].asString().unwrapOr(id),
                static_cast<int>(p["rank"].asInt().unwrapOr(0)),0,0,p["score"].asDouble().unwrapOr(0),passing,p["kind"].asString().unwrapOr("")=="BOT",false,p["displayedTier"].asString().unwrapOr("UNRANKED")});
        }
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
        if(!view.result.isObject() || m_resultMotion==view.id)return;m_resultMotion=view.id;
        if(Mod::get()->getSavedValue<std::string>("survival-result-animation")==view.id)return;
        auto input=runtime.scoreAnimation();if(!input)return;Mod::get()->setSavedValue("survival-result-animation",view.id);m_motion=true;setKeypadEnabled(false);
        auto const weak=WeakRef<SurvivalGameLayer>(this);auto finish=[weak,input=*input]{if(auto owner=weak.lock())owner->scoreMotion(input);};
        if((input->promoted || input->demoted) && input->beforeTier!=input->afterTier){
            auto* transition=ui::RankTransitionLayer::create(input->beforeTier,input->afterTier,input->promoted?ui::RankTransitionType::Promotion:ui::RankTransitionType::Demotion,
                text(input->promoted?"promoted":"demoted",input->promoted?"PROMOTED":"DEMOTED"),text("skip","SKIP"),text("continue","CONTINUE"),finish,true);
            if(transition){addChild(transition,1200);return;}
        }finish();
    }
public:
    static SurvivalGameLayer* create(){auto* layer=new SurvivalGameLayer;if(layer->init()){layer->autorelease();return layer;}delete layer;return nullptr;}
    void update(float) override {
        if(m_closed)return;auto& runtime=SurvivalRuntime::get();runtime.tick();
        if(!runtime.active() && !runtime.creating()){m_closed=true;queueInMainThread([]{CCDirector::sharedDirector()->popScene();});return;}
        auto const& view=runtime.view();auto const& preparation=RankedRoundPreparation::get().view();auto const result=view.result.isObject();
        syncAudio(view);
        auto const elimination=presentEliminations(view);
        auto const intro=view.presentationGate=="INTRO";
        auto const mapWindow=!result && !elimination && !intro && view.map.levelId>0 && view.state!="ROUND_RESULT";
        auto const searching=!result && !elimination && !intro && view.state=="MAP_SELECTION";
        auto const now=std::chrono::steady_clock::now();
        auto const debugPopup=CCDirector::sharedDirector()->getRunningScene()->getChildByIDRecursive("survival-debug-config"_spr)!=nullptr;
        m_grid->setVisible(!mapWindow && !searching);if(m_mapPanel)m_mapPanel->setVisible(mapWindow);
        m_spinner->setVisible(searching);m_spinner->setPosition({getContentSize().width/2,getContentSize().height/2});m_spinner->setScale(2);
        auto const context=preparationContextText(runtime.preparationContext());auto* level=RankedLevelPreparation::get().level();
        m_resume->setVisible(!result && m_launchedContext==context && runtime.canEnter());m_resume->setEnabled(!m_motion && !elimination);
        m_continue->setVisible(result);m_continue->setEnabled(!m_motion && !elimination && !runtime.creating());m_surrender->setVisible(!result);m_surrender->setEnabled(!elimination && !m_motion);
        if(m_force){m_force->setVisible(!result && !mapWindow);m_force->setEnabled(!m_motion && now>=m_eliminationUntil && !runtime.creating());}
        for(auto* option:{m_ldm,m_shake,m_jukeboxButton}){option->setVisible(mapWindow);option->setEnabled(!m_motion && !elimination && level && static_cast<int>(level->m_levelID)==view.map.levelId);}
        if(level && m_optionsContext!=context){m_optionsContext=context;RankedRoundPreparation::get().setLowDetail(m_lowDetail);level->m_disableShakeToggled=m_disableShake;}
        m_ldmLabel->setString(fmt::format("LDM: {}",m_lowDetail?"ON":"OFF").c_str());m_shakeLabel->setString(fmt::format("{}: {}",text("disable-shake","Disable Shake"),m_disableShake?"ON":"OFF").c_str());
        ui::fitMenuLabelWidth(m_shakeLabel,m_shake->getContentSize().width-12,.29f,.20f);
        if(!elimination)m_heading->setString(result?fmt::format("SURVIVAL · {}",i18n::isKorean()?fmt::format("{}위",view.result["rank"].asInt().unwrapOr(0)):survivalOrdinal(static_cast<int>(view.result["rank"].asInt().unwrapOr(0)))).c_str():
            (intro?fmt::format("SURVIVAL · {}",text("survival-match-roster","16 PLAYERS")):
            fmt::format("SURVIVAL · {}",view.phase=="TIEBREAK"?"DEATH MATCH":view.phase=="FINAL"?"FINAL":("ROUND "+std::to_string(view.roundNumber)))).c_str());
        auto const group=view.group=="RED_BRONZE"?std::string("RED-BRONZE"):view.group;
        m_map->setString((view.map.levelId>0?fmt::format("{} · GD #{}",group,view.map.levelId):group).c_str());
        std::string status;
        if(!view.error.empty())status=i18n::text(view.error,view.error);
        else if(result)status=fmt::format("{:+} LP · {} LP",view.result["actualDelta"].asInt().unwrapOr(0),view.result["profileAfter"]["lp"].asInt().unwrapOr(0));
        else if(intro)status=text("survival-match-intro","ALL 16 PARTICIPANTS HAVE JOINED");
        else if(view.state=="MAP_SELECTION")status=view.mapError.empty()?text("survival-selecting-map","SELECTING A SERVER MAP..."):text("survival-map-retry","MAP SEARCH FAILED · RETRYING...");
        else if(!preparation.mapReady)status=text("downloading-map","DOWNLOADING MAP...");
        else if(!preparation.audioReady)status=text("downloading-audio","DOWNLOADING AUDIO...");
        else status=m_launchedContext==context?text("survival-game-running","THE SERVER TIMER CONTINUES. YOU CAN RE-ENTER."):text("survival-auto-entry","READY · ENTERING AUTOMATICALLY...");
        if(elimination)status=now<m_eliminationUntil?text("result","RESULT"):text("survival-next-map-wait","NEXT MAP SOON...");
        m_status->setString(status.c_str());ui::fitMenuLabelWidth(m_status,getContentSize().width-24,.28f,.20f);
        if(!elimination){if(mapWindow)showMapInformation(view);else showParticipants(view,result);}
        if(m_confirmation && (result || view.state=="CANCELLED" || m_confirmationContext!=view.id+":"+view.roundKey+":"+std::to_string(view.mapGeneration))){if(auto popup=m_confirmation->lock())popup->cancel();m_confirmation.reset();}
        if(view.state=="PREPARING" && RankedRoundPreparation::get().ready())runtime.assetsReady(level);
        auto const jukeboxOpen=m_jukebox && m_jukebox->lock() && m_jukebox->lock()->getParent();
        if(intro && now>=m_introUntil && !m_confirmation && !debugPopup)runtime.presentationComplete();
        if(!m_motion && !elimination && !m_confirmation && !jukeboxOpen && !debugPopup && !runtime.creating() &&
            mapWindow && m_mapShownAt!=std::chrono::steady_clock::time_point{} && now>=m_mapShownAt+std::chrono::milliseconds(650) &&
            runtime.canEnter() && m_launchedContext!=context && RankedRoundPreparation::get().launch(CCDirector::sharedDirector()->getRunningScene()))m_launchedContext=context;
        if(result && !elimination)beginResultMotion();
    }
    void onEnter() override {
        CCLayerColor::onEnter();
        // Native Quit/Resume may have paused the FMOD output while this retained
        // scene was covered. Reclaim menu/result audio after native teardown.
        RankedAudioManager::get().onApplicationResume();syncAudio(SurvivalRuntime::get().view());
    }
    void keyBackClicked() override {
        if(m_closed || m_motion || std::chrono::steady_clock::now()<m_eliminationUntil)return;
        if(SurvivalRuntime::get().view().result.isObject()){SurvivalRuntime::get().acknowledge();return;}
        m_closed=true;queueInMainThread([]{CCDirector::sharedDirector()->popScene();});
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
    if(auto* play=PlayLayer::get();play && SurvivalRuntime::get().gameplayLevel(static_cast<int>(play->m_level->m_levelID)))return;
    auto* layer=SurvivalGameLayer::create();if(!layer)return;auto* scene=CCScene::create();scene->addChild(layer);director->pushScene(scene);
}
}
