import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';import {tmpdir} from 'node:os';
const read=p=>readFileSync(new URL('../'+p,import.meta.url),'utf8');
test('actual Survival spectator runtime gates admission, ACK publication, mode transitions and stale callbacks independently of Free Play',()=>{
 const json=process.env.CORUM_MATJSON_SOURCE,result=process.env.CORUM_RESULT_INCLUDE;assert(json&&result);
 const dir=mkdtempSync(join(tmpdir(),'survival-live-runtime-'));
 try{
 const header=read('src/SurvivalSpectatorRuntime.hpp').replace(/^#include.*$|^#pragma once$/gm,'').replace('private:','public:');
 const code=read('src/SurvivalSpectatorRuntime.cpp').replace(/^#include.*$/gm,'');
 const context=read('src/SpectatorLiveRuntime.hpp').slice(read('src/SpectatorLiveRuntime.hpp').indexOf('struct SurvivalSpectatorTransportContext'),read('src/SpectatorLiveRuntime.hpp').indexOf('class SpectatorLiveRuntime final'));
 const cpp=`
#include <matjson.hpp>
#include <chrono>
#include <optional>
#include <functional>
#include <algorithm>
#include <cassert>
#include <utility>
#include "${new URL('../src/domain/LevelPreparationPolicy.hpp',import.meta.url).pathname}"
#include "${new URL('../src/domain/SpectatorRemotePolicy.hpp',import.meta.url).pathname}"
#include "${new URL('../src/domain/SurvivalFlowPolicy.hpp',import.meta.url).pathname}"
struct CCScene {void* getChildByID(char const*){return this;}};
constexpr char const* operator""_spr(char const*s,std::size_t){return s;}
struct CCDirector {CCScene scene;static CCDirector*sharedDirector(){static CCDirector d;return &d;}CCScene*getRunningScene(){return &scene;}void*getNextScene(){return nullptr;}};
struct GJGameLevel{};struct PlayLayer {static inline PlayLayer*current=nullptr;static PlayLayer*get(){return current;}};
namespace fmt {template<class...T>std::string format(std::string s,T...){return s;}}
namespace geode {namespace prelude{}namespace utils{template<class T>struct N{T unwrapOr(T n){return n;}};template<class T>N<T>numFromString(std::string){return {};}}
namespace utils::web {
struct WebResponse{int status;matjson::Value body;int code()const{return status;}struct J{matjson::Value value;matjson::Value unwrapOr(matjson::Value)const{return value;}};J json()const{return {body};}};
struct Operation{std::string url;};struct WebRequest{template<class...T>void header(T...){}void followRedirects(bool){}template<class T>void timeout(T){}Operation get(std::string s){return{s};}Operation post(std::string s){return{s};}};
struct Pending{Operation op;std::function<void(WebResponse)>done;};inline std::vector<Pending>pending;
}
namespace async{template<class T>struct TaskHolder{void cancel(){}void spawn(utils::web::Operation o,std::function<void(T)>f){utils::web::pending.push_back({o,f});}};}}
using namespace geode::utils;
namespace corum::ranked {
${context}
enum class SpectatorLiveStatus{Idle,Waiting,Live,Syncing,PartialSync,Reconnecting,Buffering,Cleared,Disconnected};
struct SpectatorLiveRuntime{
 SurvivalSpectatorTransportContext context;int captures=0,terminals=0,resets=0;bool viewer=false;
 static SpectatorLiveRuntime&survival(){static SpectatorLiveRuntime r;return r;}
 void configureSurvival(SurvivalSpectatorTransportContext c){context=c;viewer=c.viewer;}
 void stopSurvival(){viewer=false;++resets;}bool survivalViewerAuthorized()const{return viewer;}
 bool tickPublisher(SpectatorGhostPose const&,bool){++captures;return true;}void publishTerminal(SpectatorGhostPose const&,bool){++terminals;}
 void pumpSurvivalPublisher(){}SpectatorLiveStatus status()const{return SpectatorLiveStatus::Waiting;}
 std::optional<SpectatorRemoteFrame>presentationFrame()const{return {};}
};
struct RankedRuntime{std::string session="session";static RankedRuntime&get(){static RankedRuntime r;return r;}std::string spectatorLiveSessionToken()const{return session;}};
struct SurvivalRuntime{
 struct Map{int levelId=123;};struct Row{std::string id;double bestProgress=0,currentProgress=0;};
 struct View{std::string id="match",state="PLAYING",roundKey="R1";int roundNumber=1,mapGeneration=1;Map map;bool ownEliminated=true,online=true,spectatorDemand=true;std::string ownId="viewer";matjson::Value participants;std::vector<std::string>aliveIds{"target"};std::vector<Row>standings;std::optional<double>cutoff=40;}v;
 bool free=false;int lock=0,freeReturn=0;std::string accepted,attempt="native-id";SurvivalOutcomeChoice choice=SurvivalOutcomeChoice::Undecided;
 static SurvivalRuntime&get(){static SurvivalRuntime r;return r;}auto const&view()const{return v;}std::string optionsContext()const{return v.id+v.roundKey+std::to_string(v.mapGeneration);}
 void chooseOutcome(SurvivalOutcomeChoice c){choice=c;}int watchLockSeconds()const{return lock;}bool freePlaying()const{return free;}
 void returnFreePlayForSpectator(){++freeReturn;}bool validateNative(GJGameLevel*)const{return !free;}bool canEnterObserver()const{return choice==SurvivalOutcomeChoice::Observe;}
 auto spectatorAcceptedAttempt()const{return accepted;}auto localAttemptId()const{return attempt;}
 std::optional<std::int64_t>roundRemainingMs()const{return 120000;}
};
struct RankedRoundPreparation{int launches=0;static RankedRoundPreparation&get(){static RankedRoundPreparation r;return r;}bool ready()const{return false;}bool launch(CCScene*){++launches;return true;}};
namespace i18n{inline std::string text(std::string const&,std::string fallback){return fallback;}}
inline int opens=0,returns=0;void showSurvivalSpectatorTargets(){++opens;}bool requestSurvivalGameplayReturn(PlayLayer*){++returns;return true;}
}
${header}
${code}
int main(){using namespace corum::ranked;namespace web=geode::utils::web;
 auto&r=SurvivalSpectatorRuntime::get();auto&s=SurvivalRuntime::get();auto&transport=SpectatorLiveRuntime::survival();
 r.m_targets={{"target","Human","PLAYING",123,1,50,40,true,false},{"bot","Bot","NO_NATIVE_STREAM",0,2,30,10,false,true}};
 s.v.participants=matjson::Value::array();matjson::Value target;target["participantId"]="target";target["eliminationStage"]=nullptr;s.v.participants.push(target);
 s.lock=5;r.openTargets();assert(opens==0);s.lock=0;r.openTargets();assert(opens==1&&s.choice==SurvivalOutcomeChoice::LiveSpectate&&!s.free);
 r.select("bot");assert(web::pending.empty()&&!r.active());
 r.select("target");auto first=web::pending.back().done;assert(!r.active()&&!r.canLaunch());
 first({403,{}});assert(!r.active()&&!r.canLaunch());
 r.select("target");auto old=web::pending.back().done;
 r.select("target");auto current=web::pending.back().done;
 matjson::Value approved;approved["targetId"]="target";approved["context"]["roundKey"]="R1";approved["context"]["mapGeneration"]=1;approved["context"]["levelId"]=123;
 old({200,approved});assert(!r.active());current({200,approved});assert(r.active()&&r.canLaunch()&&!s.free&&transport.viewer);
 GJGameLevel level;assert(r.arm(&level));assert(r.sceneLevel(123));s.v.mapGeneration=2;assert(!r.sceneLevel(123));s.v.mapGeneration=1;assert(r.sceneLevel(123));PlayLayer play;PlayLayer::current=&play;
 r.select("target");assert(r.exitRequested()&&r.active()&&!s.free);r.tick();assert(returns==1);
 r.sceneExited();PlayLayer::current=nullptr;r.tick();assert(!r.active());
 web::pending.back().done({200,approved});assert(r.active());assert(r.arm(&level));PlayLayer::current=&play;
 r.freePlay();assert(r.exitRequested()&&!s.free);r.sceneExited();PlayLayer::current=nullptr;r.tick();assert(s.choice==SurvivalOutcomeChoice::Observe&&!r.active()&&!transport.viewer);
 r.m_startFreePlay=false;s.free=true;r.openTargets();assert(s.freeReturn==1&&!r.active());s.free=false;r.tick();assert(opens>=2&&!r.active());
 // Publishing is gated by an acknowledged exact local Attempt, not an invented UUID.
 s.v.ownEliminated=false;s.accepted.clear();SpectatorGhostPose pose{100,40,0};assert(!r.capture(pose)&&transport.captures==0);
 s.accepted="native-id";assert(r.capture(pose)&&transport.captures==1&&!transport.viewer);
 s.free=true;assert(!r.capture(pose));s.free=false;s.v.spectatorDemand=false;assert(!r.capture(pose));s.v.spectatorDemand=true;
 pose.dead=true;r.terminal(pose);s.accepted="different-attempt";r.tick();assert(transport.terminals==0);
 s.accepted="native-id";r.tick();assert(transport.terminals==1);r.tick();assert(transport.terminals==1);
 // No mode boundary creates a new Attempt or modifies the official identifier.
 assert(s.attempt=="native-id");
}
`;
 const file=join(dir,'runtime.cpp'),bin=join(dir,'runtime');writeFileSync(file,cpp);
 execFileSync('g++',['-std=c++23','-I',json+'/include','-I',result,file,...['value','impl','parser','dump','external/dragonbox'].map(p=>json+'/src/'+p+'.cpp'),'-o',bin],{stdio:'pipe'});execFileSync(bin,[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
