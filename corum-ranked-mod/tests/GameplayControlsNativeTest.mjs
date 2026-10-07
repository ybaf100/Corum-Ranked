import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
const source=read('src/RankedRuntime.cpp');
const body=name=>{const at=source.indexOf(name);assert(at>=0,name);const open=source.indexOf('{',at);let end=open+1,depth=1;while(depth){if(source[end]==='{')++depth;if(source[end]==='}')--depth;++end;}return source.slice(at,end);};

test('actual gameplay controls serialize pause leases, preserve 30s across rounds and retry exact surrender scope',()=>{
    const viewer=read('tests/SpectatorLiveViewerNativeTest.mjs');
    const model=viewer.slice(viewer.indexOf('namespace matjson {'),viewer.indexOf('${readFileSync(join(root,'))
        .replace('Value()=default;', 'Value():data(Object{}){}');
    const methods=['std::string RankedRuntime::beginMatchGameplayPause(', 'void RankedRuntime::endMatchGameplayPause(',
        'double RankedRuntime::matchPauseRemainingMs()', 'void RankedRuntime::flushGameplayControls()',
        'void RankedRuntime::resetGameplayControls()', 'void RankedRuntime::surrenderCurrentMatch(',
        'bool RankedRuntime::gameplayConceded('].map(body).join('\n').replaceAll('std::chrono::steady_clock::now()','testNow()');
    const cpp=`
#include "${join(root,'src/domain/MatchPauseBudget.hpp')}"
#include <cassert>
#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <variant>
#include <vector>
${model}
namespace corum::ranked {
namespace log { template<class... T> void info(T...) {} template<class... T> void warn(T...) {} }
inline auto gameplayTestClock=std::chrono::steady_clock::time_point(std::chrono::milliseconds(1000));
auto testNow(){return gameplayTestClock;}
double gameplayControlNowMs(){return std::chrono::duration<double,std::milli>(gameplayTestClock.time_since_epoch()).count();}
namespace web {
struct WebResponse {int status=200;matjson::Value root;int code()const{return status;}matjson::Result<matjson::Value> json()const{return {root};}};
struct Operation{std::string path,session,token;matjson::Value body;};
struct WebRequest {std::string session,token;matjson::Value body;void bodyJSON(matjson::Value v){body=std::move(v);}Operation post(std::string p){return {p,session,token,body};}};
struct Pending{Operation operation;std::function<void(WebResponse)> callback;};inline std::vector<Pending> pending;
}
namespace geode::async{template<class T>struct TaskHolder{void cancel(){}void spawn(web::Operation op,std::function<void(T)> cb){web::pending.push_back({op,cb});}};}
web::WebRequest baseRequest(std::string session,std::string token){return {session,token,{}};}
bool successful(web::WebResponse const& r){return r.status>=200&&r.status<300;}
std::string responseError(web::WebResponse const&){return "permanent rejection";}
bool stateAllowsActiveAttempt(std::string const& state){return state=="ROUND_PLAYING"||state=="ROUND_SETTLING"||state=="DEATHMATCH_PLAYING";}
enum class RuntimeStage {Ready,Matched};
struct SpectatorLiveRuntime {
 bool paused=false;static SpectatorLiveRuntime& get(){static SpectatorLiveRuntime s;return s;}
 void publisherPaused(bool value){paused=value;}
};
struct RankedRuntime {
 struct Match {std::string matchId="match",state="ROUND_PLAYING",surrenderScope;int roundNumber=1,deathmatchSequence=0,surrenderRound=0,surrenderDeathmatch=0;};
 struct View {RuntimeStage stage=RuntimeStage::Ready;Match match;} m_view;
 bool placement=false,pendingResult=false,granted=false;int event=0,parsed=0,errors=0,attemptFlushes=0;
 std::string m_sessionToken="session",m_matchToken="match-token";
 MatchPauseBudget m_matchPauseBudget;std::string m_pauseMatchId,m_pauseLeaseId;
 bool m_pauseDesired=false,m_pauseNeedsSync=false,m_pauseBusy=false,m_surrenderBusy=false;
 enum class SurrenderStatus {Idle,WaitingAttempt,Sending,Retrying,Accepted,Rejected};
 SurrenderStatus m_surrenderStatus=SurrenderStatus::Idle;
 std::chrono::steady_clock::time_point m_nextPauseAt{},m_nextSurrenderAt{};std::optional<matjson::Value> m_pendingSurrender;
 std::string m_surrenderMatchId;std::uint64_t m_surrenderRequestGeneration=0;
 geode::async::TaskHolder<web::WebResponse> m_pauseRequest,m_surrenderRequest;
 bool nativeEntryStartGranted()const{return granted;}bool isPlacement()const{return placement;}bool isGameplayLevel(int level)const{return level==123;}
 bool hasLocalAttemptInFlight()const{return pendingResult;}bool hasLocalOpenAttempt()const{return false;}
 void flushAttemptEvents(){++attemptFlushes;}
 std::string newEventId(char const* kind){return std::string(kind)+std::to_string(++event);}
 std::string endpoint(std::string path){return path;}
 void setTransientError(std::string){++errors;}void parseMatchState(matjson::Value const&){++parsed;}
 std::string beginMatchGameplayPause(int);void endMatchGameplayPause(std::string const&);
 double matchPauseRemainingMs()const;void flushGameplayControls();void resetGameplayControls();
 void surrenderCurrentMatch(bool);bool gameplayConceded(int,int)const;
};
${methods}
}
int main(){using namespace corum::ranked;
 assert(matjson::Value{}.isObject()); // real matjson constructor semantics
 RankedRuntime r;assert(r.beginMatchGameplayPause(123).empty()&&web::pending.empty());
 r.m_view.stage=RuntimeStage::Matched;r.placement=true;assert(r.beginMatchGameplayPause(123).empty());r.placement=false;
 assert(r.beginMatchGameplayPause(999).empty());
 auto one=r.beginMatchGameplayPause(123);assert(!one.empty()&&web::pending.size()==1&&SpectatorLiveRuntime::get().paused);
 gameplayTestClock+=std::chrono::seconds(12);r.endMatchGameplayPause(one);assert(web::pending.size()==1&&r.matchPauseRemainingMs()==18000&&!SpectatorLiveRuntime::get().paused);
 matjson::Value ack;ack["remainingMs"]=30000;ack["accepted"]=true;web::pending[0].callback({200,ack});
 r.flushGameplayControls();assert(web::pending.size()==2&&!web::pending[1].operation.body["paused"].asBool().unwrapOr(true));
 r.m_view.match.roundNumber=2;auto two=r.beginMatchGameplayPause(123);assert(two!=one&&web::pending.size()==2);
 ack["remainingMs"]=18000;web::pending[1].callback({200,ack});r.flushGameplayControls();assert(web::pending.size()==3);
 r.endMatchGameplayPause(one);assert(r.m_pauseDesired); // late scene destruction cannot End a newer lease
 gameplayTestClock+=std::chrono::seconds(18);assert(r.matchPauseRemainingMs()==0);r.endMatchGameplayPause(two);
 web::pending[2].callback({200,ack});r.flushGameplayControls();ack["remainingMs"]=0;web::pending[3].callback({200,ack});
 assert(r.matchPauseRemainingMs()==0);r.m_view.match.roundNumber=3;assert(r.matchPauseRemainingMs()==0);
 RankedRuntime s;s.m_view.stage=RuntimeStage::Matched;s.m_view.match.roundNumber=2;
 auto pendingCount=web::pending.size();s.pendingResult=true;s.surrenderCurrentMatch(false);
 assert(web::pending.size()==pendingCount&&s.attemptFlushes==1&&s.m_pendingSurrender);
 assert(s.m_surrenderStatus==RankedRuntime::SurrenderStatus::WaitingAttempt);
 auto before=web::pending.size();s.pendingResult=false;s.flushGameplayControls();assert(web::pending.size()==before+1);
 auto request=web::pending.back().operation;assert(request.body["scope"].asString().unwrapOr("")=="ROUND");
 assert(request.body["roundNumber"].asInt().unwrapOr(0)==2&&request.token=="match-token");
 s.surrenderCurrentMatch(true);assert(web::pending.size()==before+1);
 web::pending.back().callback({500,{}});gameplayTestClock+=std::chrono::milliseconds(500);s.flushGameplayControls();
 assert(web::pending.back().operation.body["roundNumber"].asInt().unwrapOr(0)==2); // retry cannot concede a different round
 assert(web::pending.back().operation.body["clientEventId"].asString().unwrapOr("")==request.body["clientEventId"].asString().unwrapOr("x"));
 web::pending.back().callback({409,{}});assert(!s.m_pendingSurrender&&s.errors==1&&s.m_surrenderStatus==RankedRuntime::SurrenderStatus::Rejected);
 s.surrenderCurrentMatch(true);assert(web::pending.back().operation.body["scope"].asString().unwrapOr("")=="MATCH");
 auto old=web::pending.back().callback;s.resetGameplayControls();old({200,{}});assert(!s.m_surrenderBusy&&s.parsed==0);
 // Same-match reset also invalidates older callbacks, not merely match ID checks.
 s.surrenderCurrentMatch(false);web::pending.back().callback({500,{}});
 gameplayTestClock+=std::chrono::milliseconds(500);s.m_view.match.roundNumber=3;
 before=web::pending.size();s.flushGameplayControls();assert(web::pending.size()==before&&!s.m_pendingSurrender);
 // A certified GO can arrive before the next poll changes ENTERING to PLAYING.
 RankedRuntime e;e.m_view.stage=RuntimeStage::Matched;e.m_view.match.state="ROUND_ENTERING";
 before=web::pending.size();assert(e.beginMatchGameplayPause(123).empty());e.surrenderCurrentMatch(false);
 assert(web::pending.size()==before&&!e.m_pendingSurrender);
 e.granted=true;assert(!e.beginMatchGameplayPause(123).empty());e.surrenderCurrentMatch(false);
 assert(e.m_pendingSurrender&&e.m_surrenderStatus==RankedRuntime::SurrenderStatus::Sending);
 // Even an in-flight success cannot project the older round after a newer GET.
 RankedRuntime late;late.m_view.stage=RuntimeStage::Matched;late.surrenderCurrentMatch(false);
 auto oldRoundAck=web::pending.back().callback;late.m_view.match.roundNumber=2;
 oldRoundAck({200,{}});assert(late.parsed==0&&!late.m_surrenderBusy&&!late.m_pendingSurrender);
 s.m_view.match.surrenderScope="ROUND";s.m_view.match.surrenderRound=1;assert(s.gameplayConceded(1,0)&&!s.gameplayConceded(2,0));
 s.m_view.match.surrenderDeathmatch=1;assert(s.gameplayConceded(3,1)&&!s.gameplayConceded(3,2));
 s.m_view.match.surrenderScope="MATCH";assert(s.gameplayConceded(3,2));
}
`;
    const dir=mkdtempSync(join(tmpdir(),'corum-gameplay-controls-'));
    try{writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}finally{rmSync(dir,{recursive:true,force:true});}
});
