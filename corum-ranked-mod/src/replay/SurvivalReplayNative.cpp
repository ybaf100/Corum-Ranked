#include "../spectator/CorumRemotePlayer.hpp"
#include "../ui/RankedTranslations.hpp"
#include "../SurvivalSpectatorRuntime.hpp"
#include "SurvivalReplayRuntime.hpp"
#include "SurvivalReplayEnvironment.hpp"
#include "SurvivalReplayInputBoundary.hpp"
#include "../SurvivalRuntime.hpp"
#include "../SurvivalSceneBridge.hpp"
#include "../spectator/SpectatorPlayerCapture.hpp"
#include "../spectator/SpectatorPoseCodec.hpp"
#include "../ui/RankedVisuals.hpp"
#include "../domain/SpectatorAudioPolicy.hpp"
#include "../spectator/SpectatorNativeWorldEvents.hpp"
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PlayerButtonCommand.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/DashRingObject.hpp>
using namespace geode::prelude;
namespace {
using corum::ranked::replay::Json;
Json replayMotion(PlayerObject* player){
    if(!player)return nullptr;auto value=Json::array();
    auto add=[&](auto v){value.push(corum::ranked::replay::replayPhysicsValue(v));};
    add(player->getPositionX());add(player->getPositionY());add(player->getRotation());
    add(player->m_yVelocity);add(player->m_shipRotation.x);add(player->m_shipRotation.y);
    add(player->m_jumpBuffered);add(player->m_wasJumpBuffered);add(player->m_isOnGround);
    add(player->m_isShip);add(player->m_isBall);add(player->m_isBird);add(player->m_isDart);
    add(player->m_isRobot);add(player->m_isSpider);add(player->m_isSwing);add(player->m_isUpsideDown);
    add(player->m_isDashing);add(player->m_isOnSlope);add(player->m_gravity);add(player->m_isDead);
    for(int button=0;button<=3;++button){auto found=player->m_holdingButtons.find(button);add(found!=player->m_holdingButtons.end()&&found->second);}
    return value;
}
Json replayWorldReferences(PlayLayer* layer,PlayerObject* player){
    if(!player)return nullptr;auto refs=Json::object();refs["unresolved"]=false;
    auto object=[&](char const* key,GameObject* value){
        if(!value){refs[key]=nullptr;return;}
        int index=-1;if(layer->m_objects)for(unsigned i=0;i<layer->m_objects->count();++i)if(layer->m_objects->objectAtIndex(i)==value){index=static_cast<int>(i);break;}
        if(index<0){refs["unresolved"]=true;return;}auto identity=Json::array();identity.push(index);identity.push(value->m_objectID);refs[key]=std::move(identity);
    };
    object("dashRing",player->m_dashRing);object("snapped",player->m_objectSnappedTo);
    object("slope",player->m_currentSlope);object("slope2",player->m_currentSlope2);
    object("potentialSlope",player->m_currentPotentialSlope);object("lastGround",player->m_lastGroundObject);
    object("previousGround",player->m_preLastGroundObject);object("collided",player->m_collidedObject);
    object("left",player->m_collidingWithLeft);object("right",player->m_collidingWithRight);
    object("portal",player->m_lastActivatedPortal);
    refs["checkpointPresent"]=player->m_pendingCheckpoint!=nullptr;
    if(player->m_pendingCheckpoint)refs["unresolved"]=true;
    auto floats=Json::array();for(auto v:player->m_playerFollowFloats)floats.push(v);refs["follow"]=std::move(floats);
    auto rings=Json::array();for(auto v:player->m_touchedRings)rings.push(v);auto sorted=rings.asArray().unwrap();std::sort(sorted.begin(),sorted.end(),[](auto const&a,auto const&b){return a.asInt().unwrap()<b.asInt().unwrap();});refs["touchedRings"]=Json::array();for(auto v:sorted)refs["touchedRings"].push(v);
    refs["touchingRingsCount"]=player->m_touchingRings?static_cast<int>(player->m_touchingRings->count()):0;
    if(player->m_touchingRings&&player->m_touchingRings->count()>0)refs["unresolved"]=true;
    return refs;
}
double replayPercent(PlayLayer* layer){return std::max(std::clamp(static_cast<double>(layer->getCurrentPercent()),0.,100.),std::clamp(static_cast<double>(layer->getCurrentPercentInt()),0.,100.));}
matjson::Value replayPose(PlayLayer* layer){
    auto pose=corum::ranked::captureSpectatorPose(layer->m_player1,layer->m_player1&&layer->m_player1->m_isDead,layer->m_hasCompletedLevel,layer->m_gameState.m_isDualMode);
    if(pose.dual&&layer->m_player2){auto second=corum::ranked::captureSpectatorPose(layer->m_player2);pose.player2=corum::ranked::spectatorAsSecondary(second);}
    pose.progress=replayPercent(layer);pose.camera=corum::ranked::SpectatorCameraOrigin{layer->m_gameState.m_cameraPosition.x,layer->m_gameState.m_cameraPosition.y};
    pose.camera->transform=corum::ranked::captureSpectatorCameraTransform(layer);
    auto* audio=FMODAudioEngine::sharedEngine();auto track=std::string(audio->getActiveMusic(0));if(!track.empty())pose.audio=corum::ranked::SpectatorAudioSample{corum::ranked::spectatorTrackHash(track),static_cast<double>(audio->getMusicTimeMS(0))};
    return corum::ranked::encodeSpectatorPose(pose);
}
}
class $modify(CorumSurvivalReplayLayer,PlayLayer){
public:
 struct ReplayCamera {CCPoint origin,second,diff;float zoom=1,angle=0;struct Layer{CCPoint pos;float x=1,y=1,angle=0;};std::array<Layer,3> layers;};
 struct Fields {Json trace=Json::array();std::array<unsigned,2> playerUpdates{};bool preInput=false;std::optional<ReplayCamera> nativeCamera;bool ready=false,replayOwned=false,liveWorldOwned=false,replayAdvancing=false,internalReset=false,desyncReported=false;CCLabelBMFont* replayText=nullptr;CCLabelBMFont* captureError=nullptr;std::string captureErrorText;Slider* replaySlider=nullptr;float replayHudWait=0;unsigned lastRenderFrame=0;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
 double captureUs=0,captureMaxUs=0;unsigned captureFrames=0;
#endif
};
 bool init(GJGameLevel* level,bool useReplay,bool noObjects){
   m_fields->replayOwned=level&&corum::ranked::replay::Runtime::get().owns(static_cast<int>(level->m_levelID));
   m_fields->liveWorldOwned=level&&corum::ranked::SurvivalSpectatorRuntime::get().sceneLevel(static_cast<int>(level->m_levelID))&&corum::ranked::SurvivalSpectatorRuntime::get().world();
   // A failed restore must never leak replay's leased physics into ordinary
   // gameplay. Retry before entering Native init, outside every CBF callback.
   if(!m_fields->replayOwned&&!m_fields->liveWorldOwned&&corum::ranked::replay::cbfReplayHooksHeld()){
     corum::ranked::replay::releaseCbfReplayHooks();
     if(corum::ranked::replay::cbfReplayHooksHeld()){
       log::error("[SurvivalReplayCBF] Native scene admission blocked: CBF_HOOK_RESTORE_FAILED");
       queueInMainThread([]{FLAlertLayer::create("CBF",corum::ranked::i18n::text("replay-cbf-restore-failed","CBF physics could not be restored. Restart Geometry Dash before playing.").c_str(),"OK")->show();});
       return false;
     }
   }
   if(!PlayLayer::init(level,useReplay,noObjects)){if(m_fields->replayOwned)corum::ranked::replay::Runtime::get().exited();if(m_fields->liveWorldOwned)corum::ranked::SurvivalSpectatorRuntime::get().sceneExited();return false;}
   if(m_fields->replayOwned){m_isTestMode=true;resetReplayInitial();addReplayHud();}
   if(m_fields->liveWorldOwned)resetReplayInitial();m_fields->ready=true;return true;
 }
 corum::ranked::replay::Playback* worldPlayback(){return m_fields->liveWorldOwned?corum::ranked::SurvivalSpectatorRuntime::get().world():corum::ranked::replay::Runtime::get().playback();}
 void resetLevel(){if((m_fields->replayOwned||m_fields->liveWorldOwned)&&!m_fields->internalReset&&m_fields->ready)return;PlayLayer::resetLevel();}
    void showCaptureError(){
        auto& recorder=corum::ranked::replay::Recorder::get();auto state=recorder.notice();
        using Notice=corum::ranked::replay::ReplayNotice;
        if(state==Notice::None){if(m_fields->captureError)m_fields->captureError->setVisible(false);m_fields->captureErrorText.clear();return;}
        auto text=std::to_string(static_cast<int>(state))+recorder.error();if(text==m_fields->captureErrorText)return;m_fields->captureErrorText=text;
        auto size=CCDirector::sharedDirector()->getWinSize();
        if(!m_fields->captureError){m_fields->captureError=corum::ranked::ui::menuLabel("",.19f,{size.width/2,38},corum::ranked::ui::Red);addChild(m_fields->captureError,1500);}
        auto key=state==Notice::Preparation?"replay-preparation-failed":state==Notice::Data?"replay-data-error":state==Notice::Space?"replay-space-error":state==Notice::Receipt?"replay-receipt-pending":state==Notice::Unsupported?"replay-physics-unsupported":"replay-file-error";
        auto fallback=state==Notice::Preparation?"Replay preparation failed":state==Notice::Data?"Replay data error":state==Notice::Space?"Not enough replay storage":state==Notice::Receipt?"Awaiting official replay confirmation":state==Notice::Unsupported?"Replay capture unavailable for this physics environment":"Replay file could not be saved";
        auto message=corum::ranked::i18n::text(key,fallback);m_fields->captureError->setString(message.c_str());m_fields->captureError->setVisible(true);m_fields->captureError->setColor(state==Notice::Receipt?corum::ranked::ui::Gold:corum::ranked::ui::Red);
        corum::ranked::ui::fitFullMenuLabel(m_fields->captureError,size.width-24,22,.19f);
    }
    void saveReplayCamera(){
        ReplayCamera state;state.origin=m_gameState.m_cameraPosition;state.second=m_gameState.m_cameraPosition2;state.diff=m_gameState.m_cameraStepDiff;
        state.zoom=m_gameState.m_cameraZoom;state.angle=m_gameState.m_cameraAngle;
        std::array<CCNode*,3> nodes{m_objectLayer,m_inShaderObjectLayer,m_aboveShaderObjectLayer};
        for(std::size_t i=0;i<nodes.size();++i)if(auto* n=nodes[i])state.layers[i]={n->getPosition(),n->getScaleX(),n->getScaleY(),n->getRotation()};
        m_fields->nativeCamera=state;
    }
    void restoreReplayCamera(){
        if(!m_fields->nativeCamera)return;auto const& state=*m_fields->nativeCamera;
        m_gameState.m_cameraPosition=state.origin;m_gameState.m_cameraPosition2=state.second;m_gameState.m_cameraStepDiff=state.diff;
        m_gameState.m_cameraZoom=state.zoom;m_gameState.m_cameraAngle=state.angle;
        std::array<CCNode*,3> nodes{m_objectLayer,m_inShaderObjectLayer,m_aboveShaderObjectLayer};
        for(std::size_t i=0;i<nodes.size();++i)if(auto* n=nodes[i]){auto const& l=state.layers[i];n->setPosition(l.pos);n->setScaleX(l.x);n->setScaleY(l.y);n->setRotation(l.angle);}
        updateCameraBGArt(state.origin,state.zoom);
    }
    void resetReplayInitial(){
        auto* replay=worldPlayback();if(!replay)return;
        m_fields->desyncReported=false;
        auto const& initial=replay->initial();
        auto problem=corum::ranked::replay::replayInitialDataProblem(initial);
        if(problem.empty())problem=corum::ranked::replay::replayPlayerStateProblem(m_player1,initial["player1"],replayWorldReferences(this,m_player1));
        if(problem.empty())problem=corum::ranked::replay::replayPlayerStateProblem(m_player2,initial["player2"],replayWorldReferences(this,m_player2));
        if(problem.empty()&&replay->cbf())problem=corum::ranked::replay::validateCbfInitial(this,initial["cbfInitial"]);
        if(!problem.empty()){replay->reject(problem);return;}
        auto seed=numFromString<std::uint64_t>(initial["randomSeed"].asString().unwrapOr(""));
        auto replaySeed=numFromString<std::uint64_t>(initial["replayRandSeed"].asString().unwrapOr(""));
        if(!seed||!replaySeed){replay->reject("INITIAL_RNG_UNAVAILABLE");return;}
        m_randomSeed=seed.unwrap();m_replayRandSeed=replaySeed.unwrap();
        m_currentStep=static_cast<int>(corum::ranked::replay::integer(initial,"nativeStep"));m_resumeTimer=static_cast<int>(corum::ranked::replay::integer(initial,"resumeTimer"));
        m_started=initial["started"].asBool().unwrap();m_inResetDelay=initial["inResetDelay"].asBool().unwrap();
        m_attemptTime=static_cast<float>(corum::ranked::replay::number(initial,"attemptTime"));m_gameState.m_levelTime=corum::ranked::replay::number(initial,"levelTime");
        m_extraDelta=corum::ranked::replay::number(initial,"extraDelta");m_timestamp=corum::ranked::replay::number(initial,"timestamp");
        m_gameState.m_timeWarp=static_cast<float>(corum::ranked::replay::number(initial,"timeWarp"));m_gameState.m_isDualMode=initial["dual"].asBool().unwrap();
        m_clickBetweenSteps=initial["clickBetweenSteps"].asBool().unwrap();m_clickOnSteps=initial["clickOnSteps"].asBool().unwrap();m_isBetweenSteps=initial["isBetweenSteps"].asBool().unwrap();
        corum::ranked::replay::applyReplayPlayerState(m_player1,initial["player1"]);corum::ranked::replay::applyReplayPlayerState(m_player2,initial["player2"]);
        auto const queued=initial["queued"].asArray().unwrap();m_queuedButtons.clear();for(auto const& value:queued)m_queuedButtons.push_back(corum::ranked::replay::decodeReplayButton<PlayerButtonCommand>(value));
        if(replay->cbf())corum::ranked::replay::restoreCbfInitial(this,initial["cbfInitial"]);
        m_fields->desyncReported=false;saveReplayCamera();
    }
    void resetReplayTrace(){m_fields->trace=Json::array();m_fields->playerUpdates={};}
    Json physicsEvidence(bool detailed=false){
        auto value=Json::object();value["p1"]=replayMotion(m_player1);value["p2"]=m_gameState.m_isDualMode?replayMotion(m_player2):Json();
        value["nativeStep"]=m_currentStep;value["timestamp"]=m_timestamp;value["extraDelta"]=m_extraDelta;
        value["randomSeed"]=std::to_string(m_randomSeed);value["replayRandSeed"]=std::to_string(m_replayRandSeed);
        value["pendingButtons"]=corum::ranked::replay::encodeReplayButtons(m_queuedButtons);
        auto count=Json::array();for(auto n:m_fields->playerUpdates)count.push(static_cast<std::int64_t>(n));value["playerUpdates"]=(corum::ranked::replay::Recorder::get().cbfCapture()||((m_fields->replayOwned||m_fields->liveWorldOwned)&&worldPlayback()&&worldPlayback()->cbf()))?corum::ranked::replay::cbfNativeUpdateCounts():std::move(count);if(detailed)value["trace"]=m_fields->trace;return value;
    }
    void reportReplayFailure(Json before=nullptr){
        auto* replay=worldPlayback();if(!replay)return;
        if(replay->desync()&&!m_fields->desyncReported){m_fields->desyncReported=true;log::warn("[SurvivalReplayDesync] attemptId={} initial={} failure={} actualBefore={} actualAfter={} environment={}",corum::ranked::replay::str(replay->metadata(),"attemptId"),replay->initial().dump(matjson::NO_INDENTATION),replay->failure().dump(matjson::NO_INDENTATION),before.dump(matjson::NO_INDENTATION),physicsEvidence(true).dump(matjson::NO_INDENTATION),corum::ranked::replay::captureReplayPhysicsEnvironment().dump(matjson::NO_INDENTATION));}
    }
    void onReplayControl(CCObject* sender){
        auto* replay=worldPlayback();if(!replay||!sender)return;
        switch(sender->getTag()){
          case 1:replay->pause(!replay->paused());break;
          case 2:replay->seek(0);break;
          case 3:{auto speed=replay->speed();replay->speed(speed==.5?1:speed==1?2:speed==2?4:.5);break;}
          case 4:replay->seek(std::max(0.,replay->time()-10));break;
          case 5:replay->seek(std::min(replay->duration(),replay->time()+10));break;
          case 6:onQuit();break;
        }
    }
    void onReplaySeek(CCObject*){if(auto* replay=worldPlayback();replay&&m_fields->replaySlider)replay->seek(m_fields->replaySlider->getValue()*replay->duration());}
    void addReplayHud(){
        auto size=CCDirector::sharedDirector()->getWinSize();
        m_fields->replayText=corum::ranked::ui::menuLabel("REPLAY",.25f,{size.width/2,size.height-8},corum::ranked::ui::Text);addChild(m_fields->replayText,1500);
        auto* menu=CCMenu::create();menu->setPosition({0,0});addChild(menu,1500);
        std::array<char const*,6> captions{"PLAY / PAUSE","RESTART","SPEED","-10s","+10s","BACK"};
        auto width=std::min(460.f,size.width-12),cell=width/6;
        for(int i=0;i<6;++i){auto* face=corum::ranked::ui::surface({cell-3,23},{},corum::ranked::ui::Cyan,corum::ranked::ui::Surface,235,true);
          auto* text=corum::ranked::ui::menuLabel(captions[i],.18f,{(cell-3)/2,11.5f},corum::ranked::ui::Text);corum::ranked::ui::fitFullMenuLabel(text,cell-8,18,.18f);face->addChild(text,5);
          auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(CorumSurvivalReplayLayer::onReplayControl));item->setTag(i+1);item->setPosition({(size.width-width)/2+cell*(i+.5f),16});menu->addChild(item);
        }
        m_fields->replaySlider=Slider::create(this,menu_selector(CorumSurvivalReplayLayer::onReplaySeek),.65f);m_fields->replaySlider->setPosition({size.width/2,43});addChild(m_fields->replaySlider,1500);
    }
    void pumpReplay(float dt){
        auto* replay=worldPlayback();if(!replay)return;
        if(m_fields->replayOwned&&m_isPaused){FMODAudioEngine::sharedEngine()->pauseMusic(0);return;}
        if(m_fields->liveWorldOwned){
          auto frame=corum::ranked::SpectatorLiveRuntime::survival().presentationFrame();if(!frame||!frame->pose||!frame->pose->progress)return;
          replay->followRemote(frame->context.attemptId+":"+std::to_string(frame->context.generation)+":"+std::to_string(frame->context.epoch),frame->publisherElapsedMs/1000.,*frame->pose->progress);
        }else replay->accumulate(dt);
        if(replay->takeReset()){m_fields->internalReset=true;PlayLayer::resetLevel();m_fields->internalReset=false;m_isTestMode=m_fields->replayOwned;replay->restart();resetReplayInitial();}
        if(!replay->desync())if(auto problem=corum::ranked::replay::replayPhysicsEnvironmentMismatch(replay->metadata()["physicsEnvironment"],corum::ranked::replay::captureReplayPhysicsEnvironment());!problem.empty())replay->reject(problem);
        auto began=std::chrono::steady_clock::now();
        for(std::size_t n=0;n<corum::ranked::replay::SeekPolicy::MaxFramesPerPump&&replay->due();++n){
            auto const* frame=replay->current();
            if(m_fields->liveWorldOwned)restoreReplayCamera();
            resetReplayTrace();auto before=physicsEvidence();
            if(replay->inputV2()){
                if(!frame->preInputs.isArray()){replay->reject("PRE_FRAME_INPUTS_UNAVAILABLE");break;}
                m_fields->replayAdvancing=true;m_fields->preInput=true;
                auto const preInputs=frame->preInputs.asArray().unwrap();for(auto const& input:preInputs){
                    auto* player=input["player2"].asBool().unwrap()?m_player2:m_player1;
                    if(replayMotion(player)!=input["before"]||m_currentStep!=corum::ranked::replay::integer(input,"nativeStep")||m_timestamp!=corum::ranked::replay::number(input,"timestamp")){replay->reject("PRE_FRAME_INPUT_STATE",input,physicsEvidence());break;}
                    handleButton(input["down"].asBool().unwrap(),static_cast<int>(corum::ranked::replay::integer(input,"button")),!input["player2"].asBool().unwrap());
                    if(replayMotion(player)!=input["after"]){replay->reject("PRE_FRAME_HANDLE_BUTTON",input,replayMotion(player));break;}
                }
                m_fields->preInput=false;m_fields->replayAdvancing=false;
                if(auto problem=corum::ranked::replay::prepareReplayInputBoundary(m_queuedButtons,frame->queued,replay->pendingInputs());!problem.empty())replay->reject(problem,frame->queued,corum::ranked::replay::encodeReplayButtons(m_queuedButtons));
                if(replay->desync())break;
            }
            if(replay->cbf())corum::ranked::replay::beginCbfNativeFrame(this,replay);
            {m_fields->replayAdvancing=true;corum::ranked::advanceSurvivalReplayWorld(this,static_cast<float>(frame->dt));m_fields->replayAdvancing=false;}
            if(replay->cbf())corum::ranked::replay::finishCbfNativeFrame();
            auto pose=replayPose(this);pose["physics"]=physicsEvidence();replay->completeFrame(pose);
            reportReplayFailure(before);
            if(m_fields->liveWorldOwned)saveReplayCamera();
            if(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count()>=corum::ranked::replay::SeekPolicy::MaxPumpMillis)break;
        }
        reportReplayFailure();
        if(m_fields->liveWorldOwned){for(auto* player:{m_player1,m_player2})corum::ranked::CorumRemotePlayer::hideNativeVisuals(player);return;}
        auto* audio=FMODAudioEngine::sharedEngine();auto const* presented=replay->presentedPose();
        if(replay->paused()||replay->seeking()||replay->ended()||replay->desync()||replay->speed()!=1||!presented||!(*presented)["audio"].isObject())audio->pauseMusic(0);
        else{
            auto const& sample=(*presented)["audio"];auto track=std::string(audio->getActiveMusic(0));
            if(!track.empty()&&corum::ranked::spectatorTrackHash(track)==corum::ranked::replay::str(sample,"trackHash")){
                auto expected=corum::ranked::replay::number(sample,"positionMs");if(std::abs(static_cast<double>(audio->getMusicTimeMS(0))-expected)>100)audio->setMusicTimeMS(static_cast<unsigned>(expected),true,0);audio->resumeMusic(0);
            }else audio->pauseMusic(0);
        }
        m_fields->replayHudWait-=dt;if(m_fields->replayHudWait<=0){m_fields->replayHudWait=.1f;
          auto status=replay->desync()?"REPLAY DESYNC":replay->seeking()?"SEEKING":replay->ended()?"FINISHED":replay->paused()?"PAUSED":"PLAYING";
          auto text=fmt::format("{} · {} · {:.1f}/{:.1f}s · {:.1f}x · BEST {:.3f}%",corum::ranked::replay::str(replay->metadata(),"captureCompleteness")=="PARTIAL_RECOVERED"?"PARTIAL REPLAY":"INPUT REPLAY",status,replay->time(),replay->duration(),replay->speed(),replay->metadata()["officialBestProgress"].asDouble().unwrapOr(0));
          m_fields->replayText->setString(text.c_str());corum::ranked::ui::fitFullMenuLabel(m_fields->replayText,CCDirector::sharedDirector()->getWinSize().width-20,20,.25f);
          m_fields->replaySlider->setValue(replay->duration()>0?replay->time()/replay->duration():0);
        }
    }

};
class $modify(CorumSurvivalReplayNative,GJBaseGameLayer){
public:
 static void onModify(auto& self){
    if(!self.setHookPriorityPre("GJBaseGameLayer::update",Priority::Last))log::warn("Replay capture could not acquire post-entry native boundary");
 }

 void update(float dt){
    auto* play=PlayLayer::get();if(!play||static_cast<GJBaseGameLayer*>(play)!=this){GJBaseGameLayer::update(dt);return;}
    auto* replay=static_cast<CorumSurvivalReplayLayer*>(play);
    if(replay->m_fields->replayOwned||replay->m_fields->liveWorldOwned){
      if(!replay->m_fields->ready||replay->m_fields->internalReset||replay->m_fields->replayAdvancing){GJBaseGameLayer::update(dt);return;}
      auto frame=CCDirector::sharedDirector()->getTotalFrames();if(replay->m_fields->lastRenderFrame==frame)return;replay->m_fields->lastRenderFrame=frame;
      replay->pumpReplay(dt);if(replay->m_fields->liveWorldOwned)corum::ranked::presentSurvivalReplayCamera(play);return;
    }
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    auto captureBegan=std::chrono::steady_clock::now();
#endif
    auto& survival=corum::ranked::SurvivalRuntime::get();auto& recorder=corum::ranked::replay::Recorder::get();
    bool official=play->m_level&&survival.gameplayLevel(static_cast<int>(play->m_level->m_levelID))&&!survival.freePlaying()&&!play->m_isPracticeMode&&!play->m_isTestMode&&survival.canPlay();
    bool frameBegan=false,cbfFrameBegan=false;
    if(official&&std::isfinite(dt)&&dt>0&&dt<=1&&!recorder.inNativeFrame()&&!play->m_isPaused&&play->m_player1&&!play->m_player1->m_isDead&&!play->m_hasCompletedLevel){
      if(survival.localAttemptId().empty()&&survival.beginAttempt()&&recorder.active()){
        matjson::Value initial;initial["kind"]="initial";initial["randomSeed"]=std::to_string(m_randomSeed);initial["replayRandSeed"]=std::to_string(m_replayRandSeed);
        initial["physicsSchema"]=corum::ranked::replay::ReplayPhysicsSchema;
        if(recorder.cbfCapture())initial["cbfInitial"]=corum::ranked::replay::captureCbfInitial(play);
        initial["player1"]=corum::ranked::replay::captureReplayPlayerState(play->m_player1,replayWorldReferences(play,play->m_player1));
        initial["player2"]=corum::ranked::replay::captureReplayPlayerState(play->m_player2,replayWorldReferences(play,play->m_player2));
        initial["queued"]=corum::ranked::replay::encodeReplayButtons(m_queuedButtons);initial["timestamp"]=m_timestamp;
        initial["timeWarp"]=play->m_gameState.m_timeWarp;initial["dual"]=play->m_gameState.m_isDualMode;
        initial["clickBetweenSteps"]=m_clickBetweenSteps;initial["clickOnSteps"]=m_clickOnSteps;initial["isBetweenSteps"]=m_isBetweenSteps;
        initial["started"]=play->m_started;initial["inResetDelay"]=play->m_inResetDelay;initial["attemptTime"]=play->m_attemptTime;initial["levelTime"]=static_cast<double>(play->m_gameState.m_levelTime);initial["extraDelta"]=m_extraDelta;initial["nativeStep"]=m_currentStep;initial["resumeTimer"]=m_resumeTimer;initial["pose"]=replayPose(play);auto problem=corum::ranked::replay::replayInitialDataProblem(initial);if(problem.empty()&&recorder.cbfCapture()&&!corum::ranked::replay::cbfInitialDataValid(initial["cbfInitial"]))problem="CBF_INITIAL_STATE_UNAVAILABLE";if(!problem.empty())recorder.unsupported(problem);else recorder.add(initial);
      }
      if(recorder.active()&&recorder.physicsEnvironmentUnchanged()){if(recorder.cbfCapture()){corum::ranked::replay::beginCbfNativeFrame(play);cbfFrameBegan=true;}replay->resetReplayTrace();corum::ranked::beginNativeWorldEventCapture(play);recorder.nativeFrameBegin();frameBegan=true;recorder.elapsed(play->m_attemptTime);matjson::Value frame;frame["kind"]="frame";frame["dt"]=static_cast<double>(dt);frame["queued"]=corum::ranked::replay::encodeReplayButtons(m_queuedButtons);frame["preInputs"]=recorder.takePreInputs();recorder.add(frame);}
    }
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    auto beforePhysics=std::chrono::steady_clock::now();
#endif
    GJBaseGameLayer::update(dt);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    auto afterPhysics=std::chrono::steady_clock::now();
#endif
    if(cbfFrameBegan){auto cbf=corum::ranked::replay::finishCbfNativeFrame();if(auto error=corum::ranked::replay::cbfNativeFrameError();!error.empty())recorder.unsupported(error);else recorder.add(std::move(cbf));}
    if(frameBegan&&recorder.active()){auto pose=replayPose(play);pose["physics"]=replay->physicsEvidence();pose["kind"]="pose";recorder.elapsed(play->m_attemptTime);recorder.add(pose);}
    if(frameBegan)recorder.nativeFrameEnd();
    if(official)replay->showCaptureError();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if(official){auto us=std::chrono::duration<double,std::micro>(beforePhysics-captureBegan+std::chrono::steady_clock::now()-afterPhysics).count();
      replay->m_fields->captureUs+=us;replay->m_fields->captureMaxUs=std::max(replay->m_fields->captureMaxUs,us);
      if(++replay->m_fields->captureFrames==120){log::info("[SurvivalReplayCapture] active={} nativeFrameCaptured={} tick={} boundaryMeanUs={} boundaryMaxUs={} writesUseWorker=true",recorder.active(),frameBegan,recorder.tick(),replay->m_fields->captureUs/120,replay->m_fields->captureMaxUs);replay->m_fields->captureFrames=0;replay->m_fields->captureUs=replay->m_fields->captureMaxUs=0;}}
#endif
 }
    void handleButton(bool down,int button,bool player1){
        auto* play=PlayLayer::get();auto* owner=play&&static_cast<GJBaseGameLayer*>(play)==this?static_cast<CorumSurvivalReplayLayer*>(play):nullptr;
        // Only the synchronous replay simulation may dispatch recorded native commands.
        if(owner&&(owner->m_fields->replayOwned||owner->m_fields->liveWorldOwned)&&owner->m_fields->ready&&!owner->m_fields->internalReset&&!owner->m_fields->replayAdvancing)return;
        auto& recorder=corum::ranked::replay::Recorder::get();
        bool trace=owner&&(owner->m_fields->replayAdvancing||(recorder.active()&&play->m_level&&corum::ranked::SurvivalRuntime::get().gameplayLevel(static_cast<int>(play->m_level->m_levelID))&&!corum::ranked::SurvivalRuntime::get().freePlaying()&&!play->m_isPracticeMode&&!play->m_isTestMode));
        auto* player=trace?(player1?play->m_player1:play->m_player2):nullptr;
        Json input;if(trace){input["button"]=button;input["down"]=down;input["player2"]=!player1;input["nativeStep"]=m_currentStep;input["timestamp"]=m_timestamp;input["before"]=replayMotion(player);}
        GJBaseGameLayer::handleButton(down,button,player1);
        if(trace){input["after"]=replayMotion(player);
            if(owner->m_fields->replayAdvancing){if(!owner->m_fields->preInput)if(auto* playback=owner->worldPlayback())playback->nativeDispatch(input);}
            else recorder.nativeInput(std::move(input));
        }
    }
    void processCommands(float dt,bool half,bool last){
        auto* play=PlayLayer::get();auto* owner=play&&static_cast<GJBaseGameLayer*>(play)==this?static_cast<CorumSurvivalReplayLayer*>(play):nullptr;
        if(owner&&owner->m_fields->replayAdvancing){if(auto* replay=owner->worldPlayback())replay->nativeStep(dt,half,last,m_currentStep);}
        if(owner&&corum::ranked::SurvivalRuntime::get().gameplayLevel(static_cast<int>(play->m_level->m_levelID)) && !corum::ranked::SurvivalRuntime::get().freePlaying() && !play->m_isPracticeMode && !play->m_isTestMode&&corum::ranked::replay::Recorder::get().active()&&corum::ranked::replay::Recorder::get().inNativeFrame()){
            auto& recorder=corum::ranked::replay::Recorder::get();recorder.nextTick();matjson::Value step;step["kind"]="step";step["dt"]=static_cast<double>(dt);step["half"]=half;step["last"]=last;step["nativeStep"]=m_currentStep;recorder.add(step);
        }
        GJBaseGameLayer::processCommands(dt,half,last);
    }
    void processQueuedButtons(float dt,bool clear){
        auto* play=PlayLayer::get();auto* owner=play&&static_cast<GJBaseGameLayer*>(play)==this?static_cast<CorumSurvivalReplayLayer*>(play):nullptr;
        auto recording=owner&&corum::ranked::SurvivalRuntime::get().gameplayLevel(static_cast<int>(play->m_level->m_levelID)) && !corum::ranked::SurvivalRuntime::get().freePlaying() && !play->m_isPracticeMode && !play->m_isTestMode&&corum::ranked::replay::Recorder::get().active()&&corum::ranked::replay::Recorder::get().inNativeFrame();
        auto encodeQueue=[](auto const& commands){return corum::ranked::replay::encodeReplayButtons(commands);};
        auto* playback=owner&&owner->m_fields->replayAdvancing?owner->worldPlayback():nullptr;
        // Old files retain consumed input tick/step/timestamp information.
        // Dispatch it at that physics boundary, never at render-frame start.
        if(playback)for(auto const& input:playback->legacyInputsForStep()){
            PlayerButtonCommand c;c.m_button=static_cast<PlayerButton>(input["button"].asInt().unwrapOr(1));c.m_isPush=input["down"].asBool().unwrapOr(false);c.m_isPlayer2=input["player2"].asBool().unwrapOr(false);c.m_step=static_cast<int>(input["nativeStep"].asInt().unwrapOr(0));c.m_timestamp=input["timestamp"].asDouble().unwrapOr(0);m_queuedButtons.push_back(c);
        }
        auto const* expected=playback?playback->nativeButtons(dt,clear,m_currentStep):nullptr;
        if(expected&&playback->inputV2()){if((*expected)["queued"]!=encodeQueue(m_queuedButtons))playback->reject("INPUT_QUEUE_BEFORE_CONSUMPTION",(*expected)["queued"],encodeQueue(m_queuedButtons));}
        if(expected&&!playback->inputV2()){m_queuedButtons.clear();auto const queued=(*expected)["queued"].asArray().unwrapOr(std::vector<matjson::Value>{});for(auto const& input:queued){
            PlayerButtonCommand c;c.m_button=static_cast<PlayerButton>(input["button"].asInt().unwrapOr(1));c.m_isPush=input["down"].asBool().unwrapOr(false);c.m_isPlayer2=input["player2"].asBool().unwrapOr(false);c.m_step=static_cast<int>(input["nativeStep"].asInt().unwrapOr(0));c.m_timestamp=input["timestamp"].asDouble().unwrapOr(0);m_queuedButtons.push_back(c);
        }}
        if(playback&&playback->desync())return;
        auto before=recording?m_queuedButtons:gd::vector<PlayerButtonCommand>{};
        GJBaseGameLayer::processQueuedButtons(dt,clear);
        if(expected)playback->nativeButtonsConsumed((*expected)["remaining"],encodeQueue(m_queuedButtons));
        if(recording){
            matjson::Value queue;queue["kind"]="buttons";queue["dt"]=static_cast<double>(dt);queue["clear"]=clear;queue["nativeStep"]=m_currentStep;queue["queued"]=encodeQueue(before);queue["remaining"]=encodeQueue(m_queuedButtons);corum::ranked::replay::Recorder::get().add(queue);
            auto remaining=m_queuedButtons;
            for(auto const& command:before){
                auto retained=std::find_if(remaining.begin(),remaining.end(),[&](auto const& other){return other.m_step==command.m_step&&other.m_timestamp==command.m_timestamp&&other.m_button==command.m_button&&other.m_isPush==command.m_isPush&&other.m_isPlayer2==command.m_isPlayer2;});
                if(retained!=remaining.end()){remaining.erase(retained);continue;}
                matjson::Value input;input["kind"]="input";input["button"]=static_cast<int>(command.m_button);input["down"]=command.m_isPush;input["player2"]=command.m_isPlayer2;input["nativeStep"]=command.m_step;input["timestamp"]=command.m_timestamp;corum::ranked::replay::Recorder::get().add(input);
            }
        }
    }
};

namespace corum::ranked {
void survivalReplayPlayerStep(PlayerObject* player,float dt,bool before){
    auto* play=PlayLayer::get();if(!play||!player||(player!=play->m_player1&&player!=play->m_player2))return;
    auto* owner=static_cast<CorumSurvivalReplayLayer*>(play);
    if(!owner->m_fields->replayAdvancing&&!replay::Recorder::get().inNativeFrame())return;
    auto index=player==play->m_player2?1:0;if(before)++owner->m_fields->playerUpdates[index];
    auto sample=Json::array();sample.push(index);sample.push(before);sample.push(dt);sample.push(play->m_currentStep);sample.push(replayMotion(player));
    auto trace=owner->m_fields->trace.asArray().unwrap();if(trace.size()==8)trace.erase(trace.begin());trace.push_back(std::move(sample));
    owner->m_fields->trace=Json::array();for(auto& item:trace)owner->m_fields->trace.push(std::move(item));
}
void survivalReplayNativeSnap(PlayerObject* player,bool spider,float fromX,float fromY){
    auto* play=PlayLayer::get();if(!play||!player||(player!=play->m_player1&&player!=play->m_player2))return;
    auto* owner=static_cast<CorumSurvivalReplayLayer*>(play);replay::Json anchor;
    anchor["kind"]="anchor";anchor["event"]=spider?"SPIDER_SNAP":"TELEPORT";anchor["player2"]=player==play->m_player2;
    anchor["fromX"]=static_cast<double>(fromX);anchor["fromY"]=static_cast<double>(fromY);
    anchor["toX"]=static_cast<double>(player->getPositionX());anchor["toY"]=static_cast<double>(player->getPositionY());
    if(owner->m_fields->replayAdvancing){if(auto* world=owner->worldPlayback())world->nativeAnchor(anchor);return;}
    if(play->m_level&&!play->m_isPracticeMode&&!play->m_isTestMode&&SurvivalRuntime::get().gameplayLevel(static_cast<int>(play->m_level->m_levelID))&&!SurvivalRuntime::get().freePlaying()&&replay::Recorder::get().active())replay::Recorder::get().add(anchor);
}
void finishSurvivalReplayForReturn(PlayLayer* play){
    if(!play||play!=PlayLayer::get())return;
    replay::Recorder::get().finish(SurvivalRuntime::get().surrenderPending()?"SURRENDER":SurvivalRuntime::get().roundRemainingMs().value_or(0)==0?"ROUND_TIMEOUT":"ROUND_TRANSITION",replayPercent(play));
}
}
