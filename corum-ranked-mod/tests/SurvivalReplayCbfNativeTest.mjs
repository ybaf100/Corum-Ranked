import test from 'node:test';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';import {tmpdir} from 'node:os';
const root=resolve(new URL('..',import.meta.url).pathname);
for(const platform of ['MACOS','WINDOWS'])test(platform+': production CBF root/cursor executes captured native substeps and P1/P2 inputs, preserves local collision/rotation state and restores only leased hooks',()=>{
 const json=process.env.CORUM_MATJSON_SOURCE,result=process.env.CORUM_RESULT_INCLUDE;if(!json||!result)throw Error('Pinned JSON dependencies required');
 const dir=mkdtempSync(join(tmpdir(),'corum-cbf-boundary-'));
 try{
 let native=readFileSync(join(root,'src/replay/SurvivalReplayCbfNative.cpp'),'utf8');native=native.slice(0,native.indexOf('// The early boundary surrounds')).replace(/^#include.*$|^using namespace geode::prelude;$/gm,'');
 writeFileSync(join(dir,'test.cpp'),`
#define GEODE_IS_${platform} 1
#include "replay/SurvivalReplayPlayback.hpp"
#include "replay/SurvivalReplayCbfNative.hpp"
#include <filesystem>
#include <fstream>
#include <set>
#include <functional>
#include <map>
#include <cassert>
#include <iostream>
using namespace corum::ranked::replay;
struct CCPoint{float x=0,y=0;CCPoint()=default;CCPoint(float a,float b):x(a),y(b){}};
struct Dict{int size=0;void removeAllObjects(){size=0;}int count()const{return size;}};
struct PlayerObject{
 CCPoint pos{0,105},m_shipRotation{},m_lastPosition{};float rotation=0;double m_yVelocity=0;
 bool m_jumpBuffered=false,m_isOnGround=true,m_isUpsideDown=false,ship=false;
 std::map<int,bool>m_holdingButtons;int m_lastCollisionBottom=-1,m_lastCollisionTop=-1,m_lastCollisionLeft=-1,m_lastCollisionRight=-1;
 Dict logs[4];Dict*m_collisionLogTop=&logs[0],*m_collisionLogBottom=&logs[1],*m_collisionLogLeft=&logs[2],*m_collisionLogRight=&logs[3];
 float getPositionX()const{return pos.x;}float getPositionY()const{return pos.y;}float getRotation()const{return rotation;}CCPoint getPosition()const{return pos;}
 void update(float);void updateRotation(float);void updateShipRotation(float);
};
struct PlayLayer{PlayerObject*m_player1=nullptr,*m_player2=nullptr;int m_currentStep=364;double m_timestamp=.5;struct{double m_levelTime=.75;}m_gameState;static inline PlayLayer*current=nullptr;static PlayLayer*get(){return current;}int checkCollisions(PlayerObject*,float,bool);void handleButton(bool,int,bool);};
struct Hook{std::string name;bool enabled=true,failDisable=false,failEnable=false;int disables=0,enables=0,priority=0;int getPriority()const{return priority;}
 std::string_view getDisplayName()const{return name;}bool isEnabled()const{return enabled;}std::uintptr_t getAddress()const;void setPriority(int p){priority=p;}
 geode::Result<> disable(){++disables;if(failDisable)return geode::Err("fixture disable failure");enabled=false;return geode::Ok();}
 geode::Result<> enable(){++enables;if(failEnable)return geode::Err("fixture enable failure");enabled=true;return geode::Ok();}
};
namespace tulip::hook{enum class TulipConvention{Default};}
float baseSlerp(float a,float b,float c){return a+(b-a)*c;}
std::uintptr_t Hook::getAddress()const{return reinterpret_cast<std::uintptr_t>(&baseSlerp);}
struct Mod{template<class F>geode::Result<Hook*>hook(void*,F,std::string,tulip::hook::TulipConvention){static Hook observer{"CorumReplay::SlerpObserver"};return geode::Ok(&observer);}template<class T>T getSavedValue(char const*,T fallback){return fallback;}static Mod*get(){static Mod m;return &m;}static Mod*cbf(){static Mod m;return &m;}std::vector<Hook*>hooks;bool isLoaded()const{return true;}std::string getID()const{return this==cbf()?"syzzi.click_between_frames":"hwanhee1.corum_ranked";}std::vector<Hook*>getHooks(){return hooks;}std::filesystem::path getBinaryPath(){return "${dir}/cbf.bin";}};
struct Loader{static Loader*get(){static Loader l;return &l;}std::vector<Mod*>getAllMods(){return {Mod::cbf(),Mod::get()};}};
namespace Priority{constexpr int First=-1000000,Last=100;}
namespace fixtureLog{template<class...T>void error(T const&...){}}
namespace corum::ranked::replay{namespace log=fixtureLog;struct Recorder{static Recorder&get(){static Recorder r;return r;}std::uint64_t tick()const{return 0;}};}
${native}
using namespace corum::ranked::replay;
bool sourceCbf=true,buffering=false,dual=true;int nativeCalls=0,inputCalls=0;std::array<CCPoint,2>sourcePrevious;bool sourceSplit=false;
void nativeMove(PlayerObject*p,float dt){Operation op("UPDATE",p,dt);++nativeCalls;if(p->m_holdingButtons[1]&&p->m_isOnGround){p->m_yVelocity=15;p->m_isOnGround=false;p->m_jumpBuffered=true;}p->m_yVelocity-=dt*.5;p->pos.y+=p->m_yVelocity*dt;p->pos.x+=dt*2;p->m_lastPosition=p->pos;}
void nativeRotate(PlayerObject*p,float dt){Operation op("ROTATE",p,dt);
#if defined(GEODE_IS_WINDOWS)
 if(p->ship){p->updateShipRotation(dt);return;}
 p->rotation=observeSlerp(p->rotation,p->rotation+3.f,dt);p->m_shipRotation={p->rotation,dt};return;
#endif
 p->rotation+=(p->ship?float(p->m_yVelocity)*.25f:3.f)*dt;p->m_shipRotation={p->rotation,dt};}
void PlayerObject::updateShipRotation(float requested){
#if defined(GEODE_IS_WINDOWS)
 ShipRequest request(requested);auto& s=state();auto* previous=s.shipPlayer;s.shipPlayer=this;
 auto dt=sourceCbf?1.f/1024.f:requested;dt=nativeShipDelta(dt);
 Json fields;fields["requestedDt"]=static_cast<double>(s.shipRequested);Operation op("SHIP",this,dt,std::move(fields));
 float coefficient=dt*.25f;if(sourceCbf)coefficient=requested*coefficient*1024.f;
 rotation=observeSlerp(rotation,float(m_yVelocity),coefficient);m_shipRotation={rotation,dt};op.finish();s.shipFactor=0;s.shipPlayer=previous;
#else
 (void)requested;
#endif
}
void PlayerObject::update(float dt){Root guard(this,"UPDATE",dt);if(guard.handled)return;
 auto* scene=PlayLayer::get();
 if(!sourceCbf||this!=scene->m_player1){if(sourceCbf&&sourceSplit&&this==scene->m_player2)return;nativeMove(this,dt);return;}
 sourcePrevious={scene->m_player1->pos,scene->m_player2->pos};sourceSplit=true;
 auto initialGround=scene->m_player1->m_isOnGround;
 for(int segment=0;segment<4;++segment){
  if(segment==1)scene->handleButton(true,1,true);
  if(segment==2)scene->handleButton(true,1,false);
  if(segment==3){scene->handleButton(false,1,true);scene->handleButton(false,1,false);}
  if(!buffering||segment==3){nativeMove(this,buffering?dt:dt*.25f);if(segment!=3){if(segment==0&&((m_yVelocity<0)^m_isUpsideDown))m_isOnGround=initialGround;scene->checkCollisions(this,0,true);nativeRotate(this,dt*.25f);clearCollisionLog(this);}}
  if(dual){nativeMove(scene->m_player2,dt*.25f);if(segment!=3){if(segment==0&&((scene->m_player2->m_yVelocity<0)^scene->m_player2->m_isUpsideDown))scene->m_player2->m_isOnGround=true;scene->checkCollisions(scene->m_player2,0,true);nativeRotate(scene->m_player2,dt*.25f);clearCollisionLog(scene->m_player2);}}
  else if(segment==3)nativeMove(scene->m_player2,dt);
 }
}
void PlayerObject::updateRotation(float dt){Root guard(this,"ROTATE",dt);if(guard.handled)return;auto split=sourceCbf&&sourceSplit&&(this==PlayLayer::get()->m_player1?!buffering:dual);nativeRotate(this,split?dt*.25f:dt);if(split)m_lastPosition=sourcePrevious[this==PlayLayer::get()->m_player2];}
int PlayLayer::checkCollisions(PlayerObject*p,float dt,bool ignore){Json f;f["ignoreDamage"]=ignore;Operation op("COLLISION",p,dt,std::move(f));p->logs[0].size=1;p->m_lastCollisionTop=17;if(p->pos.y<105){p->pos.y=105;p->m_yVelocity=0;p->m_isOnGround=true;}return 0;}
void PlayLayer::handleButton(bool down,int button,bool first){auto*p=first?m_player1:m_player2;Json f;f["button"]=button;f["down"]=down;f["timestamp"]=m_timestamp;Operation op("INPUT",p,0,std::move(f));++inputCalls;p->m_holdingButtons[button]=down;}
void sequence(PlayLayer& scene,float dt=1){scene.m_player1->update(dt);if(dual)scene.m_player2->update(dt);scene.m_player1->updateRotation(dt);if(dual)scene.m_player2->updateRotation(dt);}
Json actualPose(PlayLayer& scene){Json p;p["kind"]="pose";p["x"]=scene.m_player1->pos.x;p["y"]=scene.m_player1->pos.y;p["rotation"]=scene.m_player1->rotation;p["physics"]["p1"]=motion(scene.m_player1);p["physics"]["p2"]=motion(scene.m_player2);p["physics"]["nativeStep"]=scene.m_currentStep;p["physics"]["pendingButtons"]=Json::array();return p;}
void worldStep(PlayLayer& scene,float dt){sequence(scene,dt);++scene.m_currentStep;scene.m_timestamp+=dt;scene.m_gameState.m_levelTime+=dt;}
int main(){
 std::ofstream("${dir}/cbf.bin")<<"CBF fixture binary, not the real GD mod";
#if defined(GEODE_IS_WINDOWS)
 state().slerpAddress=reinterpret_cast<std::uintptr_t>(&baseSlerp);
#endif
 for(bool ship:{false,true})for(bool buffer:{false,true})for(bool dualMode:{false,true}){
  buffering=buffer;dual=dualMode;PlayerObject a,b;a.ship=b.ship=ship;if(buffer)a.m_isOnGround=false;
  PlayLayer source;source.m_player1=&a;source.m_player2=&b;PlayLayer::current=&source;sourceCbf=true;sourceSplit=false;
  beginCbfNativeFrame(&source);sequence(source);auto tape=finishCbfNativeFrame();
  assert(cbfNativeFrameError().empty()&&cbfFrameValid(tape));auto expectedA=motion(&a),expectedB=motion(&b);auto lastA=point(a.m_lastPosition),lastB=point(b.m_lastPosition);
  Document doc;doc.complete=true;doc.metadata["nativeInputVersion"]=3;Json initial;initial["kind"]="initial";Json frame;frame["kind"]="frame";frame["dt"]=.01;doc.records={initial,frame,tape};Playback playback(doc);
  PlayerObject c,d;c.ship=d.ship=ship;if(buffer)c.m_isOnGround=false;PlayLayer target;target.m_player1=&c;target.m_player2=&d;PlayLayer::current=&target;sourceCbf=false;
  beginCbfNativeFrame(&target,&playback); // set tape tick via production processCommands accounting
  sequence(target);finishCbfNativeFrame();
  if(!cbfNativeFrameError().empty()){std::cerr<<cbfNativeFrameError()<<" "<<playback.failure().dump()<<"\\n";return 1;}
  assert(motion(&c)==expectedA&&motion(&d)==expectedB);assert(point(c.m_lastPosition)==lastA&&point(d.m_lastPosition)==lastB);
  Playback bad(doc);auto stable=c.pos;c.pos.x+=25;beginCbfNativeFrame(&target,&bad);sequence(target);finishCbfNativeFrame();assert(bad.desync());auto first=bad.failure()["reason"];bad.reject("FOLLOWUP_FAILURE");assert(bad.failure()["reason"]==first&&bad.failure()["errorCounts"]["FOLLOWUP_FAILURE"].asInt().unwrap()==1);assert(c.pos.x==stable.x+25);
 }
 // Exercise the actual Native-boundary interpreter under the production
 // render scheduler. Positions/velocities below come from the independent
 // local movement bodies, never the expected pose in the document.
 buffering=false;dual=true;
 for(bool ship:{false,true}){
  PlayerObject a,b;a.ship=b.ship=ship;PlayLayer scene;scene.m_player1=&a;scene.m_player2=&b;PlayLayer::current=&scene;sourceCbf=true;
  Document doc;doc.complete=true;doc.metadata["nativeInputVersion"]=3;doc.metadata["nativeClockVersion"]=1;Json init;init["kind"]="initial";init["timestamp"]=scene.m_timestamp;doc.records.push_back(init);
  for(int n=0;n<40;++n){float durations[]={1.f/30,1.f/60,1.f/144,1.f/240,1.f/120};float dt=durations[n%5];Json frame;frame["kind"]="frame";frame["dt"]=static_cast<double>(dt);doc.records.push_back(frame);beginNativeInputClock(&scene);beginCbfNativeFrame(&scene);scene.m_timestamp=70163.98234975+n*.1;{NativeInputClockScope nativeStepClock("STEP",scene.m_timestamp);worldStep(scene,dt);}doc.records.push_back(finishCbfNativeFrame());doc.records.push_back(finishNativeInputClock());assert(nativeInputClockError().empty());doc.records.push_back(actualPose(scene));}
  for(double speed:{.5,1.,2.,4.}){
   PlayerObject c,d;c.ship=d.ship=ship;PlayLayer target;target.m_player1=&c;target.m_player2=&d;PlayLayer::current=&target;sourceCbf=false;Playback replay(doc);replay.speed(speed);
   auto step=[&]{auto dt=static_cast<float>(replay.current()->dt);beginNativeInputClock(&target,&replay);beginCbfNativeFrame(&target,&replay);target.m_timestamp=90163.98234975+target.m_currentStep*.1;{NativeInputClockScope nativeStepClock("STEP",target.m_timestamp);worldStep(target,dt);}finishCbfNativeFrame();finishNativeInputClock();assert(nativeInputClockError().empty());replay.completeFrame(actualPose(target));assert(!replay.desync());};
   replay.pause(true);replay.accumulate(.1);assert(!replay.due());replay.pause(false);
   int count=0;while(!replay.ended()){replay.accumulate(1./60);while(replay.due()){step();++count;}}assert(count==40&&target.m_currentStep==404&&motion(&c)==motion(&a)&&motion(&d)==motion(&b));
   replay.seek(.1);assert(replay.takeReset());replay.restart();
   auto reset=[](PlayerObject& p){p.pos={0,105};p.m_lastPosition=p.m_shipRotation={};p.rotation=0;p.m_yVelocity=0;p.m_jumpBuffered=false;p.m_isOnGround=true;p.m_holdingButtons.clear();for(auto& log:p.logs)log.size=0;p.m_lastCollisionBottom=p.m_lastCollisionTop=p.m_lastCollisionLeft=p.m_lastCollisionRight=-1;};reset(c);reset(d);target.m_currentStep=364;target.m_timestamp=.5;target.m_gameState.m_levelTime=.75;
   while(replay.due()){step();}assert(!replay.desync()&&replay.time()>=.1&&!replay.seeking());
   replay.seek(replay.duration());while(replay.due()){step();}assert(replay.ended()&&motion(&c)==motion(&a)&&motion(&d)==motion(&b));
  }
 }
 // A late scope from a retired scene cannot touch a new scene clock.
 {PlayLayer oldScene,newScene;PlayLayer::current=&oldScene;beginNativeInputClock(&oldScene);{NativeInputClockScope late("STEP",oldScene.m_timestamp);retireNativeReplayFrame(&oldScene);PlayLayer::current=&newScene;beginNativeInputClock(&newScene);}auto trace=finishNativeInputClock();assert(nativeInputClockError().empty()&&trace["events"].size()==1);assert(finishNativeInputClock().isNull());}
 Hook shipHook{"PlayerObject::updateShipRotation"},slerpHook{"Slerp2D"},clockHook{"GJBaseGameLayer::getModifiedDelta"};
 Hook u{"PlayerObject::update"},r{"PlayerObject::updateRotation"},cmd{"GJBaseGameLayer::processCommands"},world{"GJBaseGameLayer::update"},unrelated{"PlayLayer::levelComplete"};Mod::cbf()->hooks={&u,&r,&cmd,&world,&unrelated};
#if defined(GEODE_IS_WINDOWS)
 Mod::cbf()->hooks.insert(Mod::cbf()->hooks.end(),{&shipHook,&slerpHook,&clockHook});
#endif
 Hook ou{"PlayerObject::update"},iu{"PlayerObject::update"},orr{"PlayerObject::updateRotation"},ir{"PlayerObject::updateRotation"},ih{"GJBaseGameLayer::handleButton"},ic{"GJBaseGameLayer::checkCollisions"};ou.priority=orr.priority=-100;iu.priority=ir.priority=ih.priority=ic.priority=100;Mod::get()->hooks={&ou,&iu,&orr,&ir,&ih,&ic};
 #if defined(GEODE_IS_WINDOWS)
 Hook os{"PlayerObject::updateShipRotation"},is{"PlayerObject::updateShipRotation"},oc{"GJBaseGameLayer::getModifiedDelta"};os.priority=oc.priority=-100;is.priority=100;Mod::get()->hooks.insert(Mod::get()->hooks.end(),{&os,&is,&oc});
 state().slerpAddress=reinterpret_cast<std::uintptr_t>(&baseSlerp);
#endif
 Json env;env["cbf"]["loaded"]=true;env["cbf"]["disabled"]=false;assert(cbfNativeCapability()["verified"].asBool().unwrap());
 assert(acquireCbfReplayHooks(env).empty());assert(!u.enabled&&!r.enabled&&!cmd.enabled&&unrelated.enabled);
#if defined(GEODE_IS_MACOS)
 assert(!world.enabled);
#else
 assert(world.enabled&&!shipHook.enabled&&!clockHook.enabled&&slerpHook.enabled);
#endif
 assert(cbfReplayHooksHeld());releaseCbfReplayHooks();assert(u.enabled&&r.enabled&&cmd.enabled&&world.enabled&&unrelated.enabled&&!cbfReplayHooksHeld());
 // A restoration failure retains ownership and retries only the failed hook.
 assert(acquireCbfReplayHooks(env).empty());u.failEnable=true;releaseCbfReplayHooks();assert(cbfReplayHooksHeld()&&!u.enabled&&r.enabled);u.failEnable=false;releaseCbfReplayHooks();assert(!cbfReplayHooksHeld()&&u.enabled);
 auto priority=iu.priority;iu.priority=-200;assert(!cbfNativeCapability()["verified"].asBool().unwrap());iu.priority=priority;
 cmd.failDisable=true;assert(acquireCbfReplayHooks(env)=="CBF_REPLAY_LEASE_FAILED");assert(u.enabled&&r.enabled&&cmd.enabled&&world.enabled&&!cbfReplayHooksHeld());
 std::cout<<"CBF boundary fixture passed; proprietary GD physics not executed\\n";
}
`);
 execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-isystem',json+'/include','-isystem',result,'-I',join(root,'src'),join(dir,'test.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(p=>json+'/src/'+p+'.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
 execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
