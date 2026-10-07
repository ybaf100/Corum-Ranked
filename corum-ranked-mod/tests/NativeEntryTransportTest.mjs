import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const source=readFileSync(join(root,'src/RankedRuntime.cpp'),'utf8');
function method(marker){const at=source.indexOf(marker);assert(at>=0,marker);let end=source.indexOf('{',at)+1,depth=1;while(depth){if(source[end]==='{')++depth;if(source[end]==='}')--depth;++end;}return source.slice(at,end).replaceAll('std::chrono::steady_clock::now()','testNow()');}

test('actual native scene entry transport is scoped, serialized, retry bounded and independent of attempts',()=>{
 const dir=mkdtempSync(join(tmpdir(),'corum-native-entry-'));
 try{
 const viewer=readFileSync(join(root,'tests/SpectatorLiveViewerNativeTest.mjs'),'utf8');
 const model=viewer.slice(viewer.indexOf('namespace matjson {'),viewer.indexOf('${readFileSync(join(root,'));
 const cpp=String.raw`
#include <cassert>
#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>
#include "${join(root,'src/domain/RoundResourcePolicy.hpp')}"
#include "${join(root,'src/domain/ServerClock.hpp')}"
#include "${join(root,'src/domain/ServerClock.cpp')}"
${model}
namespace geode::log{template<class... T>void info(T...){}template<class... T>void warn(T...){} }
using namespace geode;
namespace fmt{
template<class T>std::string value(T const& t){std::ostringstream s;s<<t;return s.str();}
template<class... T>std::string format(std::string s,T const&... t){auto put=[&](auto const& v){auto at=s.find("{}");if(at!=std::string::npos)s.replace(at,2,value(v));};(put(t),...);return s;}
}
std::chrono::steady_clock::time_point fakeNow{std::chrono::milliseconds(1000)};
auto testNow(){return fakeNow;}
std::int64_t localNowMillis(){return std::chrono::duration_cast<std::chrono::milliseconds>(fakeNow.time_since_epoch()).count();}
namespace web{
struct WebResponse{int status=200;matjson::Value root;int code()const{return status;}matjson::Result<matjson::Value> json()const{return {root};}};
struct Operation{std::string path;matjson::Value body;};
struct WebRequest{matjson::Value body;void bodyJSON(matjson::Value v){body=v;}Operation post(std::string p){return {p,body};}};
struct Pending{Operation operation;std::function<void(WebResponse)> callback;};
std::vector<Pending> pending;
}
struct Request{int cancelled=0;void cancel(){++cancelled;}void spawn(web::Operation o,std::function<void(web::WebResponse)> callback){web::pending.push_back({o,callback});}};
web::WebRequest baseRequest(std::string,std::string){return {};}
bool successful(web::WebResponse const& r){return r.status>=200&&r.status<300;}
std::string responseError(web::WebResponse const&){return "temporary error";}
namespace corum::ranked{
enum class RuntimeStage{Ready,Matched};
struct RankedRuntime{
 struct Match{std::string matchId="match",state="ROUND_ENTERING",side="A";int roundNumber=1,deathmatchSequence=0;
  struct Map{int levelId=10;};std::optional<Map> currentMap=Map{};
  struct Entry{int roundNumber=1,deathmatchSequence=0,levelId=10;bool enteredA=false,enteredB=false;std::string startsAt;}entry;
 };
 struct View{RuntimeStage stage=RuntimeStage::Matched;Match match;}m_view;
 bool placement=false,spectator=false;int parsed=0,errors=0;
 std::string m_sessionToken="session",m_matchToken="match-token",m_nativeEntryKey;
 std::uint64_t m_nativeEntryGeneration=0;bool m_nativeEntryBusy=false;
 std::chrono::steady_clock::time_point m_nextNativeEntryAt{},m_nextPollAt{};
 Request m_nativeEntryRequest;ServerClock m_serverClock;
 bool isPlacement()const{return placement;}bool isSpectating()const{return spectator;}
 std::string endpoint(std::string p){return p;}
 void setTransientError(std::string){++errors;}
 void parseMatchState(matjson::Value const& root){++parsed;m_view.match.entry.enteredA=root["enteredA"].asBool().unwrapOr(false);}
 bool canEnterNativeRoundScene()const;
 bool nativeEntryStartGranted()const;
 std::optional<std::int64_t> nativeEntryCountdownMillis()const;
 void noteNativeRoundSceneEntered(int);
};
${['bool RankedRuntime::canEnterNativeRoundScene()','bool RankedRuntime::nativeEntryStartGranted()','std::optional<std::int64_t> RankedRuntime::nativeEntryCountdownMillis()','void RankedRuntime::noteNativeRoundSceneEntered(int'].map(method).join('\n')}
}
using namespace corum::ranked;
int main(){
 RankedRuntime r;r.m_view.stage=RuntimeStage::Ready;assert(!r.canEnterNativeRoundScene());
 r.m_view.stage=RuntimeStage::Matched;r.m_view.match.state="ROUND_PREPARE";assert(!r.canEnterNativeRoundScene());
 r.m_view.match.state="ROUND_ENTERING";assert(r.canEnterNativeRoundScene());
 r.placement=true;assert(!r.canEnterNativeRoundScene());r.placement=false;r.spectator=true;assert(!r.canEnterNativeRoundScene());r.spectator=false;
 r.noteNativeRoundSceneEntered(999);assert(web::pending.empty());
 r.noteNativeRoundSceneEntered(10);assert(web::pending.size()==1&&r.m_nativeEntryBusy);
 assert(web::pending.back().operation.path=="/api/ranked/matches/match/entered");
 assert(web::pending.back().operation.body["playableLevelId"].asString().unwrap()=="10");
 assert(web::pending.back().operation.body["roundNumber"].asInt().unwrap()==1);
 r.noteNativeRoundSceneEntered(10);assert(web::pending.size()==1);
 auto failed=web::pending.back().callback;failed({503,{}});assert(!r.m_nativeEntryBusy&&r.errors==1);
 r.noteNativeRoundSceneEntered(10);assert(web::pending.size()==1);
 fakeNow+=std::chrono::milliseconds(501);r.noteNativeRoundSceneEntered(10);assert(web::pending.size()==2);
 matjson::Value ack;ack["enteredA"]=true;web::pending.back().callback({200,ack});
 fakeNow+=std::chrono::seconds(2);r.noteNativeRoundSceneEntered(10);assert(web::pending.size()==2&&r.parsed==1);
 // Same map in another round is a different generation; late callback cannot
 // restore entry state or clear the current in-flight request.
 r.m_view.match.roundNumber=2;r.m_view.match.entry.roundNumber=2;r.m_view.match.entry.enteredA=false;
 r.noteNativeRoundSceneEntered(10);assert(web::pending.size()==3);auto old=web::pending.back().callback;
 r.m_view.match.deathmatchSequence=1;r.m_view.match.entry.roundNumber=0;r.m_view.match.entry.deathmatchSequence=1;r.m_view.match.state="DEATHMATCH_ENTERING";
 r.noteNativeRoundSceneEntered(10);assert(web::pending.size()==4);old({200,ack});assert(r.parsed==1&&r.m_nativeEntryBusy);
 web::pending.back().callback({200,ack});assert(r.parsed==2&&!r.m_nativeEntryBusy);
 // A late entry ACK cannot roll an already-started phase back to ENTERING.
 r.m_view.match.entry.enteredA=false;fakeNow+=std::chrono::seconds(1);
 r.noteNativeRoundSceneEntered(10);auto lateEntry=web::pending.back().callback;
 r.m_view.match.state="DEATHMATCH_PLAYING";lateEntry({200,ack});assert(r.parsed==2&&!r.m_nativeEntryBusy);
 r.m_view.match.state="DEATHMATCH_ENTERING";r.m_view.match.entry.enteredA=true;
 r.m_view.match.entry.startsAt="2026-10-07T02:00:03.000Z";
 assert(!r.nativeEntryCountdownMillis());
 assert(r.m_serverClock.observe("2026-10-07T02:00:00.000Z",localNowMillis()));
 assert(r.nativeEntryCountdownMillis()==3000);assert(!r.nativeEntryStartGranted());
 r.m_view.match.entry.enteredB=true;
 fakeNow+=std::chrono::milliseconds(1250);assert(r.nativeEntryCountdownMillis()==1750);assert(!r.nativeEntryStartGranted());
 fakeNow+=std::chrono::milliseconds(1750);assert(r.nativeEntryCountdownMillis()==0);assert(r.nativeEntryStartGranted());
 r.m_view.match.entry.enteredB=false;assert(!r.nativeEntryStartGranted());r.m_view.match.entry.enteredB=true;
 fakeNow+=std::chrono::milliseconds(3000);assert(r.nativeEntryStartGranted());
 fakeNow+=std::chrono::milliseconds(1);assert(!r.nativeEntryStartGranted());
 r.m_view.match.entry.levelId=999;assert(!r.canEnterNativeRoundScene());
}
`;
 writeFileSync(join(dir,'test.cpp'),cpp);const binary=join(dir,'test');
 execFileSync('g++',['-std=c++20','-Wall','-Wextra','-Werror','-pedantic',join(dir,'test.cpp'),'-o',binary],{stdio:'pipe'});
 execFileSync(binary,[],{stdio:'pipe'});
 }finally{rmSync(dir,{recursive:true,force:true});}
});
