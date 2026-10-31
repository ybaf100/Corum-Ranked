import test from 'node:test';import assert from 'node:assert/strict';import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';import {join,resolve} from 'node:path';import {tmpdir} from 'node:os';import {execFileSync} from 'node:child_process';
const root=resolve(new URL('..',import.meta.url).pathname),source=readFileSync(join(root,'src/spectator/SpectatorNativeWorldEvents.cpp'),'utf8');
function method(signature){const start=source.indexOf(signature);assert(start>=0);let p=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[p]==='{')-(source[p]==='}');++p;}return source.slice(start,p);}
test('production Random native activation/output hooks record real selected groups and replay exact branches without changing official RNG calls',()=>{
 const dir=mkdtempSync(join(tmpdir(),'native-random-events-'));try{
 const json=process.env.CORUM_MATJSON_SOURCE,result=process.env.CORUM_RESULT_INCLUDE;assert(json&&result);
 const structs=source.slice(source.indexOf('struct ObjectIdentity'),source.indexOf('replay::Playback *replayOwner'));
 const cpp=`
#include "${root}/src/replay/SurvivalReplayPlayback.hpp"
#include <cassert>
#include "${root}/src/domain/SpectatorCapturePolicy.hpp"
#include <deque>
#include "${root}/src/domain/SpectatorSourceTrace.hpp"
#include <set>
#include <unordered_map>
using namespace corum::ranked;
namespace geode::log{template<class...T>void warn(T const&...){}template<class...T>void info(T const&...){}}
using namespace geode;
namespace gd{template<class T>using vector=std::vector<T>;}
struct GameObject{virtual ~GameObject()=default;};struct GJGameLevel{int m_levelID=123;};
struct GJBaseGameLayer{std::vector<int>groups;virtual ~GJBaseGameLayer()=default;virtual void spawnGroup(int group,bool,double,gd::vector<int>const&,int,int){groups.push_back(group);}};
${structs}
struct OutputHook:GJBaseGameLayer{${method('    void spawnGroup(')}};
struct PlayLayer:OutputHook{GJGameLevel level;GJGameLevel*m_level=&level;bool m_isTestMode=false,m_isPracticeMode=false;int m_currentStep=12;struct{double m_levelTime=.05;}m_gameState;
 struct Fields{std::unordered_map<GameObject*,ObjectIdentity>objects;bool remote=false,applyingSource=false;std::set<int>missingActivations;std::unordered_map<int,unsigned>nativeActivations,sourceActivations;unsigned missingLogs=0;std::string scope;std::uint64_t ordinal=0;std::deque<SpectatorRandomEvent>expected;std::vector<SpectatorRandomEvent>captured;SpectatorRandomEvents snapshot;unsigned misses=0;}fields;Fields*m_fields=&fields;int identityBuilds=0;void buildObjects(){++identityBuilds;} };using CorumNativeWorldEventLayer=PlayLayer;
static PlayLayer*current=nullptr;static replay::Playback*ownedPlayback=nullptr;
PlayLayer*owner(GJBaseGameLayer*p){return current&&static_cast<GJBaseGameLayer*>(current)==p?current:nullptr;}
replay::Playback*replayOwner(PlayLayer*p){return p==current?ownedPlayback:nullptr;}
${method('void emitSpawn(')}
namespace corum::ranked{struct SurvivalRuntime{bool official=true;static SurvivalRuntime&get(){static SurvivalRuntime s;return s;}bool gameplayLevel(int)const{return official;}bool freePlaying()const{return false;}std::string localAttemptId()const{return "official-attempt";}};
struct SpectatorLiveRuntime{std::string reason;static SpectatorLiveRuntime&get(){static SpectatorLiveRuntime s;return s;}void noteWorldPartial(std::string s){reason=s;}void worldCauseRecovered(std::string const&s){if(reason==s)reason.clear();}};
namespace replay{struct Recorder{bool enabled=true;std::vector<Json>records;static Recorder&get(){static Recorder r;return r;}bool active()const{return enabled;}bool inNativeFrame()const{return enabled;}void add(Json j){records.push_back(std::move(j));}};}}
struct RandTriggerGameObject:GameObject{int m_objectID=1912,nativeChoice=7,nativeCalls=0,m_controlID=2;virtual void triggerObject(GJBaseGameLayer*layer,int uniqueId,gd::vector<int>const*){++nativeCalls;if(nativeChoice>=0)layer->spawnGroup(nativeChoice,false,0,{},uniqueId,m_controlID);}};
struct RandomHook:RandTriggerGameObject{${method('    void triggerObject(')}};
replay::Playback playback(matjson::Value event){replay::Document d;d.complete=true;matjson::Value initial;initial["kind"]="initial";initial["randomSeed"]="1";initial["replayRandSeed"]="2";matjson::Value frame;frame["kind"]="frame";frame["dt"]=.05;auto pose=encodeSpectatorPose(SpectatorGhostPose{});pose["kind"]="pose";pose["progress"]=20.;d.records={initial,frame,event,pose};return replay::Playback(std::move(d));}
int main(){PlayLayer scene;current=&scene;RandomHook random;scene.fields.objects[&random]={0,1912,100,200};auto& recorder=replay::Recorder::get();
 random.triggerObject(&scene,11,nullptr);assert(scene.groups.back()==7&&random.nativeCalls==1&&recorder.records.size()==1);assert(scene.identityBuilds==0);auto sourceEvent=recorder.records[0];assert(sourceEvent["event"]["outputs"][0]["group"].asInt().unwrap()==7);
 random.nativeChoice=42;random.triggerObject(&scene,11,nullptr);assert(scene.groups.back()==42&&random.nativeCalls==2&&recorder.records.size()==2);
 SurvivalRuntime::get().official=false;auto viewer=playback(sourceEvent);ownedPlayback=&viewer;random.nativeChoice=999;random.triggerObject(&scene,11,nullptr);assert(scene.groups.back()==7&&random.nativeCalls==2&&recorder.records.size()==2&&!viewer.desync());
 auto zeroChoice=playback(sourceEvent);ownedPlayback=&zeroChoice;random.nativeChoice=-1;random.triggerObject(&scene,11,nullptr);assert(scene.groups.back()==7&&random.nativeCalls==2&&!zeroChoice.desync());
 auto changed=sourceEvent;changed["event"]["objectId"]=999;auto wrongMap=playback(changed);ownedPlayback=&wrongMap;auto count=scene.groups.size();random.triggerObject(&scene,11,nullptr);assert(wrongMap.desync()&&scene.groups.size()==count&&random.nativeCalls==2);
 RandomCall scoped;scoped.layer=&scene;{RandomScope active(scoped);OutputHook foreign;foreign.spawnGroup(77,false,0,{},0,0);assert(foreign.groups.back()==77&&scoped.event.outputs.empty());}
 ownedPlayback=nullptr;random.nativeChoice=55;random.triggerObject(&scene,11,nullptr);assert(scene.groups.back()==55&&random.nativeCalls==3&&recorder.records.size()==2); // ordinary gameplay is an exact pass-through
 {// PARTIAL SYNC (beta.125 log): native activation before OR after the source event.
  auto& live=SpectatorLiveRuntime::get();auto e=*decodeSpectatorRandomEvent(sourceEvent["event"]);auto apply=[&]{scene.fields.expected.push_back(e);scene.fields.applyingSource=true;random.triggerObject(&scene,11,nullptr);scene.fields.applyingSource=false;};
  scene.fields.remote=true;live.reason.clear();
  random.triggerObject(&scene,11,nullptr);assert(live.reason=="RANDOM_SOURCE_ACTIVATION_MISSING");apply();assert(live.reason.empty()&&scene.fields.missingActivations.empty()); // native ahead, then source
  apply();random.triggerObject(&scene,11,nullptr);assert(live.reason.empty()&&scene.fields.missingActivations.empty()); // source first, late native mirror
  random.triggerObject(&scene,11,nullptr);assert(live.reason=="RANDOM_SOURCE_ACTIVATION_MISSING"); // an extra native activation with no source event stays partial
  scene.fields.remote=false;scene.fields.missingActivations.clear();scene.fields.nativeActivations.clear();scene.fields.sourceActivations.clear();live.reason.clear();}
 auto encoded=sourceEvent["event"];assert(decodeSpectatorRandomEvent(encoded));encoded["objectIndex"]=4294967296LL;assert(!decodeSpectatorRandomEvent(encoded));
 // Multiple verified native snaps may cancel their gravity flips before a 50ms packet.
 SpectatorGhostPose before;before.mode=SpectatorGameMode::Spider;before.x=0;before.y=100;auto middle=before;middle.x=1;middle.y=1000;middle.upsideDown=true;auto after=before;after.x=10;after.y=120;
 SpectatorCapturePolicy capture;capture.observe(before);capture.nativeSnap(SpectatorDiscontinuity::SpiderSnap,before,middle);capture.observe(middle);capture.nativeSnap(SpectatorDiscontinuity::SpiderSnap,middle,after);capture.observe(after);auto combined=capture.snapshot();assert(combined&&combined->discontinuity&&combined->discontinuity->nativeEvents.size()==2&&verifiedSpectatorSnap(before,*combined));auto wire=encodeSpectatorPose(*combined);assert(decodeSpectatorPose(wire));combined->discontinuity->nativeEvents.clear();assert(!verifiedSpectatorSnap(before,*combined));
 // Codec/capture policy fixtures exercise diagnostic identity only; these
 // synthetic callbacks are NOT positive Native Entropie reproduction evidence.
 capture.reset();before.mode=SpectatorGameMode::Ship;before.x=25885.127f;before.y=4044;after=before;after.x=25894.729f;after.y=5439;
 assert(!verifiedSpectatorSnap(before,after));capture.observe(before);
 SpectatorSnapTrace trace{1,12,3,71295,"PORTAL"};assert(std::string(capture.nativeSnap(SpectatorDiscontinuity::Teleport,before,after,trace))=="CAPTURED");capture.observe(after);
 auto encodedTrace=encodeSpectatorPose(*capture.snapshot());auto decodedTrace=decodeSpectatorPose(encodedTrace);assert(decodedTrace&&decodedTrace->discontinuity->nativeEvents[0].trace->event==1);
 encodedTrace["discontinuity"]["nativeEvents"][0]["trace"]["callback"]="POSITION_DELTA";assert(!decodeSpectatorPose(encodedTrace));
 capture.sampled();assert(!capture.snapshot()->discontinuity);
 SpectatorSourceTrace budget;for(int i=0;i<1000;++i){(void)budget.emit(i%16,143308401,70.0+i*.003,25890,false);(void)budget.emit(0,143308401,71,25890,true);}assert(budget.logs<=160&&budget.criticalLogs==32);assert(!budget.emit(0,1,71,25890,true));
 SpectatorRandomTimeline timeline;auto e=*decodeSpectatorRandomEvent(sourceEvent["event"]);auto events=std::make_shared<std::vector<SpectatorRandomEvent>const>(std::vector{e});assert(timeline.push(events,50,100));assert(!timeline.through(0));assert(timeline.through(100)->size()==1);assert(timeline.push(events,50,100));assert(timeline.through(150)->size()==1);e.ordinal=3;assert(!timeline.push(std::make_shared<std::vector<SpectatorRandomEvent>const>(std::vector{e}),100,50));timeline.reset();assert(!timeline.through(100));
}
`;
 writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-pthread','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-isystem',json+'/include','-isystem',result,join(dir,'test.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(p=>json+'/src/'+p+'.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
