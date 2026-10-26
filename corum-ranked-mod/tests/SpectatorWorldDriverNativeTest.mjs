import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = p => readFileSync(join(root,p),'utf8');
const source=read('src/PlayLayer.cpp');

function method(signature, from=0) {
    const start=source.indexOf(signature,from);assert(start>=0,signature);
    const open=source.indexOf('{',start);let depth=1,end=open+1;
    while(depth){if(source[end]==='{')++depth;if(source[end]==='}')--depth;++end;}
    return source.slice(start,end);
}

test('actual world owner synchronizes triggers, Spider and native camera before final presentation without local physics',()=>{
    const dir=mkdtempSync(join(tmpdir(),'corum-camera-owner-'));
    try {
        mkdirSync(join(dir,'Geode/binding'),{recursive:true});
        writeFileSync(join(dir,'Geode/Geode.hpp'),read('tests/helpers/SpectatorNativeDouble.hpp'));
        for(const name of ['PlayLayer','GJBaseGameLayer','PlayerObject','GJGameLevel','GhostTrailEffect','HardStreak'])
            writeFileSync(join(dir,`Geode/binding/${name}.hpp`),'#include <Geode/Geode.hpp>\n');
        const methods=['bool spectatorCameraActive()','bool beginSpectatorPresentationFrame()', 'bool beginSpectatorWorldFrame()', 'void reconcileSpectatorWorldDriver()',
            'std::optional<CCPoint> spectatorNativeCameraTarget()','void logSpectatorNativeCamera(char const* stage)',
            'void applySpectatorProgressPresentation()','void updateProgressbar()',
            'void driveSpectatorCamera()','void ensureSpectatorCameraFrame(float dt)',
            'void cleanupSpectatorPresentation()', 'bool blocksSpectatorNativePlayerWrite(PlayerObject* player)'].map(s=>method(s)).join('\n');
        const hook=method('void updateCamera(float dt)',source.indexOf('class $modify(CorumRankedSpectatorCameraHook')).replace('void updateCamera(', 'void cameraHook(');
        const worldHook=method('void update(float dt)',source.indexOf('class $modify(CorumRankedSpectatorCameraHook')).replace('void update(', 'void worldHook(');
        const cpp=`
#include "${join(root,'src/spectator/CorumRemotePlayer.hpp')}"
#include "${join(root,'src/spectator/CorumSpectatorCamera.hpp')}"
#include "${join(root,'src/spectator/ScopedCameraTargetOverride.hpp')}"
#include "${join(root,'src/domain/SpectatorProgressPolicy.hpp')}"
#include "${join(root,'src/spectator/SpectatorWorldDriver.hpp')}"
#include "${join(root,'src/spectator/SpectatorTriggerScope.hpp')}"
#include "${join(root,'src/domain/SpectatorInterpolationBuffer.hpp')}"
#include "${join(root,'src/spectator/SpectatorPlayerCapture.hpp')}"
#include "${join(root, 'src/domain/SpectatorNativeProbe.hpp')}"
#include "${join(root,'src/domain/SpectatorRestoreTimeline.hpp')}"
#include <chrono>
#include <memory>
#include <iostream>
#include <source_location>
using namespace geode::prelude;
namespace fmt { std::string format(char const* pattern,float value){
    char buf[40];snprintf(buf,sizeof(buf),std::string(pattern).find(".2")!=std::string::npos?"%.2f%%":"%.0f%%",value);return buf;
} }
namespace corum::ranked {
struct RankedRuntime {
    struct Match{std::optional<int> opponentProgress;bool spectatorLiveEnabled=true;};struct View{Match match;} data;
    bool spectator=true;static RankedRuntime& get(){static RankedRuntime r;return r;}
    bool isSpectating()const{return spectator;}View const& view()const{return data;}
};
struct SpectatorLiveRuntime {
    std::optional<SpectatorRemoteFrame> frame;mutable int reads=0;
    static SpectatorLiveRuntime& get(){static SpectatorLiveRuntime r;return r;}
    auto presentationFrame()const{++reads;return frame;}
    SpectatorRestoreTimeline restore;
    auto takeRestorationFrames(){return restore.takeBatch();}
    void restorationApplied(SpectatorRemoteFrame const& f,bool advanced,double){assert(advanced);restore.applied(f);frame=f;}
    void requestSceneRestore(){}void noteWorldPartial(std::string){}

};
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
namespace debug { struct DebugSpectateRuntime {
    static DebugSpectateRuntime& get(){static DebugSpectateRuntime r;return r;}
    bool playbackLevel(int level)const{return level==88350599;}
}; }
#endif
}
struct ScopedPersistenceOwner{explicit ScopedPersistenceOwner(PlayLayer*){}};
struct ScopedRealGameplayEnd{bool& flag;bool previous;ScopedRealGameplayEnd(bool& f,bool b):flag(f),previous(f){flag=b;}~ScopedRealGameplayEnd(){flag=previous;}};
template<class T>struct FieldsAccess { std::unique_ptr<T> value=std::make_unique<T>();T* operator->(){return value.get();} };
struct CorumRankedPlayLayer: PlayLayer {
 bool blockEntryGameplayUpdate(){return false;}
    struct Fields {
        bool spectatorScenePrepared=true,spectatorPresentationRetired=false,autoExitRequested=false;
        struct Safety{bool enabled=true;bool active()const{return enabled;}void resetDummy(){}}localSafety;
        corum::ranked::CorumRemotePlayer remotePlayer;
        corum::ranked::CorumSpectatorCamera spectatorCamera;
        std::optional<corum::ranked::SpectatorRemoteFrame> spectatorRenderedFrame;
        std::optional<unsigned> spectatorCameraFrameNumber,spectatorPresentationFrameNumber,spectatorWorldFrameNumber;
        bool spectatorWorldUpdateInProgress=false,spectatorWorldResetInProgress=false,spectatorCollisionChecked=false,spectatorWorldPoseApplied=false;
        corum::ranked::SpectatorWorldDriver spectatorWorldDriver;
        corum::ranked::SpectatorSceneContext sceneContext{"debug-spectate:server",1,0,88350599};
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        corum::ranked::SpectatorNativeProbe spectatorNativeProbe;
        bool debugPlaybackScene=true,cameraForensicFrame=false;
        int levelId=88350599,cameraForensicFrames=0;std::int64_t cameraForensicSequence=-1;
#endif
    };
    FieldsAccess<Fields> m_fields;bool watchable=true;
    bool watchableSpectatorScene(){return watchable;}
    bool prepareSpectatorScene(){return spectatorCameraActive();}
    void sampleSpectatorPresentation(){}void logSpectatorWorldDriver(){}void logSpectatorNativeProbe(){}
    bool freezeLocalDummy(PlayerObject* p){return p==m_player1 && m_fields->localSafety.active();}
    corum::ranked::SpectatorGhostPose observedSpectatorPose(){return corum::ranked::captureSpectatorPose(m_player1);}
 bool observesSpectatorPlayer(PlayerObject*){return false;}
    void observeSpectatorNativeUpdate(PlayerObject*){}
    void observeSpectatorNativeSnap(PlayerObject*,corum::ranked::SpectatorDiscontinuity,corum::ranked::SpectatorGhostPose const&){}
    ${methods}
    ${hook}
    ${worldHook}
    ${method('int checkCollisions(PlayerObject* player,float dt,bool ignoreDamage)')}
    ${method('void collisionCheckObjects(PlayerObject* player,gd::vector<GameObject*>* objects,int count,float dt)')}
};
CorumRankedPlayLayer* localSafetyLayer(){return static_cast<CorumRankedPlayLayer*>(PlayLayer::get());}
enum class Priority {First};
${method('class $modify(CorumRankedSpectatorDummyPlayer, PlayerObject)').replace('class $modify(CorumRankedSpectatorDummyPlayer, PlayerObject)', 'struct PlayerHook:PlayerObject')};

void close(float a,float b,std::source_location where=std::source_location::current()){if(std::abs(a-b)>=0.001f)std::cerr<<"close "<<a<<" != "<<b<<" at line "<<where.line()<<std::endl;assert(std::abs(a-b)<0.001f);}
int main(){
 using namespace corum::ranked;
 {
  CorumRankedPlayLayer scene;PlayerHook dummy;PlayerObject second;PlayLayer::current=&scene;
  scene.m_isPaused=true;scene.m_player1=&dummy;scene.m_player2=&second;dummy.m_gameLayer=&scene;dummy.m_gameLayer=&scene;
  GJGameLevel level;level.m_levelID=88350599;scene.m_level=&level;
  auto* world=CCLayer::create();geode::Ref<CCLayer> root(world);scene.m_objectLayer=world;
  CCSprite fill;CCLabelBMFont label;scene.m_progressFill=&fill;scene.m_percentageLabel=&label;
  scene.m_gameState.m_cameraPosition=CCPoint{150,20};scene.m_fields->spectatorCamera.attach(&scene);
  auto& live=SpectatorLiveRuntime::get();
  SpectatorRemoteContext context{"debug-spectate:server",1,0,88350599,"recording",1,1};
  live.frame=SpectatorRemoteFrame{context,0,SpectatorGhostPose{100,40,0,false,false},0};
  live.frame->pose->camera=SpectatorCameraOrigin{-65,-60};
  std::vector<int> triggers;float followX=0;int background=0;
  int sourceDestinationSweeps=0;bool spiderReadyBeforeWorld=false;
  GJBaseGameLayer::worldDispatch=[](GJBaseGameLayer* l,float dt){static_cast<CorumRankedPlayLayer*>(l)->worldHook(dt);};
  GJBaseGameLayer::cameraDispatch=[](GJBaseGameLayer* l,float dt){static_cast<CorumRankedPlayLayer*>(l)->cameraHook(dt);};
  // Native world double reads the REAL m_player1 before its trigger/follow work.
  // It is an ordering fixture, not a reimplementation/proof of GD's trigger engine.
  static std::vector<int>* events;events=&triggers;
  static float* follow;follow=&followX;static int* bg;bg=&background;
  static int* sweeps;sweeps=&sourceDestinationSweeps;static bool* spiderReady;spiderReady=&spiderReadyBeforeWorld;
  GJBaseGameLayer::nativeWorldObserver=[](GJBaseGameLayer* b,float dt){
   auto* o=static_cast<CorumRankedPlayLayer*>(b);auto* p=b->m_player1;
   assert(!o->m_isPaused);auto const& f=*o->m_fields->spectatorRenderedFrame;
   if(!f.pose)return;
   close(p->getPosition().x,f.pose->x);close(p->getPosition().y,f.pose->y);
   assert(!p->isVisible() && p->m_isUpsideDown==f.pose->upsideDown);
   assert(p->m_isSpider==(f.pose->mode==SpectatorGameMode::Spider));
   *spiderReady=p->m_isSpider && p->m_isUpsideDown;
   *follow=float(p->m_positionX);
   // Synthetic position-trigger crossing including a Follow Player target and background.
   for(int x:{110,125,140,180})if(p->m_lastPosition.x<x && p->getPosition().x>=x){events->push_back(x);if(x==110||x==180)++*bg;}
   if(std::abs(p->getPosition().y-p->m_lastPosition.y)>400)++*sweeps;
   auto collisionCalls=b->collisionQueries;b->checkCollisions(p,dt,false);b->checkCollisions(p,dt,false);assert(b->collisionQueries==collisionCalls+1);
   p->update(dt);p->updateRotation(dt);p->spiderTestJump(false); // all local physics blocked
#if !defined(GEODE_IS_WINDOWS)
   p->updateSpecial(dt); // native helper cannot double-write driver follow history
#endif
   // Windows updateSpecial is inline in its blocked physics caller; no detour.
   p->setVisible(true);assert(p->isVisible()); // unhooked native setter; owner hides before camera/presentation
   // A native portal may mutate flags. Position writes cannot win over the scoped frame.
   p->setPosition({9999,9999});p->setRotation(123);
   close(p->getPosition().x,f.pose->x);close(p->getRotation(),f.pose->rotation);
   p->m_isSpider=false;p->m_isUpsideDown=false;
   b->updateCamera(dt);b->updateCamera(dt);
   close(p->getPosition().x,f.pose->x);assert(p->m_isSpider==(f.pose->mode==SpectatorGameMode::Spider));
   p->m_isSpider=false;p->m_isUpsideDown=false; // a later native write is reconciled after world
  };
  GameObject trigger,solid;trigger.m_isTrigger=true;GJBaseGameLayer::nativeObjects={&trigger,&solid};
  auto tick=[&](int sequence,double elapsed,float x,float y,bool advance=true){
   ++CCDirector::sharedDirector()->totalFrames;
   live.frame->sequence=sequence;live.frame->publisherElapsedMs=elapsed;
   live.frame->pose->x=x;live.frame->pose->y=y;
   live.frame->pose->camera=SpectatorCameraOrigin{x-165,y-100};
   auto reads=live.reads,cameras=scene.cameraUpdates,updates=scene.worldUpdates,bgs=scene.bgUpdates;
   scene.GJBaseGameLayer::update(1.0f/60);scene.ensureSpectatorCameraFrame(1.0f/60);
   scene.GJBaseGameLayer::update(1.0f/60);scene.GJBaseGameLayer::updateCamera(1.0f/60);
   assert(live.reads==reads+1 && scene.worldUpdates==updates+(advance?1:0) && scene.cameraUpdates==cameras+(advance?1:0));
   assert(scene.bgUpdates==bgs+1); // one final viewport correction after world
   close(dummy.getPosition().x,x);close(dummy.getPosition().y,y);assert(!dummy.isVisible());
   assert(scene.m_isPaused);assert(dummy.physicsUpdates==0 && dummy.rotationUpdates==0 && dummy.spiderJumps==0);
   auto* remote=scene.m_fields->remotePlayer.player();assert(remote && remote!=&dummy && remote!=&second);
   close(remote->getPosition().x,x);close(remote->getPosition().y,y);
   close(scene.m_gameState.m_cameraPosition.x,x-165);close(scene.m_gameState.m_cameraPosition.y,y-100);
   close(CCPointApplyAffineTransform(remote->getPosition(),world->nodeToWorldTransform()).x,165);
  };
  tick(0,0,100,40,false);auto* remote=scene.m_fields->remotePlayer.player();
  tick(0,25,110,40);tick(1,50,120,40);tick(1,75,130,40);tick(2,100,140,40);tick(3,150,150,40);
  assert((triggers==std::vector<int>{110,125,140}) && background==1 && followX==150);
  assert(dummy.m_followRelated==15);assert(scene.collisionQueries==5&&scene.triggerObjects==5&&scene.damageIgnored);
  tick(3,150,150,40,false);assert(scene.collisionQueries==5); // native 10ms Follow Player Y, independent of render cadence
  live.frame->pose->mode=SpectatorGameMode::Spider;live.frame->pose->upsideDown=true;
  live.frame->pose->discontinuity=SpectatorSnapEvidence{SpectatorDiscontinuity::SpiderSnap,150,40,160,740};
  live.frame->pose->cameraSnap=true;live.frame->pose->rotation=270;
  tick(4,200,160,740);
  assert(spiderReadyBeforeWorld && sourceDestinationSweeps==0 && dummy.m_followRelated==0);
  assert(dummy.m_lastPosition.x==160 && dummy.m_lastPosition.y==740);
  tick(4,225,165,740);assert(dummy.m_followRelated==2); // held snap evidence resets only once
  assert(dummy.m_isSpider && dummy.m_isUpsideDown && scene.m_fields->remotePlayer.player()==remote);
  live.frame->pose->discontinuity.reset();live.frame->pose->cameraSnap=false;
  tick(5,250,180,740);assert(background==2 && triggers.back()==180 && sourceDestinationSweeps==0);
  live.frame->pose->finished=true;tick(6,300,200,740);scene.updateProgressbar();
  assert(label.text=="100%" && scene.newBest==7 && scene.savedPercentage==7 && scene.m_currentProgress==0);
  assert(scene.m_fields->remotePlayer.player()==remote);
  // Context reset is the only native world reset, never an ordinary relocation.
  live.frame->context.generation=2;live.frame->context.epoch=2;live.frame->context.attemptId="next";
  live.frame->pose->finished=false;live.frame->pose->mode=SpectatorGameMode::Cube;live.frame->pose->upsideDown=false;
  auto resets=scene.resets;tick(0,0,100,40,false);assert(scene.resets==resets+1);
  // Unauthenticated / unrelated / Practice scenes cannot use the local write adapter.
  SpectatorWorldDriver denied;auto writes=dummy.gameplayPositionWrites;
  assert(!denied.apply(&scene,false,live.frame));scene.m_isPracticeMode=true;assert(!denied.apply(&scene,true,live.frame));scene.m_isPracticeMode=false;
  auto bad=live.frame;bad->context.levelId=123;assert(!denied.apply(&scene,true,bad));
  assert(dummy.gameplayPositionWrites==writes);
  auto worlds=scene.worldUpdates,resetCount=scene.resets;
  ++CCDirector::sharedDirector()->totalFrames;live.frame->context.roundNumber=2;live.frame->publisherElapsedMs=50;
  scene.GJBaseGameLayer::update(.016f);assert(scene.worldUpdates==worlds&&scene.resets==resetCount&&!scene.m_fields->spectatorRenderedFrame);
  auto objects=scene.triggerObjects;scene.checkCollisions(&second,.016f,false);assert(scene.triggerObjects==objects+2&&!scene.damageIgnored);
  scene.cleanupSpectatorPresentation();
  GJBaseGameLayer::nativeWorldObserver=nullptr;GJBaseGameLayer::worldDispatch=nullptr;GJBaseGameLayer::cameraDispatch=nullptr;
  PlayLayer::current=nullptr;CCObject::drain();
 }
 // The unchanged publisher buffer drives continuous world coordinates at every render FPS.
 for(int fps:{30,60,120,144,240}){
  PlayLayer scene;PlayerObject p;GJGameLevel level;scene.m_player1=&p;scene.m_level=&level;p.m_gameLayer=&scene;
  SpectatorInterpolationBuffer buffer;SpectatorWorldDriver driver;
  SpectatorRemoteContext c{"match",1,0,level.m_levelID,"attempt",1,1};
  for(int seq=0;seq<=20;++seq){SpectatorGhostPose pose{100+seq*20.0f,40+seq*2.0f,0,false,false};assert(buffer.push({seq,seq*50.0,pose},false,0));}
  double previous=100,nativeTime=0;
  for(int step=0;step<=fps;++step){auto sample=buffer.render(step*1000.0/fps);assert(sample);
   SpectatorRemoteFrame f{c,sample->sequence,sample->pose,sample->publisherElapsedMs};
   assert(driver.apply(&scene,true,f));if(auto delta=driver.takeNativeDeltaSeconds())nativeTime+=*delta;close(p.getPosition().x,f.pose->x);
   assert(p.getPosition().x>=previous && p.getPosition().x-previous<=400.0/fps+0.001);
   previous=p.getPosition().x;
  }
  close(p.getPosition().x,500);close(float(nativeTime),1.f);assert(p.m_followRelated==100 && p.physicsUpdates==0 && !p.isVisible());
 }
 // Repeated buffered snaps: renderer identity, driver phase and camera owner
 // survive 26 boundaries under the old 80ms + 110ms serialized HTTP cadence.
 for(int fps:{60,120,144,240}){
  PlayLayer scene;PlayerObject dummy;GJGameLevel level;scene.m_player1=&dummy;scene.m_level=&level;dummy.m_gameLayer=&scene;
  auto* world=CCLayer::create();geode::Ref<CCLayer> root(world);scene.m_objectLayer=world;
  SpectatorInterpolationBuffer buffer;SpectatorWorldDriver driver;CorumRemotePlayer remote;CorumSpectatorCamera camera;
  camera.attach(&scene);SpectatorRemoteContext c{"match",1,0,level.m_levelID,"attempt",1,1};
  PlayerObject* identity=nullptr;int next=0,nextSnap=3,snaps=0;double arrival=190;unsigned segment=0;
  for(double wall=0;wall<5000;wall+=1000.0/fps){
   while(arrival<=wall){
    int last=int(arrival/50);buffer.noteResponse(arrival,110,80,last-next+1);
    for(;next<=last;++next){bool snap=next==nextSnap;if(snap){++snaps;nextSnap+=snaps%2?4:3;}
     SpectatorGhostPose pose{100+next*10.0f,snaps%2?740.0f:40.0f,0,false,false};pose.mode=SpectatorGameMode::Spider;
     pose.upsideDown=snaps%2;pose.camera=SpectatorCameraOrigin{pose.x-165,pose.y-100};
     assert(buffer.push({next,next*50.0,pose},snap,arrival));
    }arrival+=190;
   }
   auto sample=buffer.render(wall);if(!sample)continue;
   SpectatorRemoteFrame f{c,sample->sequence,sample->pose,sample->publisherElapsedMs,sample->segment};
   assert(driver.apply(&scene,true,f));remote.update(&scene,true,f);camera.update(&scene,true,c,remote.cameraFrame());
   if(!identity){identity=remote.player();}assert(remote.player()==identity && identity!=&dummy);
   close(dummy.getPosition().x,identity->getPosition().x);close(dummy.getPosition().y,identity->getPosition().y);
   close(scene.m_gameState.m_cameraPosition.x,f.pose->camera->x);close(scene.m_gameState.m_cameraPosition.y,f.pose->camera->y);
   assert(dummy.m_isUpsideDown==f.pose->upsideDown && f.context==c && camera.ownsCamera());
   if(sample->segment!=segment){close(dummy.m_lastPosition.x,f.pose->x);close(dummy.m_lastPosition.y,f.pose->y);segment=unsigned(sample->segment);}
   assert(dummy.physicsUpdates==0 && dummy.spiderJumps==0);
  }
  assert(snaps>=26 && buffer.starvations()==0);camera.cleanup(&scene);remote.cleanup();driver.reset();
 }
 // A skipped render frame landing AFTER C still resets native crossing history.
 {
  PlayLayer s;PlayerObject p;GJGameLevel l;s.m_player1=&p;s.m_level=&l;p.m_gameLayer=&s;SpectatorWorldDriver d;
  SpectatorRemoteContext c{"m",1,0,l.m_levelID,"a",1,1};
  assert(d.apply(&s,true,SpectatorRemoteFrame{c,0,SpectatorGhostPose{100,40,0},0,0}));
  assert(d.apply(&s,true,SpectatorRemoteFrame{c,4,SpectatorGhostPose{800,740,0},200,1}));
  close(p.m_lastPosition.x,800);close(p.m_lastPosition.y,740);assert(p.m_followRelated==0);
 }
 {
  PlayLayer scene;PlayerObject p;GJGameLevel level;scene.m_player1=&p;scene.m_level=&level;p.m_gameLayer=&scene;
  SpectatorWorldDriver driver;SpectatorRemoteContext c{"m",1,0,level.m_levelID,"a",1,1};
  int i=0;for(auto mode:{SpectatorGameMode::Cube,SpectatorGameMode::Ship,SpectatorGameMode::Ball,SpectatorGameMode::UFO,SpectatorGameMode::Wave,SpectatorGameMode::Robot,SpectatorGameMode::Spider,SpectatorGameMode::Swing}){
   SpectatorGhostPose pose{100,40,float(i*45),false,false};pose.mode=mode;pose.mini=i%2;pose.upsideDown=i%2;pose.flipped=i%2;
   SpectatorRemoteFrame f{c,i,pose,i*50.0};assert(driver.apply(&scene,true,f));
   assert(p.m_isShip==(mode==SpectatorGameMode::Ship)&&p.m_isSpider==(mode==SpectatorGameMode::Spider)&&p.m_isSwing==(mode==SpectatorGameMode::Swing));
   assert(p.m_isUpsideDown==pose.upsideDown&&p.m_vehicleSize==(pose.mini?.6f:1.f));++i;
  }
  auto f=*driver.frame();f.pose->dual=true;assert(!driver.apply(&scene,true,f)&&!driver.takeNativeDeltaSeconds());
  f.pose->dual=false;--f.sequence;assert(!driver.apply(&scene,true,f));
  f=*driver.frame();f.publisherElapsedMs+=1000;f.sequence+=20;f.pose->x=5000;assert(driver.apply(&scene,true,f));
  assert(!driver.takeNativeDeltaSeconds()&&p.m_lastPosition.x==5000&&driver.partialHistory()); // no invented crossing/catch-up history
  driver.reset();assert(!driver.partialHistory()&&!driver.frame()&&!driver.takeNativeDeltaSeconds()&&!driver.owns(&p));
 }

 // Late joins execute the production WorldDriver/native owner, not just a queue policy.
 for(int duration:{0,10000,60000,120000}){
  CorumRankedPlayLayer scene;PlayerHook dummy;GJGameLevel level;level.m_levelID=88350599;
  scene.m_level=&level;scene.m_player1=&dummy;dummy.m_gameLayer=&scene;PlayLayer::current=&scene;scene.m_isPaused=true;
  auto* world=CCLayer::create();geode::Ref<CCLayer> root(world);scene.m_objectLayer=world;
  auto& live=SpectatorLiveRuntime::get();live.restore.reset();
  SpectatorRemoteContext c{"debug-spectate:server",1,0,level.m_levelID,"late-"+std::to_string(duration),7,7};
  live.restore.begin(c,true);
  for(int sequence=0;sequence<=duration/50;++sequence){
   SpectatorGhostPose pose{100+sequence*5.f,40,0};pose.camera=SpectatorCameraOrigin{pose.x-165,pose.y-100};
   assert(live.restore.push(sequence,sequence*50.0,pose,false));
  }
  live.restore.networkCaughtUp(true);live.frame=live.restore.frame();
  GJBaseGameLayer::nativeObjects.clear();
  GJBaseGameLayer::worldDispatch=[](GJBaseGameLayer* p,float dt){static_cast<CorumRankedPlayLayer*>(p)->worldHook(dt);};
  GJBaseGameLayer::cameraDispatch=[](GJBaseGameLayer* p,float dt){static_cast<CorumRankedPlayLayer*>(p)->cameraHook(dt);};
  PlayerObject* identity=nullptr;int ticks=0;
  while(live.restore.active()){
   auto const updates=scene.worldUpdates,camera=scene.cameraUpdates;
   ++CCDirector::sharedDirector()->totalFrames;scene.GJBaseGameLayer::update(.016f);++ticks;
   assert(scene.worldUpdates-updates<=8 && scene.cameraUpdates-camera<=1 && scene.m_isPaused);
   auto* current=scene.m_fields->remotePlayer.player();if(!identity)identity=current;assert(current==identity);
   assert(dummy.physicsUpdates==0 && dummy.spiderJumps==0 && scene.savedPercentage==7 && scene.newBest==7);
   assert(ticks<600);
  }
  assert(live.restore.appliedCount()==unsigned(duration/50+1));assert(scene.worldUpdates==duration/50);
  assert(std::abs(scene.m_gameState.m_levelTime-duration/1000.0)<.01);
  close(dummy.getPosition().x,100+duration*.1f);close(scene.m_gameState.m_cameraPosition.x,dummy.getPosition().x-165);
  assert(!live.restore.applied(*live.restore.frame())); // duplicate completion cannot run native work again
  scene.cleanupSpectatorPresentation();live.restore.reset();live.frame.reset();
  GJBaseGameLayer::worldDispatch=nullptr;GJBaseGameLayer::cameraDispatch=nullptr;PlayLayer::current=nullptr;
  CCObject::drain();
 }
 CCObject::drain();assert(CCObject::alive==0);
 std::cout<<"world ordering/Spider/follow history/scoped writes/60-120-240Hz passed\\n";
}
`;
        writeFileSync(join(dir,'test.cpp'),cpp);
        for(const config of [{debug:false,windows:false},{debug:true,windows:false},{debug:true,windows:true}]){
            const binary=join(dir,'test');
            execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',...(config.debug?['-DCORUM_RANKED_DEBUG_SPECTATE=1']:[]),...(config.windows?['-DGEODE_IS_WINDOWS=1']:[]),'-I',dir,
                join(root,'src/spectator/CorumRemotePlayer.cpp'),join(root,'src/spectator/CorumSpectatorCamera.cpp'),join(root,'src/spectator/SpectatorWorldDriver.cpp'),join(root,'src/spectator/SpectatorPlayerCapture.cpp'),join(dir,'test.cpp'),'-o',binary],{stdio:'pipe'});
            assert.match(execFileSync(binary,{encoding:'utf8'}),/60-120-240Hz passed/);
        }
    } finally {rmSync(dir,{recursive:true,force:true});}
});
