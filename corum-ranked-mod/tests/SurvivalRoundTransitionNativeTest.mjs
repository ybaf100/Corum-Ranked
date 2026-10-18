// Production Survival transport and menu decisions, with controlled HTTP/Cocos
// boundaries. Does not certify FMOD playback or GD scene rendering on a device.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
function method(source,name){const start=source.indexOf(name);assert(start>=0,name);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');end++;}return source.slice(start,end).replaceAll('std::chrono::steady_clock::now()','testNow()');}
function run(code){const dir=mkdtempSync(join(tmpdir(),'survival-round-return-'));try{
 writeFileSync(join(dir,'test.cpp'),code);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}
const viewer=read('tests/SpectatorLiveViewerNativeTest.mjs');
const json=viewer.slice(viewer.indexOf('namespace matjson {'),viewer.indexOf('${readFileSync(join(root,'));
const preamble=`
#include <algorithm>
#include <cassert>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>
${json}
auto wall=std::chrono::steady_clock::time_point{};auto testNow(){return wall;}
namespace geode::log{template<class...T>void info(T const&...){}}
namespace utils::random{std::string generateUUID(){static int n=0;return "event-"+std::to_string(++n);}}
`;

test('ended progress cannot block presentation; pending Start/End retain FIFO, exact retries and callback ownership',()=>{
 const source=read('src/SurvivalRuntime.cpp');
 run(preamble+`
namespace web {
struct WebResponse{int status;matjson::Value root;int code()const{return status;}matjson::Result<matjson::Value>json()const{return {root};}};
struct Operation{matjson::Value body;};struct WebRequest{matjson::Value body;void bodyJSON(matjson::Value v){body=v;}Operation post(std::string const&){return {body};}};
struct Pending{Operation operation;std::function<void(WebResponse)>done;};std::vector<Pending>pending;
}
web::WebRequest request(std::string const&){return {};}
struct Request{int cancellations=0;void cancel(){++cancellations;}void spawn(web::Operation op,std::function<void(web::WebResponse)>fn){web::pending.push_back({op,fn});}};
namespace corum::ranked {
struct SurvivalRuntime{
 struct View{std::string id="match",state="PLAYING",roundKey="r1",error,presentationGate;int mapGeneration=1;struct Map{int levelId=40000000;}map;bool online=false,ownEliminated=false;matjson::Value result;}m_view;
 std::string m_surrenderEventId;bool m_surrenderAccepted=false;void resetSurrender(){m_surrenderEventId.clear();}void pumpSurrenderReturn(){}bool surrenderPending()const{return !m_surrenderEventId.empty()&&!m_surrenderAccepted;}
 bool m_eventBusy=false;std::uint64_t m_generation=1,m_eventGeneration=0;std::string m_session="session",m_presentationContext;
 std::deque<matjson::Value>m_events;std::chrono::steady_clock::time_point m_retryAt{},m_nextPoll{};Request m_eventRequest;int observations=0;
 bool active()const{return !m_view.id.empty();}bool current(std::uint64_t gen,std::string const& session)const{return gen==m_generation&&session==m_session;}
 std::string matchEndpoint(std::string const&path)const{return path;}
 void observe(matjson::Value const&body){++observations;if(body.contains("state"))m_view.state=body["state"].asString().unwrapOr("");discardObsoleteEvents();}
 matjson::Value event(std::string const&)const;void enqueue(matjson::Value);void pumpEvents();void discardObsoleteEvents();void presentationComplete();
};
${['matjson::Value SurvivalRuntime::event(', 'void SurvivalRuntime::enqueue(', 'void SurvivalRuntime::pumpEvents()', 'void SurvivalRuntime::discardObsoleteEvents()', 'void SurvivalRuntime::presentationComplete()'].map(n=>method(source,n)).join('\n')}
}
int main(){using corum::ranked::SurvivalRuntime;SurvivalRuntime r;
 auto start=r.event("ATTEMPT_START");r.enqueue(start);auto progress=r.event("PROGRESS");r.enqueue(progress);auto end=r.event("ATTEMPT_END");r.enqueue(end);
 assert(web::pending.size()==1&&r.m_events.size()==3);web::pending[0].done({201,{}});r.pumpEvents();assert(web::pending.size()==2);
 auto oldProgress=web::pending[1].done;r.m_view.state="ROUND_RESULT";r.m_view.presentationGate="ROUND_RESULT";
 r.discardObsoleteEvents();assert(r.m_eventRequest.cancellations==1&&!r.m_eventBusy&&r.m_events.size()==1);
 assert(r.m_events.front()["eventId"].asString().unwrapOr("")==end["eventId"].asString().unwrapOr(""));
 r.presentationComplete();assert(web::pending.size()==3&&r.m_events.size()==2);r.presentationComplete();assert(r.m_events.size()==2);
 auto observed=r.observations;matjson::Value stale;stale["state"]="PLAYING";oldProgress({200,stale});
 assert(r.observations==observed&&r.m_view.state=="ROUND_RESULT"&&r.m_eventBusy);
 web::pending[2].done({409,{}});r.pumpEvents();assert(web::pending.size()==4);
 auto id=web::pending[3].operation.body["eventId"].asString().unwrapOr("");assert(web::pending[3].operation.body["type"].asString().unwrapOr("")=="PRESENTATION_COMPLETE");
 web::pending[3].done({500,{}});r.presentationComplete();r.pumpEvents();assert(web::pending.size()==4&&r.m_events.size()==1);
 wall+=std::chrono::milliseconds(500);r.pumpEvents();assert(web::pending.size()==5);
 assert(web::pending[4].operation.body["eventId"].asString().unwrapOr("")==id);web::pending[4].done({201,{}});assert(r.m_events.empty());
 // A current playing End is never discarded on a local retry or timer guess.
 r.m_view.state="PLAYING";r.m_view.presentationGate.clear();auto playingEnd=r.event("ATTEMPT_END");r.enqueue(playingEnd);
 auto index=web::pending.size()-1;web::pending[index].done({0,{}});r.discardObsoleteEvents();assert(r.m_events.size()==1);
 wall+=std::chrono::milliseconds(500);r.pumpEvents();assert(web::pending.back().operation.body["eventId"].asString().unwrapOr("")==playingEnd["eventId"].asString().unwrapOr(""));
 auto oldEnd=web::pending.back().done;r.m_view.roundKey="r2";r.m_view.mapGeneration=2;r.discardObsoleteEvents();assert(r.m_events.empty()&&!r.m_eventBusy);
 auto newStart=r.event("ATTEMPT_START");r.enqueue(newStart);oldEnd({200,stale});assert(r.m_eventBusy&&r.m_events.size()==1);
 web::pending.back().done({201,{}});assert(r.m_events.empty());
}
`);
});

test('production music selects survivors, Final preparation and server winners independently of LP and old completed rounds',()=>{
 const source=read('src/ui/SurvivalGameLayer.cpp');
 run(preamble+`
#include "domain/SurvivalRoundMenuPolicy.hpp"
namespace corum::ranked {
enum class RankedAudioMode{Round12,Round3DeathmatchWait,ResultWin,ResultLose};
struct RankedAudioManager{RankedAudioMode mode=RankedAudioMode::Round12;int changes=0,ticks=0;static RankedAudioManager&get(){static RankedAudioManager a;return a;}void setMode(RankedAudioMode m){mode=m;++changes;}void tick(){++ticks;}};
struct SurvivalMatchView{std::string phase="ROUND_1",state="ROUND_RESULT",roundKey="r1",ownId="human";bool ownWinner=false,ownEliminated=false;matjson::Value result,completedRound;};
struct SurvivalRuntime{bool observer=false;static auto& get(){static SurvivalRuntime r;return r;}bool observing()const{return observer;}};
struct Coordinator{std::optional<RankedAudioMode>m_audioMode;${method(source,'void syncAudio(')}};
}
int main(){using namespace corum::ranked;Coordinator coordinator;SurvivalMatchView view;
 matjson::Value own;own["participantId"]="human";own["qualified"]=true;view.completedRound["phase"]="ROUND_1";view.completedRound["key"]="r1";view.completedRound["players"]=matjson::Value::Array{own};
 auto check=[&](RankedAudioMode expected){coordinator.syncAudio(view,true);assert(RankedAudioManager::get().mode==expected);};
 check(RankedAudioMode::Round12);auto changes=RankedAudioManager::get().changes;check(RankedAudioMode::Round12);assert(RankedAudioManager::get().changes==changes);
 view.phase="ROUND_2";view.completedRound["phase"]="ROUND_2";check(RankedAudioMode::Round12);
 own["qualified"]=false;view.completedRound["players"]=matjson::Value::Array{own};check(RankedAudioMode::ResultLose);
 own["qualified"]=true;view.completedRound["players"]=matjson::Value::Array{own};view.phase="ROUND_3";view.completedRound["phase"]="ROUND_3";check(RankedAudioMode::Round3DeathmatchWait);
 for(auto phase:{"FINAL","TIEBREAK"})for(auto state:{"MAP_SELECTION","PREPARING","ENTERING","COUNTDOWN"}){view.phase=phase;view.state=state;coordinator.syncAudio(view);assert(RankedAudioManager::get().mode==RankedAudioMode::Round3DeathmatchWait);}
 view.state="FINAL_RESULT";view.ownWinner=true;check(RankedAudioMode::ResultWin);view.ownWinner=false;check(RankedAudioMode::ResultLose);
 view.result["won"]=true;view.result["actualDelta"]=-1;check(RankedAudioMode::ResultWin);view.result["won"]=false;view.result["actualDelta"]=50;check(RankedAudioMode::ResultLose);
 view.result={};view.state="MAP_SELECTION";view.phase="ROUND_2";coordinator.syncAudio(view);assert(RankedAudioManager::get().mode==RankedAudioMode::Round12);
}
`);
});

test('terminal/pending/cancelled screens cannot advertise another map during the result animation tail',()=>{
 const source=read('src/ui/SurvivalGameLayer.cpp');
 const begin=source.indexOf('        std::string status;'),end=source.indexOf('        m_status->setString',begin);assert(begin>0&&end>begin);
 const status=source.slice(begin,end);
 run(preamble+`
#include "domain/SurvivalRoundMenuPolicy.hpp"
namespace fmt{template<class...T>std::string format(std::string text,T const&...values){auto put=[&](auto const&v){std::ostringstream out;out<<v;auto at=text.find('{');if(at!=std::string::npos){auto end=text.find('}',at);text.replace(at,end-at+1,out.str());}};(put(values),...);return text;}}
namespace i18n{std::string text(std::string const&,std::string fallback){return fallback;}}
std::string text(char const*,char const* fallback){return fallback;}
struct View{std::string state="ROUND_RESULT",error,mapError;bool ownEliminated=false,resultPending=false;int playerCount=16;matjson::Value result;}view;
struct Preparation{bool mapReady=true,audioReady=true;}preparation;
std::string context="r1",m_launchedContext="r1";
bool liveBoard=false,m_manualMap=false;struct Runtime{int remainingMs()const{return 1000;}}runtime;
auto m_eliminationUntil=wall+std::chrono::seconds(3);
std::string render(bool elimination){auto const now=wall;bool intro=false;auto const result=view.result.isObject();
 auto const nextRound=corum::ranked::survivalCanPrepareNext(view.state,view.ownEliminated,view.resultPending,result);
 ${status}
 return status;
}
int main(){assert(render(true)=="RESULT");wall+=std::chrono::seconds(3);assert(render(true)=="NEXT MAP SOON...");
 for(auto state:{"FINAL_RESULT","PLAYING"}){view.state=state;view.resultPending=true;assert(render(true)=="RANK FIXED · SETTLING LP...");}
 view.resultPending=false;view.state="FINAL_RESULT";assert(render(true)=="RANK FIXED · SETTLING LP...");
 view.result["actualDelta"]=50;view.result["profileAfter"]["lp"]=200;assert(render(true).find("NEXT")==std::string::npos);
 view.result={};view.state="CANCELLED";assert(render(true)=="MATCH CANCELLED");
 view.state="ROUND_RESULT";view.ownEliminated=true;assert(render(true)=="RANK FIXED · SETTLING LP...");
 for(auto state:{"MAP_SELECTION","PREPARING","ENTERING"})assert(!corum::ranked::survivalCanPrepareNext(state,true,false,false));
}
`);
});
