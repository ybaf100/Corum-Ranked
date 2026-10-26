import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const source=readFileSync(join(root,'src/PlayLayer.cpp'),'utf8');
function method(name,from=0){
    const start=source.indexOf(name,from);assert(start>=0,name);const open=source.indexOf('{',start);
    let depth=1,end=open+1;while(depth){if(source[end]==='{')++depth;if(source[end]==='}')--depth;++end;}return source.slice(start,end);
}
for(const debugOn of [false,true]) test(`native update/rotation/teleport -> completed camera snapshot -> 50ms capture (Debug ${debugOn?'ON':'OFF'})`,()=>{
    const owner=['bool observesSpectatorPlayer(', 'corum::ranked::SpectatorGhostPose observedSpectatorPose(',
        'corum::ranked::SpectatorGhostPose observedSpectatorPlayerPose(', 'void observeSpectatorNativeUpdate(', 'void observeSpectatorNativeSnap(', 'void sampleSpectatorPresentation()'].map(n=>method(n)).join('\n');
    const playerAt=source.indexOf('class $modify(CorumRankedSpectatorDummyPlayer');
    const worldAt=source.indexOf('class $modify(CorumRankedSpectatorCameraHook');
    const capture=readFileSync(join(root,'src/spectator/SpectatorPlayerCapture.cpp'),'utf8').replace(/^#include.*$/gm,'');
    const cpp=`
#include "${join(root,'src/domain/SpectatorCapturePolicy.hpp')}"
#include "${join(root,'src/domain/SpectatorNativeProbe.hpp')}"
#include "${join(root,'src/debug/DebugSpectatePolicy.hpp')}"
#include "${join(root,'src/spectator/SpectatorPlayerCapture.hpp')}"
#include "${join(root,'src/domain/SpectatorCameraPolicy.hpp')}"
#include <cassert>
#include <chrono>
#include <limits>
#include <vector>
using namespace corum::ranked;
static std::int64_t clockMs=0;
namespace geode::log {template<class... T>void info(T...){} }
using namespace geode;
struct CCDirector { static CCDirector* sharedDirector(){static CCDirector d;return &d;} unsigned getTotalFrames()const{return 0;} };
struct ScopedRealGameplayEnd { bool& flag;bool previous;ScopedRealGameplayEnd(bool& f,bool b):flag(f),previous(f){flag=b;}~ScopedRealGameplayEnd(){flag=previous;} };
struct CCPoint{float x=0,y=0;};struct GJBaseGameLayer;
struct MainLayer{float getScaleY()const{return 1;}float getScaleX()const{return 1;}float getRotation()const{return 0;}};
namespace cocos2d{using CCNode=MainLayer;}
struct PlayerObject {
 GJBaseGameLayer* m_gameLayer=nullptr;CCPoint position{100,40};MainLayer main;MainLayer* m_mainLayer=&main;
 bool m_isShip=false,m_isBall=false,m_isBird=false,m_isDart=false,m_isRobot=false,m_isSpider=false,m_isSwing=false;
 bool m_isUpsideDown=false,m_isDead=false,m_wasTeleported=false,moving=false,nativeTeleport=false;
 float m_vehicleSize=1,rotation=0;int updates=0;
 CCPoint getPosition()const{return position;}float getRotation()const{return rotation;}bool isVisible()const{return true;}
 virtual ~PlayerObject()=default;
 virtual void update(float){++updates;if(moving)position.x+=5;if(nativeTeleport){position.x+=729.7268f;m_wasTeleported=true;nativeTeleport=false;}}
 virtual void updateRotation(float){if(moving)rotation+=3;}
 virtual void spiderTestJump(bool){position.y+=700;m_isUpsideDown=!m_isUpsideDown;}
};
struct TeleportPortalObject{};
struct GJBaseGameLayer {
 struct {bool m_isDualMode=false;CCPoint m_cameraPosition;double m_levelTime=0;float m_cameraZoom=1,m_cameraAngle=0,m_targetCameraZoom=1,m_targetCameraAngle=0;} m_gameState;
 MainLayer *m_objectLayer=nullptr,*m_inShaderObjectLayer=nullptr,*m_aboveShaderObjectLayer=nullptr;
 PlayerObject *m_player1=nullptr,*m_player2=nullptr;double m_attemptTime=0;bool m_inResetDelay=false,m_isPaused=false,m_started=false;
 int checkCollisions(PlayerObject*,float,bool){return 0;}
 void updateCamera(float){}
 virtual ~GJBaseGameLayer()=default;
 virtual void update(float dt){clockMs+=std::int64_t(dt*1000);m_attemptTime+=dt;
  if(m_player1){m_player1->update(dt);m_player1->updateRotation(dt);m_gameState.m_cameraPosition={m_player1->position.x-200,m_player1->position.y-100};}}
 void teleportPlayer(TeleportPortalObject*,PlayerObject* p){p->position.x+=729.7268f;p->m_wasTeleported=true;}
};
struct PlayLayer:GJBaseGameLayer {
 bool m_isTestMode=false,m_isPracticeMode=false,m_hasCompletedLevel=false;float getCurrentPercent(){return 12.5f;}
 static inline PlayLayer* current=nullptr;static PlayLayer* get(){return current;}
};
${capture}
namespace corum::ranked {
unsigned captureSpectatorEffects(PlayerObject*,SpectatorGameMode){return 0;}
SpectatorCosmetics captureSpectatorCosmetics(PlayerObject*,PlayerObject*){return {};}
struct RankedRuntime {bool spectator=false;static RankedRuntime& get(){static RankedRuntime r;return r;}bool isSpectating()const{return spectator;}};
struct SpectatorLiveRuntime {
 std::vector<SpectatorRemoteFrame> takeRestorationFrames(){return {};}void restorationApplied(SpectatorRemoteFrame const&,bool,double){}void noteWorldPartial(std::string){}
 bool busy=false;std::int64_t next=0;std::vector<SpectatorGhostPose> sent;
 static SpectatorLiveRuntime& get(){static SpectatorLiveRuntime r;return r;}
 bool tickPublisher(SpectatorGhostPose const& p,bool){if(busy||clockMs<next)return false;sent.push_back(p);next=clockMs+50;return true;}
};
namespace debug {
struct DebugSpectateRuntime {
 DebugSpectateCapture capture;std::vector<SpectatorGhostPose> sent;
 static DebugSpectateRuntime& get(){static DebugSpectateRuntime d;return d;}
 bool recordPose(SpectatorGhostPose const& p){if(!capture.capture(clockMs,p))return false;sent.push_back(p);return true;}
};
}
}
struct CorumRankedPlayLayer:PlayLayer {
 bool blockEntryGameplayUpdate(){return false;}
 struct Fields{struct Driver{bool partialHistory()const{return false;}template<class...T>bool apply(T...){return false;}std::optional<float> takeNativeDeltaSeconds(){return {};}}spectatorWorldDriver;bool spectatorWorldPoseApplied=false,spectatorCollisionChecked=false;SpectatorNativeProbe spectatorNativeProbe;bool rankedInitialized=false,rankedLevel=true,safetyRetired=false,spectatorMode=false,spectatorPending=false;
  corum::ranked::SpectatorSceneContext sceneContext;std::optional<SpectatorRemoteFrame>spectatorRenderedFrame;struct Remote{template<class...T>void update(T...){}}remotePlayer;
  bool debugRecordScene=false,debugPlaybackScene=false,debugRecordHasFrame=false;SpectatorCapturePolicy spectatorCapture;std::shared_ptr<SpectatorCosmetics const> spectatorCosmetics;std::chrono::steady_clock::time_point nextCosmeticCaptureAt{};bool spectatorWorldUpdateInProgress=false,spectatorWorldResetInProgress=false;std::optional<unsigned> spectatorCameraFrameNumber;}fields;
 Fields* m_fields=&fields;
 bool prepareSpectatorScene(){return false;}bool spectatorCameraActive(){return false;}bool beginSpectatorWorldFrame(){return false;}
 void reconcileSpectatorWorldDriver(){}void driveSpectatorCamera(){}void logSpectatorNativeCamera(char const*){}void logSpectatorWorldDriver(){}void logSpectatorNativeProbe(){}
 bool freezeLocalDummy(PlayerObject*)const{return fields.spectatorMode || fields.debugPlaybackScene;}
 ${owner}
};
static CorumRankedPlayLayer* localSafetyLayer(){return static_cast<CorumRankedPlayLayer*>(PlayLayer::get());}
struct PlayerHook:PlayerObject {
 ${['void update(float dt)', 'void updateRotation(float dt)', 'void spiderTestJump(bool dynamic)'].map(n=>method(n,playerAt)).join('\n')}
};
struct WorldHook:CorumRankedPlayLayer {
 ${method('void update(float dt)',worldAt)}
 ${method('void teleportPlayer(',worldAt)}
};
int main(){
 auto& publisher=SpectatorLiveRuntime::get();PlayerHook player;WorldHook world;PlayLayer::current=&world;
 player.m_gameLayer=&world;world.m_player1=&player;
 world.update(.016f);assert(publisher.sent.empty()); // vanilla init has no capture authority
 world.fields.rankedInitialized=true;
 for(int i=0;i<40;++i){world.update(.016f);}assert(publisher.sent.empty()); // default entrance pose is not stable gameplay
 clockMs=0;world.m_started=true;world.update(.001f);
 assert(publisher.sent.size()==1 && publisher.sent.front().x==100 && publisher.sent.front().rotation==0); // valid stationary initial world seed
 player.moving=true;world.update(.016f);world.update(.016f);world.update(.016f);world.update(.016f);
 assert(publisher.sent.size()==2 && clockMs<500);
 auto first=publisher.sent.back();assert(first.rotation==12 && first.camera->x==first.x-200);
 auto latest=world.fields.spectatorCapture.snapshot();assert(latest->rotation==player.rotation && latest->camera->x==player.position.x-200);
 publisher.busy=true;TeleportPortalObject portal;world.teleportPlayer(&portal,&player);
 for(int i=0;i<4;++i){world.update(.016f);}assert(publisher.sent.size()==2);
 publisher.busy=false;world.update(.016f);assert(publisher.sent.size()==3);
 auto snap=publisher.sent.back();assert(snap.discontinuity && verifiedSpectatorSnap(first,snap));
 assert(snap.cameraSnap && snap.camera->x==player.position.x-200);
 assert(!world.fields.spectatorCapture.snapshot()->discontinuity); // consumed only when sampled
 auto count=publisher.sent.size();player.m_isSpider=true;
 world.update(.016f);player.spiderTestJump(false);world.update(.016f);
 assert(publisher.sent.size()==count);world.update(.016f);world.update(.016f);
 assert(publisher.sent.back().discontinuity && publisher.sent.back().discontinuity->type==SpectatorDiscontinuity::SpiderSnap);
 // An arbitrary coordinate change without a native event never creates proof.
 world.fields.spectatorCapture.sampled();player.position.x+=729.7268f;world.observeSpectatorNativeUpdate(&player);
 assert(!world.fields.spectatorCapture.snapshot()->discontinuity);
 world.fields.spectatorCapture.reset();player.m_wasTeleported=false;player.nativeTeleport=true;world.update(.016f);
 assert(world.fields.spectatorCapture.snapshot()->discontinuity); // genuine native flag rising edge
 auto dummyUpdates=player.updates;world.fields.spectatorMode=true;
 for(int i=0;i<3;++i){world.update(.016f);}assert(player.updates==dummyUpdates);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
 auto& debug=debug::DebugSpectateRuntime::get();world.fields.spectatorMode=false;world.fields.rankedLevel=false;
 world.fields.debugRecordScene=true;world.fields.spectatorCapture.reset();clockMs=0;
 world.update(.001f);world.update(.016f);world.update(.016f);assert(debug.sent.size()==1 && world.fields.debugRecordHasFrame);
 assert(debug.sent.back().camera && clockMs<500);
 auto size=debug.sent.size();world.update(.016f);assert(debug.sent.size()==size);
 world.fields.debugRecordScene=false;world.fields.debugPlaybackScene=true;dummyUpdates=player.updates;
 world.update(.016f);assert(player.updates==dummyUpdates && debug.sent.size()==size);
#endif
}
`;
    const dir=mkdtempSync(join(tmpdir(),'corum-capture-native-'));
    try{const file=join(dir,'capture.cpp'),binary=join(dir,'capture');writeFileSync(file,cpp);
        execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',...(debugOn?['-DCORUM_RANKED_DEBUG_SPECTATE=1']:[]),file,'-o',binary],{stdio:'pipe'});
        execFileSync(binary,[],{stdio:'pipe'});
    }finally{rmSync(dir,{recursive:true,force:true});}
});

test('actual Debug recording clock seeds at the first valid native world and preserves relative terminal time',()=>{
    const runtime=readFileSync(join(root,'src/debug/DebugSpectateRuntime.cpp'),'utf8');
    const start=runtime.indexOf('bool DebugSpectateRuntime::recordPose('),open=runtime.indexOf('{',start);
    let end=open+1,depth=1;while(depth){if(runtime[end]==='{')++depth;if(runtime[end]==='}')--depth;++end;}
    const body=runtime.slice(start,end).replaceAll('std::chrono::steady_clock::now()', 'testClockNow()');
    const cpp=`
#include "${join(root,'src/debug/DebugSpectatePolicy.hpp')}"
#include "${join(root,'src/domain/SpectatorCapturePolicy.hpp')}"
#include <chrono>
#include "${join(root,'src/domain/SpectatorCameraPolicy.hpp')}"
#include <cassert>
#include <limits>
namespace geode::log {template<class... T>void info(T...){} }
using namespace geode;
namespace corum::ranked::debug {
static std::chrono::steady_clock::time_point wall{};
auto testClockNow(){return wall;}
struct DebugSpectateRuntime {
 DebugSpectateMode m_mode=DebugSpectateMode::Recording;
 DebugSpectateCapture m_capture;
 std::chrono::steady_clock::time_point m_recordStartedAt{};
 std::optional<SpectatorGhostPose> m_previousCapturedPose;
 int uploads=0;void pumpRecording(){++uploads;}
 bool recordPose(SpectatorGhostPose const&,bool terminal=false);
};
${body}
}
int main(){
 using namespace corum::ranked;using namespace corum::ranked::debug;
 SpectatorCapturePolicy native;
 SpectatorGhostPose initial{100,40,0,false,false};initial.camera=SpectatorCameraOrigin{-65,-60};
 native.observe(initial);assert(!native.stableNativeFrame(0,false)); // finite init alone is insufficient
 assert(native.stableNativeFrame(0,true)); // started, completed world can be stationary
 auto garbage=initial;garbage.x=std::numeric_limits<float>::quiet_NaN();native.observe(garbage);
 assert(!native.stableNativeFrame(.01,true) && !native.snapshot());
 native.observe(initial);assert(native.stableNativeFrame(.02,true));
 DebugSpectateRuntime record;wall+=std::chrono::milliseconds(1701);
 assert(record.recordPose(initial));assert(record.m_capture.batch().front().elapsedMs==0);
 wall+=std::chrono::milliseconds(50);initial.x=120;assert(record.recordPose(initial));
 wall+=std::chrono::milliseconds(50);initial.x=140;assert(record.recordPose(initial));
 auto frames=record.m_capture.batch();assert(frames[1].elapsedMs==50 && frames[2].elapsedMs==100);
 wall+=std::chrono::milliseconds(17);initial.dead=true;assert(record.recordPose(initial,true));
 assert(record.m_capture.batch().back().elapsedMs==117 && record.m_capture.completed());
}
`;
    const dir=mkdtempSync(join(tmpdir(),'corum-world-seed-'));
    try{const file=join(dir,'seed.cpp'),binary=join(dir,'seed');writeFileSync(file,cpp);
        execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',file,'-o',binary],{stdio:'pipe'});
        execFileSync(binary,[],{stdio:'pipe'});
    }finally{rmSync(dir,{recursive:true,force:true});}
});
