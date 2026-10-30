#include "SurvivalReplayRuntime.hpp"
#include "SurvivalReplayEnvironment.hpp"
#include "../HistoryLevelLookup.hpp"
#include "../RankedRoundPreparation.hpp"
#include "../RankedLevelPreparation.hpp"
#include "../RankedAudioManager.hpp"
#include "../ui/RankedVisuals.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/cocos/support/zip_support/ZipUtils.h>
using namespace geode::prelude;
namespace corum::ranked::replay {
Runtime& Runtime::get(){static auto* runtime=new Runtime;return *runtime;}
void Runtime::request(Json identity){
    if(!canOpenHistoryLevel()){m_error="Finish or leave the current match before replay";return;}
    if(m_loading.valid()){m_pendingIdentity=std::move(identity);m_requested=false;return;}m_identity=std::move(identity);m_error.clear();m_requested=true;m_scene=false;m_playback.reset();
    try{m_loading=Recorder::get().storage().playback(m_identity);}catch(std::exception const&e){m_error=e.what();}
}
void Runtime::pump(){
    if(!m_loading.valid()||m_loading.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
    try{auto doc=m_loading.get();if(m_pendingIdentity){auto next=std::move(*m_pendingIdentity);m_pendingIdentity.reset();request(std::move(next));return;}if(!m_requested)return;if(!doc){m_error="No replay saved on this device";return;}
      for(auto key:{"matchId","roundKey","participantId","mapGeometrySha256","mapVersion","mapGeneration","levelId","attemptId"})
        if(doc->metadata()[key]!=m_identity[key])throw std::runtime_error("Replay does not match this official record or map version");
      auto problem=replayInitialDataProblem(doc->initial());
      if(problem.empty())problem=replayPhysicsEnvironmentProblem(doc->metadata()["physicsEnvironment"]);
      if(problem.empty())problem=replayPhysicsEnvironmentMismatch(doc->metadata()["physicsEnvironment"],captureReplayPhysicsEnvironment());
      if(!problem.empty())throw std::runtime_error("Native replay unavailable: "+problem+". Saved record and best are preserved.");
      m_playback=std::move(doc);
    }catch(std::exception const&e){m_error=e.what();}
}
LevelPreparationContext Runtime::context()const{
    if(!m_requested||!m_playback)return {};
    return {str(m_identity,"matchId"),static_cast<int>(integer(m_identity,"levelId")),static_cast<int>(integer(m_identity,"roundNumber")),0,false,0,static_cast<int>(integer(m_identity,"mapGeneration"))};
}
void Runtime::cancel(){if(m_scene)return;m_pendingIdentity.reset();m_requested=false;m_playback.reset();}
void Runtime::exited(){releaseCbfReplayHooks();m_scene=false;cancel();}
bool Runtime::validate(GJGameLevel* level){
    if(!level||integer(m_identity,"levelId")!=static_cast<int>(level->m_levelID)||integer(m_identity,"mapVersion")!=level->m_levelVersion||level->m_levelString.empty())return false;
    auto expected=str(m_identity,"mapGeometrySha256");std::string raw=level->m_levelString;
    if(sha256Text(raw)==expected)return true;
    auto decoded=std::string(ZipUtils::decompressString(level->m_levelString,false,11).c_str());
    if(sha256Text(decoded)==expected)return true;
    m_error="Recorded map geometry is unavailable or changed";return false;
}
bool Runtime::arm(GJGameLevel* level){
    if(!canLaunch()||!validate(level))return false;
    if(auto problem=replayPhysicsEnvironmentMismatch(m_playback->metadata()["physicsEnvironment"],captureReplayPhysicsEnvironment());!problem.empty()){m_error="Native replay unavailable: "+problem;return false;}
    if(m_playback->cbf())if(auto problem=acquireCbfReplayHooks(captureReplayPhysicsEnvironment());!problem.empty()){m_error=problem;return false;}
    m_scene=true;return true;
}
namespace {
class LaunchPopup final:public Popup{
    CCLabelBMFont* m_label=nullptr;bool m_launch=false;
    bool init(Json identity){if(!Popup::init(300,145))return false;setTitle("SURVIVAL REPLAY");
      m_label=ui::menuLabel("Loading local replay...",.26f,{150,72},ui::Text);m_mainLayer->addChild(m_label);Runtime::get().request(std::move(identity));scheduleUpdate();return true;}
    void update(float)override{
      auto& runtime=Runtime::get();runtime.pump();auto& preparation=RankedRoundPreparation::get();
      auto text=!runtime.error().empty()?runtime.error():!preparation.view().error.empty()?preparation.view().error:"Preparing recorded map / audio...";
      m_label->setString(text.c_str());ui::fitFullMenuLabel(m_label,270,62,.26f);
      if(runtime.canLaunch()&&preparation.ready()){
        m_launch=true;auto* origin=CCDirector::sharedDirector()->getRunningScene();onClose(nullptr);
        queueInMainThread([origin=WeakRef(origin)]{if(auto scene=origin.lock();scene&&scene.data()==CCDirector::sharedDirector()->getRunningScene()){if(!RankedRoundPreparation::get().launch(scene.data()))Runtime::get().cancel();}else Runtime::get().cancel();});
      }
    }
    void onClose(CCObject* sender)override{if(!m_launch)Runtime::get().cancel();Popup::onClose(sender);}
public:static LaunchPopup* create(Json identity){auto*p=new LaunchPopup;if(p->init(std::move(identity))){p->autorelease();return p;}delete p;return nullptr;}
};
}
void showReplay(Json identity){if(auto*p=LaunchPopup::create(std::move(identity)))p->show();}
}
