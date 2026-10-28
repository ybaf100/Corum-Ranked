#include "domain/SurvivalRoundPresentation.hpp"
#include "replay/SurvivalReplayRecorder.hpp"
#include "SurvivalSpectatorRuntime.hpp"
#include "SurvivalRuntime.hpp"
#include "RankedRuntime.hpp"
#include "RankedRoundPreparation.hpp"
#include "RankedLevelPreparation.hpp"
#include "SurvivalSceneBridge.hpp"
#include "ui/RankedTranslations.hpp"
#include <Geode/binding/PlayLayer.hpp>
using namespace geode::prelude;
namespace corum::ranked {
namespace {web::WebRequest request(){web::WebRequest r;r.header("Authorization","Bearer "+RankedRuntime::get().spectatorLiveSessionToken());r.followRedirects(false);r.timeout(std::chrono::seconds(3));return r;}}
SurvivalSpectatorRuntime& SurvivalSpectatorRuntime::get(){static auto* r=new SurvivalSpectatorRuntime;return *r;}
std::string SurvivalSpectatorRuntime::path()const{return "https://api.52-74-37-130.sslip.io/api/ranked/survival/online/matches/"+SurvivalRuntime::get().view().id+"/spectator-live";}
void SurvivalSpectatorRuntime::selectionVisible(bool visible){m_selectionVisible=visible;if(visible){m_nextTargets={};refreshTargets();}}
void SurvivalSpectatorRuntime::openTargets(){
    auto& sur=SurvivalRuntime::get();if(!sur.officialRoundCleared()||sur.freePlaying())return;
    if(PlayLayer::get() || sur.freePlaying()){
        m_openAfterExit=true;m_pendingTarget.clear();m_exitRequested=true;
        if(sur.freePlaying())sur.returnFreePlayForSpectator();
        return;
    }
    sur.chooseOutcome(SurvivalOutcomeChoice::LiveSpectate);showSurvivalSpectatorTargets();
}
void SurvivalSpectatorRuntime::refreshTargets(){
    auto const& v=SurvivalRuntime::get().view();auto const session=RankedRuntime::get().spectatorLiveSessionToken();
    auto const listContext=SurvivalRuntime::get().optionsContext();
    if(m_targetsContext!=listContext){m_targetsContext=listContext;m_targets.clear();m_error.clear();}
    if(m_busy || !v.online || session.empty() || !SurvivalRuntime::get().officialRoundCleared() || SurvivalRuntime::get().freePlaying())return;
    m_busy=true;auto const generation=++m_generation;auto const match=v.id;auto const context=SurvivalRuntime::get().optionsContext();
    m_request.spawn(request().get(path()+"/targets"),[this,generation,match,context,session](web::WebResponse response){
        if(generation!=m_generation)return;m_busy=false;
        if(session!=RankedRuntime::get().spectatorLiveSessionToken()||match!=SurvivalRuntime::get().view().id||context!=SurvivalRuntime::get().optionsContext())return;
        m_nextTargets=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        if(response.code()<200||response.code()>=300){m_error=i18n::text("survival-spectator-connection","Survival spectator connection unavailable");return;}
        if(m_target.empty()||m_authorized)m_error.clear();m_targets.clear();auto const root=response.json().unwrapOr(matjson::Value());
        for(auto const& row:root["targets"].asArray().unwrapOr(std::vector<matjson::Value>{})){
            if(m_targets.size()>=64)break;
            m_targets.push_back({row["participantId"].asString().unwrapOr(""),row["name"].asString().unwrapOr(""),row["status"].asString().unwrapOr("WAITING"),
                numFromString<int>(row["gdAccountId"].asString().unwrapOr("0")).unwrapOr(0),static_cast<int>(row["rank"].asInt().unwrapOr(0)),
                row["best"].asDouble().unwrapOr(0),row["now"].asDouble().unwrapOr(0),row["selectable"].asBool().unwrapOr(false),row["kind"].asString().unwrapOr("")=="BOT"});
        }
    });
}
void SurvivalSpectatorRuntime::select(std::string id){
    auto const found=std::find_if(m_targets.begin(),m_targets.end(),[&](auto const&t){return t.id==id&&t.selectable&&!t.bot;});
    if(found==m_targets.end())return;
    if(m_inScene){m_pendingTarget=std::move(id);m_exitRequested=true;return;}
    auto& sur=SurvivalRuntime::get();if(!sur.view().online||!sur.officialRoundCleared()||sur.freePlaying()||PlayLayer::get()||m_worldLoading.valid())return;
    m_authorized=false;m_world.reset();SpectatorLiveRuntime::survival().stopSurvival();
    auto const& v=sur.view();m_request.cancel();m_busy=true;auto const generation=++m_generation;
    m_target=std::move(id);m_targetName=found->name;m_session=RankedRuntime::get().spectatorLiveSessionToken();m_match=v.id;
    m_context={v.id,v.map.levelId,v.roundNumber,0,false,0,v.mapGeneration};auto const key=sur.optionsContext();
    auto url=path()+"?protocolVersion=2&targetId="+m_target+"&roundKey="+v.roundKey+"&mapGeneration="+std::to_string(v.mapGeneration)+"&levelId="+std::to_string(v.map.levelId);
    m_error=i18n::text("survival-spectator-authorizing","Authorizing spectator...");
    m_request.spawn(request().get(url),[this,generation,key](web::WebResponse response){
        if(generation!=m_generation||m_session!=RankedRuntime::get().spectatorLiveSessionToken()||key!=SurvivalRuntime::get().optionsContext())return;
        auto const root=response.json().unwrapOr(matjson::Value());auto const& v=SurvivalRuntime::get().view();
        if(response.code()<200||response.code()>=300||root["targetId"].asString().unwrapOr("")!=m_target||
            root["context"]["roundKey"].asString().unwrapOr("")!=v.roundKey||root["context"]["mapGeneration"].asInt().unwrapOr(0)!=v.mapGeneration||
            root["context"]["levelId"].asInt().unwrapOr(0)!=v.map.levelId){m_busy=false;m_error=i18n::text("survival-spectator-unavailable","Spectator target unavailable");return;}
        m_authorized=true;m_error="Loading verified clear replay...";m_exitRequested=false;
        replay::Json identity;identity["matchId"]=v.id;identity["roundKey"]=v.roundKey;identity["mapGeneration"]=v.mapGeneration;identity["participantId"]=v.ownId;identity["debug"]=v.debug;
        try{m_worldLoading=replay::Recorder::get().storage().playback(identity);}catch(std::exception const&e){m_error=e.what();m_authorized=false;m_busy=false;return;}
        SurvivalRuntime::get().chooseOutcome(SurvivalOutcomeChoice::LiveSpectate);
        SpectatorLiveRuntime::survival().configureSurvival(transport(true));
    });
}
SurvivalSpectatorTransportContext SurvivalSpectatorRuntime::transport(bool viewer)const{
    auto const& sur=SurvivalRuntime::get();auto const& v=sur.view();
    return {v.id,sur.optionsContext()+":"+(viewer?m_target:"publisher"),
        RankedRuntime::get().spectatorLiveSessionToken(),v.roundKey,viewer?m_target:v.ownId,sur.spectatorAcceptedAttempt(),v.roundNumber,v.map.levelId,v.mapGeneration,viewer,!viewer};
}
bool SurvivalSpectatorRuntime::publisherAllowed()const{auto const& sur=SurvivalRuntime::get();auto const& v=sur.view();return v.online&&v.spectatorDemand&&!v.ownEliminated&&v.state=="PLAYING"&&!sur.freePlaying()&&!m_authorized&&!sur.spectatorAcceptedAttempt().empty();}
bool SurvivalSpectatorRuntime::publishing()const{return publisherAllowed()&&SurvivalRuntime::get().spectatorAcceptedAttempt()==SurvivalRuntime::get().localAttemptId();}
bool SurvivalSpectatorRuntime::capture(SpectatorGhostPose const& pose){if(!publishing())return false;auto& live=SpectatorLiveRuntime::survival();live.configureSurvival(transport(false));return live.tickPublisher(pose,false);}
void SurvivalSpectatorRuntime::terminal(SpectatorGhostPose const& pose){auto const& sur=SurvivalRuntime::get();if(!sur.view().online||!sur.view().spectatorDemand||sur.freePlaying()||sur.view().ownEliminated)return;m_terminal=pose;m_pendingTerminalAttempt=sur.localAttemptId();m_terminalUntil=std::chrono::steady_clock::now()+std::chrono::milliseconds(1500);}
bool SurvivalSpectatorRuntime::currentScope()const{
    auto const& v=SurvivalRuntime::get().view();
    auto const current=LevelPreparationContext{v.id,v.map.levelId,v.roundNumber,0,false,0,v.mapGeneration};
    return current==m_context && SurvivalRuntime::get().officialRoundCleared() && !SurvivalRuntime::get().freePlaying() && v.state=="PLAYING" && m_session==RankedRuntime::get().spectatorLiveSessionToken() &&
        std::find(v.aliveIds.begin(),v.aliveIds.end(),m_target)!=v.aliveIds.end();
}
bool SurvivalSpectatorRuntime::sceneLevel(int level)const{return m_inScene && m_authorized && m_context.levelId==level && currentScope();}
bool SurvivalSpectatorRuntime::canLaunch()const{return m_authorized&&m_world&&!m_inScene&&!m_exitRequested&&currentScope()&&!SurvivalRuntime::get().freePlaying();}
bool SurvivalSpectatorRuntime::arm(GJGameLevel* level){if(!canLaunch()||!SurvivalRuntime::get().validateNative(level))return false;m_inScene=true;return true;}
void SurvivalSpectatorRuntime::freePlay(){m_startFreePlay=true;leave();}
void SurvivalSpectatorRuntime::leave(bool chooseAgain){m_openAfterExit=chooseAgain;m_pendingTarget.clear();m_exitRequested=true;if(!m_inScene)sceneExited();}
void SurvivalSpectatorRuntime::sceneExited(){
    m_inScene=false;m_authorized=false;m_world.reset();++m_generation;m_busy=false;m_request.cancel();
    SpectatorLiveRuntime::survival().stopSurvival();
    if(!m_match.empty()&&m_match==SurvivalRuntime::get().view().id&&m_session==RankedRuntime::get().spectatorLiveSessionToken())m_leave.spawn(request().post(path()+"/leave"),[](web::WebResponse){});
    m_context={};m_target.clear();m_error.clear();m_exitRequested=false;
}
std::string SurvivalSpectatorRuntime::hudText()const{
    auto const& v=SurvivalRuntime::get().view();auto const left=SurvivalRuntime::get().roundRemainingMs().value_or(0)/1000;
    auto const status=SpectatorLiveRuntime::survival().status();auto const pose=SpectatorLiveRuntime::survival().presentationFrame();
    std::string state=status==SpectatorLiveStatus::Live?"LIVE":status==SpectatorLiveStatus::Syncing?"SYNCING":status==SpectatorLiveStatus::PartialSync?"PARTIAL SYNC":status==SpectatorLiveStatus::Reconnecting?"RECONNECTING":status==SpectatorLiveStatus::Buffering?"BUFFERING":status==SpectatorLiveStatus::Cleared?"CLEARED":status==SpectatorLiveStatus::RecaptureRequired?"RECAPTURE REQUIRED":status==SpectatorLiveStatus::Disconnected?"DISCONNECTED":"WAITING FOR PLAYER";
    if(m_world){if(m_world->desync())state="REPLAY DESYNC";else if(m_world->seeking())state="SYNCING";else if(state=="LIVE" || m_world->partial())state="PARTIAL SYNC";}
    if(pose&&pose->pose&&pose->pose->dead&&(!m_world||!m_world->desync()))state="PLAYER DOWN";
    double best=0,now=0;for(auto const&r:v.standings)if(r.id==m_target){best=r.bestProgress;now=r.currentProgress;}
    return fmt::format("SPECTATING · {}\n{} · {:02}:{:02} · {}\nCUTOFF {:.2f}% · BEST {:.2f}% · NOW {:.2f}%",m_targetName,survivalRoundLabel(v.phase,v.roundNumber),left/60,left%60,state,v.cutoff.value_or(0),best,now);
}
void SurvivalSpectatorRuntime::tick(){
    if(m_ticking)return;m_ticking=true;struct Guard{bool& b;~Guard(){b=false;}}guard{m_ticking};
    auto& sur=SurvivalRuntime::get();auto const& v=sur.view();auto const now=std::chrono::steady_clock::now();
    auto scope=sur.optionsContext();
    if(!m_lifecycleContext.empty()&&scope!=m_lifecycleContext){
        ++m_generation;m_request.cancel();m_busy=false;m_selectionVisible=false;m_targets.clear();m_targetsContext.clear();
        m_pendingTarget.clear();m_openAfterExit=m_startFreePlay=false;m_terminal.reset();
        if(m_inScene)m_exitRequested=true;
        else {m_authorized=false;m_world.reset();m_worldLoading={};m_context={};m_target.clear();SpectatorLiveRuntime::survival().stopSurvival();}
    }
    m_lifecycleContext=std::move(scope);
    if(m_worldLoading.valid()&&m_worldLoading.wait_for(std::chrono::seconds(0))==std::future_status::ready){
        try{auto document=m_worldLoading.get();if(!m_authorized||!currentScope())throw std::runtime_error("Spectator round changed");
            if(!document||replay::number(document->metadata(),"officialBestProgress")!=100||replay::str(document->metadata(),"mapGeometrySha256")!=v.certificate["geometrySha256"].asString().unwrapOr("")||
               replay::integer(document->metadata(),"mapVersion")!=v.map.version||replay::str(document->metadata(),"captureCompleteness")!="COMPLETE")throw std::runtime_error("A complete verified clear replay of this map is required on this device");
            m_world=std::move(document);m_error.clear();m_busy=false;
        }catch(std::exception const&e){m_error=e.what();m_authorized=false;m_busy=false;SpectatorLiveRuntime::survival().stopSurvival();}
    }
    if(m_selectionVisible&&now>=m_nextTargets)refreshTargets();
    if(m_terminal){if(now>=m_terminalUntil)m_terminal.reset();else if(publisherAllowed()&&m_pendingTerminalAttempt==sur.spectatorAcceptedAttempt()){
        auto& live=SpectatorLiveRuntime::survival();live.configureSurvival(transport(false));live.publishTerminal(*m_terminal,false);m_terminal.reset();}}
    if(publisherAllowed())SpectatorLiveRuntime::survival().pumpSurvivalPublisher();
    if(m_authorized){
        auto const context=LevelPreparationContext{v.id,v.map.levelId,v.roundNumber,0,false,0,v.mapGeneration};
        bool targetAlive=false;for(auto const&p:v.participants.asArray().unwrapOr(std::vector<matjson::Value>{}))if(p["participantId"].asString().unwrapOr("")==m_target&&p["eliminationStage"].isNull())targetAlive=true;
        if(m_session!=RankedRuntime::get().spectatorLiveSessionToken()||context!=m_context||v.state!="PLAYING"||!targetAlive||!SpectatorLiveRuntime::survival().survivalViewerAuthorized()){
            m_exitRequested=true;m_openAfterExit=sur.officialRoundCleared();
        }
    }
    if(m_inScene&&m_exitRequested){if(auto* layer=PlayLayer::get())requestSurvivalGameplayReturn(layer);return;}
    if(!m_inScene&&m_exitRequested)sceneExited();
    if(!m_inScene&&!PlayLayer::get()&&!CCDirector::sharedDirector()->getNextScene()){
        if(m_startFreePlay){
            if(!v.ownEliminated){m_startFreePlay=false;sur.chooseOutcome(SurvivalOutcomeChoice::Undecided);sur.beginFreePlay();return;}
            sur.chooseOutcome(SurvivalOutcomeChoice::Observe);
            if(sur.canEnterObserver()&&RankedRoundPreparation::get().ready()){
                auto* origin=CCDirector::sharedDirector()->getRunningScene();
                if(origin&&RankedRoundPreparation::get().launch(origin))m_startFreePlay=false;
            }
            if(v.state!="PLAYING")m_startFreePlay=false;
            return;
        }
        if(!m_pendingTarget.empty()){auto id=std::exchange(m_pendingTarget,{});select(std::move(id));return;}
        if(m_openAfterExit&&!sur.freePlaying()){m_openAfterExit=false;openTargets();return;}
        if(canLaunch()&&RankedRoundPreparation::get().ready()){
            auto* scene=CCDirector::sharedDirector()->getRunningScene();
            if(scene&&scene->getChildByID("survival-game-coordinator"_spr))RankedRoundPreparation::get().launch(scene);
        }
    }
}
}
