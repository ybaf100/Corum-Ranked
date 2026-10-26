import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,mkdirSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('..',import.meta.url));
const read=p=>readFileSync(join(root,p),'utf8');
const source=read('src/SpectatorLiveRuntime.cpp');
function method(name){let a=source.indexOf(name);assert(a>=0,name);let end=source.indexOf('{',a)+1,n=1;while(n){n+=(source[end]==='{')-(source[end]==='}');end++;}return source.slice(a,end).replaceAll('std::chrono::steady_clock::now()','testClockNow()');}

test('production HTTP decoder, bounded restoration, recovery and compositor share the publisher timeline',()=>{
 const json=process.env.CORUM_MATJSON_SOURCE,result=process.env.CORUM_RESULT_INCLUDE;assert(json&&result);
 const d=mkdtempSync(join(tmpdir(),'corum-stage2-'));
 try{
  mkdirSync(join(d,'Geode/binding'),{recursive:true});
  writeFileSync(join(d,'Geode/Geode.hpp'),read('tests/helpers/SpectatorNativeDouble.hpp'));
  for(const n of ['GJBaseGameLayer','PlayerObject'])writeFileSync(join(d,`Geode/binding/${n}.hpp`),'#include <Geode/Geode.hpp>\n');
  const header=read('src/SpectatorLiveRuntime.hpp').replace(/^#include.*$|^#pragma once$/gm,'').replaceAll('private:','public:');
  const methods=['SpectatorLiveStatus SpectatorLiveRuntime::status()', 'void SpectatorLiveRuntime::resetViewer()',
   'void SpectatorLiveRuntime::applyViewerResponse(', 'void SpectatorLiveRuntime::setFallback(',
   'void SpectatorLiveRuntime::requestSceneRestore()', 'std::vector<SpectatorRemoteFrame> SpectatorLiveRuntime::takeRestorationFrames()',
   'void SpectatorLiveRuntime::restorationApplied(', 'void SpectatorLiveRuntime::noteWorldPartial(', 'std::optional<SpectatorRemoteFrame> SpectatorLiveRuntime::presentationFrame()',
   'void SpectatorLiveRuntime::tickViewer()', 'void SpectatorLiveRuntime::pollViewer()'].map(method).join('\n');
  const cpp=`
#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include "spectator/SpectatorPoseCodec.hpp"
#include "spectator/CorumSpectatorCamera.hpp"
#include "spectator/SpectatorPlayerCapture.hpp"
#include "domain/SpectatorPublishQueue.hpp"
#include "domain/SpectatorCapturePolicy.hpp"
#include "domain/SpectatorInterpolationBuffer.hpp"
#include "domain/SpectatorRestoreTimeline.hpp"
#include "domain/SpectatorViewerPollPolicy.hpp"
#include <functional>
#include <sstream>
#include <chrono>
using namespace geode::prelude;
auto wall=std::chrono::steady_clock::time_point(std::chrono::seconds(10));auto testClockNow(){return wall;}
namespace fmt {template<class...T>std::string format(std::string s,T const&...args){
 auto put=[&](auto const& value){std::ostringstream out;out<<value;auto i=s.find("{}");if(i!=s.npos)s.replace(i,2,out.str());};(put(args),...);return s;}}
namespace geode::utils::web {
struct WebResponse {int status=200;matjson::Value root;int code()const{return status;}struct Json{matjson::Value value;matjson::Value unwrapOr(matjson::Value)const{return value;}};Json json()const{return {root};}};
struct Operation{std::string url;};struct WebRequest{Operation get(std::string url){return {url};}};
struct Pending{Operation op;std::function<void(WebResponse)>callback;};inline std::vector<Pending>pending;
}
namespace geode::async {template<class T>struct TaskHolder{void spawn(geode::utils::web::Operation r,std::function<void(T)>f){geode::utils::web::pending.push_back({r,std::move(f)});}};}
namespace web=geode::utils::web;
${header}
namespace corum::ranked {
constexpr int kProtocolVersion=2;constexpr float kPoseDiscontinuityDistance=400;constexpr auto kPacketTimeout=std::chrono::milliseconds(1500);
enum class RuntimeStage{Matched,Ready};
struct RankedRuntime {
 struct Map{int levelId=123;};struct Match{std::string matchId="match";int roundNumber=1,deathmatchSequence=0;bool spectatorLiveEnabled=true;std::optional<Map>currentMap=Map{};};
 struct View{RuntimeStage stage=RuntimeStage::Matched;Match match;}data;bool authority=true,pending=false;
 static RankedRuntime& get(){static RankedRuntime r;return r;}auto const&view()const{return data;}
 bool isPlacement()const{return false;}bool isSpectating()const{return authority;}bool hasLocalAttemptInFlight()const{return pending;}
 std::string spectatorLiveSessionToken()const{return "session";}std::string spectatorLiveMatchToken()const{return "token";}
};
std::string viewerContextKey(RankedRuntime::Match const& m){return fmt::format("{}:R{}:D{}:L{}",m.matchId,m.roundNumber,m.deathmatchSequence,m.currentMap?m.currentMap->levelId:0);}
web::WebRequest spectatorRequest(std::string const&,std::string const&){return {};}
bool successful(web::WebResponse const&r){return r.code()>=200&&r.code()<300;}
${method('float poseDistance(')}
std::string SpectatorLiveRuntime::endpoint(std::string const&p)const{return p;}
${methods}
}
using namespace corum::ranked;
matjson::Value response(int first,int count,int total,bool reset=false,std::string attempt="a",int generation=1){
 matjson::Value root;root["protocolVersion"]=2;root["reset"]=reset;root["stream"]["matchId"]="match";root["stream"]["roundNumber"]=1;
 root["stream"]["attemptId"]=attempt;root["stream"]["generation"]=generation;root["stream"]["opponentName"]="B";
 root["restoration"]["available"]=true;root["restoration"]["levelId"]=123;root["restoration"]["headSequence"]=total-1;
 root["restoration"]["startSequence"]=0;root["restoration"]["caughtUp"]=first+count>=total;
 auto packets=matjson::Value::array();for(int i=first;i<first+count;i++){
  SpectatorGhostPose p{100+i*5.f,40,float(i%360)};p.camera=SpectatorCameraOrigin{p.x-165,-60};
  SpectatorCameraTransform t;t.zoom=1.25;t.angle=42;for(auto& l:t.layers)l={true,1.25,1.25,42};p.camera->transform=t;
  matjson::Value packet;packet["protocolVersion"]=2;packet["matchId"]="match";packet["roundNumber"]=1;packet["levelId"]=123;
  packet["attemptId"]=attempt;packet["generation"]=generation;packet["sequence"]=i;packet["elapsedMs"]=i*50;packet["keyframe"]=encodeSpectatorPose(p);packets.push(packet);
 }root["packets"]=packets;return root;
}
void attach(SpectatorLiveRuntime& r){r.resetViewer();r.m_viewMatchId="match";r.m_viewContextKey=viewerContextKey(RankedRuntime::get().view().match);}
int main(){
 // Production decoding stages all pages on the source clock, never showing the future live pose as restored.
 for(int duration:{0,10000,60000,120000}){
  SpectatorLiveRuntime r;attach(r);int total=duration/50+1;
  for(int page=0;page<total;page+=64){r.applyViewerResponse(response(page,std::min(64,total-page),total,page==0));assert(r.m_restoreTimeline.queued()<=4096);}
  assert(r.status()==SpectatorLiveStatus::Syncing && r.presentationFrame()->sequence==0);
  int renders=0;while(r.m_restoreTimeline.active()){
   auto frames=r.takeRestorationFrames();assert(!frames.empty()&&frames.size()<=8);
   for(auto const& f:frames){r.restorationApplied(f,true,.1);}
   assert(++renders<700);
  }
  assert(r.m_restoreTimeline.appliedCount()==unsigned(total));assert(r.presentationFrame()->publisherElapsedMs==duration);
  assert(r.presentationFrame()->pose->camera->transform->angle==42);
  assert(r.status()==(duration?SpectatorLiveStatus::PartialSync:SpectatorLiveStatus::Live));
  auto old=r.m_viewPresentationEpoch;auto corrupt=response(total,1,total+1);corrupt["packets"][0]["levelId"]=456;
  r.applyViewerResponse(corrupt);assert(r.m_reason=="PACKET_LEVEL_MISMATCH"&&r.m_viewPresentationEpoch==old);
  auto held=r.presentationFrame();assert(held&&held->publisherElapsedMs==duration);
  auto fresh=response(0,1,1,true,"new-attempt",2);r.applyViewerResponse(fresh);assert(r.m_viewPresentationEpoch>old);
  assert(r.presentationFrame()->context.attemptId=="new-attempt");r.restorationApplied(held.value(),true,.1);
  assert(r.m_restoreTimeline.appliedCount()==0); // obsolete completion cannot mutate a new world
 }
 // Corrupt descriptors cannot complete restoration, and a mid-replay context change cancels old completions.
 {SpectatorLiveRuntime r;attach(r);auto corrupt=response(0,1,1,true);corrupt["restoration"]["startSequence"]=5;
  r.applyViewerResponse(corrupt);assert(r.m_reason=="INVALID_RESTORE_DESCRIPTOR"&&!r.m_restoreTimeline.active());
  auto history=response(0,64,2401,true);r.applyViewerResponse(history);auto batch=r.takeRestorationFrames();assert(!batch.empty());
  auto old=batch.front();r.applyViewerResponse(response(0,1,1,true,"replacement",2));r.restorationApplied(old,true,.1);
  assert(r.m_restoreTimeline.appliedCount()==0&&r.presentationFrame()->context.attemptId=="replacement");}
 // Terminal received during sync remains ordered and cannot be followed by motion in the same Attempt.
 {SpectatorLiveRuntime r;attach(r);auto terminal=response(0,3,3,true);terminal["packets"][2]["keyframe"]["finished"]=true;
  r.applyViewerResponse(terminal);assert(r.status()==SpectatorLiveStatus::Syncing);
  while(r.m_restoreTimeline.active())for(auto const& f:r.takeRestorationFrames())r.restorationApplied(f,true,.1);
  assert(r.presentationFrame()->pose->finished&&r.status()==SpectatorLiveStatus::Cleared);
  auto count=r.m_restoreTimeline.appliedCount();r.applyViewerResponse(response(3,1,4));
  assert(r.m_reason=="TERMINAL_STREAM_CANNOT_RESUME"&&r.presentationFrame()->pose->finished&&r.m_restoreTimeline.appliedCount()==count);}
 // A restart with no beginning is explicitly partial; no invented fast-forward or successful checkpoint claim.
 {SpectatorLiveRuntime r;attach(r);auto p=response(1200,2,1202,true);p["restoration"]["available"]=false;p["restoration"]["reason"]="ATTEMPT_START_MISSING";
  r.applyViewerResponse(p);assert(!r.m_restoreTimeline.active()&&r.status()==SpectatorLiveStatus::PartialSync);assert(r.m_reason=="ATTEMPT_START_MISSING");}
 // Real serial HTTP callback, bounded errors, explicit reopen and stale authority/context fences.
 {SpectatorLiveRuntime r;attach(r);web::pending.clear();
  for(int i=0;i<12;i++){wall+=std::chrono::seconds(5);r.tickViewer();assert(web::pending.size()==unsigned(i+1));web::pending.back().callback({503,{}});}
  assert(r.status()==SpectatorLiveStatus::Disconnected);auto count=web::pending.size();wall+=std::chrono::seconds(10);r.tickViewer();assert(web::pending.size()==count);
  r.requestSceneRestore();r.tickViewer();assert(web::pending.size()==count+1&&web::pending.back().op.url.find("worldHistory=1")!=std::string::npos);
  auto stale=web::pending.back().callback;RankedRuntime::get().data.match.roundNumber=2;stale({200,response(0,1,1,true)});assert(r.m_lastViewSequence==-1);
  RankedRuntime::get().data.match.roundNumber=1;r.m_viewBusy=false;r.tickViewer();RankedRuntime::get().authority=false;web::pending.back().callback({200,response(0,1,1,true)});assert(!r.m_restoreTimeline.active()&&r.m_viewMatchId.empty());RankedRuntime::get().authority=true;
 }
 // The actual compositor applies sampled native transforms on all three layers and releases ownership.
 {GJBaseGameLayer layer;auto* world=CCLayer::create();auto* shader=CCLayer::create();auto* above=CCLayer::create();
  geode::Ref<CCLayer> a(world),b(shader),c(above);layer.m_objectLayer=world;layer.m_inShaderObjectLayer=shader;layer.m_aboveShaderObjectLayer=above;
  layer.m_gameState.m_cameraZoom=.8f;layer.m_gameState.m_cameraAngle=15;layer.m_gameState.m_targetCameraZoom=.9f;layer.m_gameState.m_targetCameraAngle=20;world->setScale(2);world->setRotation(15);
  auto original=captureSpectatorCameraTransform(&layer);assert(original&&original->layers[0].scaleX==2);
  SpectatorRemoteContext context{"match",1,0,123,"a",1,1};auto pose=*decodeSpectatorPose(response(1,1,2)["packets"][0]["keyframe"]);
  SpectatorRemoteFrame f{context,1,pose,50};CorumSpectatorCamera camera;camera.update(&layer,true,context,f);
  for(auto* n:{world,shader,above}){assert(n->getScaleX()==1.25f&&n->getRotation()==42);auto p=CCPointApplyAffineTransform({pose.camera->x,pose.camera->y},n->nodeToWorldTransform());assert(std::abs(p.x)<.001&&std::abs(p.y)<.001);}
  assert(layer.m_gameState.m_cameraZoom==1.25&&layer.m_gameState.m_cameraAngle==42);
  auto wrong=f;wrong.context.levelId=456;wrong.pose->camera->transform->zoom=3;wrong.pose->camera->transform->layers[0].scaleX=3;
  camera.update(&layer,true,context,wrong);assert(layer.m_gameState.m_cameraZoom==1.25&&world->getScaleX()==1.25f);
  layer.m_gameState.m_targetCameraZoom=10;layer.m_gameState.m_targetCameraAngle=400;camera.cleanup(&layer);
  assert(world->getScaleX()==2&&world->getRotation()==15&&layer.m_gameState.m_cameraZoom==.8f&&layer.m_gameState.m_cameraAngle==15);
  assert(layer.m_gameState.m_targetCameraZoom==.9f&&layer.m_gameState.m_targetCameraAngle==20);
  CCObject::drain();}
 // Production interpolation under bounded serialized response delays; no packet timestamp rewriting/extrapolation.
 for(int rtt:{20,80,150,300,500}){
  SpectatorInterpolationBuffer buffer;int next=0;double nextResponse=rtt+50,lastX=100;
  for(int ms=0;ms<11000;ms+=10){
   if(ms>=nextResponse && next<=100){int available=std::min(100,(ms-rtt)/50);int count=std::max(0,available-next+1);buffer.noteResponse(ms,rtt,20,count);
    while(next<=available){SpectatorGhostPose p{100.f+next,40,0};p.finished=next==100;assert(buffer.push({next,next*50.0,p},false,ms));++next;}nextResponse=ms+rtt+20;}
   if(auto p=buffer.render(ms)){assert(p->pose.x>=lastX&&p->pose.x<=100+next-1);lastX=p->pose.x;assert(buffer.size()<=64&&buffer.delayMs()<=800);}
  }assert(lastX==200&&buffer.terminalRendered());}
 // Actual capture policy proves instant zoom/rotation as an urgent atomic camera boundary.
 {SpectatorCapturePolicy capture;SpectatorGhostPose p;SpectatorCameraTransform t;for(auto& l:t.layers)l={true,1,1,0};
  p.camera=SpectatorCameraOrigin{0,0,t};capture.observe(p);capture.sampled();p.camera->transform->zoom=2;
  capture.observe(p);assert(capture.snapshot()->cameraSnap);capture.sampled();p.camera->transform->zoom=2.1f;p.camera->transform->nativeTweening=true;
  capture.observe(p);assert(!capture.snapshot()->cameraSnap);}
 // Corrupt compositor data is rejected as a whole; mirrored scale switches atomically, never through zero.
 {auto p=response(0,1,1)["packets"][0]["keyframe"];p["cameraLayers"][0]["scaleX"]=0;assert(!decodeSpectatorPose(p));
  SpectatorCameraTransform a,b;for(auto& l:a.layers)l={true,1,1,170};b=a;b.layers[0].scaleX=-1;b.layers[0].rotation=-170;
  auto t=interpolateSpectatorCameraTransform(a,b,.5);assert(t.layers[0].scaleX==1&&std::abs(t.layers[0].rotation-180)<.001);}
}
`;
  writeFileSync(join(d,'test.cpp'),cpp);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',d,'-I',join(root,'src'),'-isystem',join(json,'include'),'-isystem',result,
   join(d,'test.cpp'),join(root,'src/spectator/CorumSpectatorCamera.cpp'),join(root,'src/spectator/SpectatorPlayerCapture.cpp'),
   ...['value','impl','parser','dump','external/dragonbox'].map(p=>join(json,'src',p+'.cpp')),'-o',join(d,'test')],{stdio:'pipe'});
  execFileSync(join(d,'test'),[],{stdio:'pipe'});
 }catch(e){throw Error(e.stderr?.toString()||e.message);}finally{rmSync(d,{recursive:true,force:true});}
});
