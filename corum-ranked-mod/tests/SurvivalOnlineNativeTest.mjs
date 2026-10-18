// Execute production HTTP boundary methods with a controllable native transport.
// The platform builds separately compile the complete Runtime and queue scenes.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const source=readFileSync(join(root,'src/SurvivalRuntime.cpp'),'utf8');
function method(name){const start=source.indexOf(name);assert(start>=0,name);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');end++;}return source.slice(start,end).replaceAll('std::chrono::steady_clock::now()','testNow()');}
const viewer=readFileSync(join(root,'tests/SpectatorLiveViewerNativeTest.mjs'),'utf8');
const json=viewer.slice(viewer.indexOf('namespace matjson {'),viewer.indexOf('${readFileSync(join(root,')).replace('bool contains(', 'bool isString()const{return std::holds_alternative<std::string>(data);}\n    bool contains(');
for(const debug of [false,true])test(`public Survival queue transport rejects old sessions/epochs, recovers ambiguous writes and selects isolated paths (${debug?'Debug':'Release'})`,()=>{
 const directory=mkdtempSync(join(tmpdir(),'corum-online-native-'));
 try{
 const cpp=`
#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include "${join(root,'src/domain/ServerClock.hpp')}"
${json}
auto wall=std::chrono::steady_clock::time_point{};
auto testNow(){return wall;}
std::int64_t localNow(){return std::chrono::duration_cast<std::chrono::milliseconds>(wall.time_since_epoch()).count();}
namespace web {
struct WebResponse{int status=200;matjson::Value root;int code()const{return status;}matjson::Result<matjson::Value> json()const{return {root};}};
struct Operation{std::string path,session;matjson::Value body;};
struct WebRequest{std::string session;matjson::Value body;void bodyJSON(matjson::Value v){body=v;}Operation post(std::string p){return {p,session,body};}};
struct Pending{Operation operation;std::function<void(WebResponse)> done;};
std::vector<Pending> pending;
}
struct Request{int cancelled=0;void cancel(){++cancelled;}void spawn(web::Operation o,std::function<void(web::WebResponse)> fn){web::pending.push_back({o,fn});}};
web::WebRequest request(std::string session){return {session,{}};}
std::string onlineEndpoint(std::string p){return "/api/ranked/survival/online"+p;}
std::string queueEndpoint(std::string p){return "/api/ranked/survival/queue"+p;}
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
std::string debugEndpoint(std::string p){return "/api/ranked/survival/debug"+p;}
#endif
namespace utils::random{std::string generateUUID(){return "nonce";}}
namespace corum::ranked {
struct RankedRuntime{std::string token="session-a";bool allowed=true,updateRequired=false;
 static RankedRuntime& get(){static RankedRuntime r;return r;}
 std::string spectatorLiveSessionToken()const{return token;}
 bool handleSurvivalAdmissionError(web::WebResponse const&){return updateRequired;}
 bool prepareSurvivalAdmission(matjson::Value& body){body["installedMods"]="captured";return allowed;}
};
struct SurvivalRuntime{
 struct View{std::string id;bool debug=false,online=false;}m_view;
 struct Queue{std::string status="LEFT",matchId,error,waitingSince;int players=0,minimumPlayers=8,maximumPlayers=16;bool enabled=true;}m_queue;
 ServerClock m_clock;bool m_creating=false,m_queuePollBusy=false,m_pollBusy=false,m_recoverBusy=false,m_needRecovery=false;
 std::uint64_t m_generation=1,m_queueGeneration=1;std::string m_session="session-a";
 std::chrono::steady_clock::time_point m_recoverRetryAt{},m_nextQueuePoll{},m_nextPoll{};
 Request m_queueRequest,m_controlRequest,m_recoverRequest,m_pollRequest;std::vector<std::string> assigned;
 bool active()const{return !m_view.id.empty();}bool queueing()const{return m_queue.status=="QUEUED"||m_queue.status=="JOINING";}
 void loadOnlineMatch(std::string const& id){assigned.push_back(id);}
 bool current(std::uint64_t,std::string const&)const;
 std::string matchEndpoint(std::string const&)const;
 int queueWaitingSeconds()const;
 void observeQueue(matjson::Value const&);
 void pollQueue();void queueAction(std::string const&,matjson::Value);
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
 void fillBots(std::string,int=-1);
#endif
};
${['bool SurvivalRuntime::current(', 'std::string SurvivalRuntime::matchEndpoint(', 'int SurvivalRuntime::queueWaitingSeconds()', 'void SurvivalRuntime::observeQueue(', 'void SurvivalRuntime::pollQueue()', 'void SurvivalRuntime::queueAction('].map(method).join('\n')}
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
${method('void SurvivalRuntime::fillBots(')}
#endif
}
int main(){using namespace corum::ranked;SurvivalRuntime r;
 r.m_queue.status="JOINING";r.queueAction("/join",{});r.queueAction("/join",{});assert(web::pending.size()==1&&r.m_creating);
 auto join=web::pending[0];assert(join.operation.path==queueEndpoint("/join")&&join.operation.session=="session-a");
 matjson::Value queued;queued["status"]="QUEUED";queued["players"]=5;queued["enabled"]=true;queued["waitingSince"]="2026-10-13T00:00:00Z";queued["serverNow"]="2026-10-13T00:00:00Z";
 join.done({201,queued});assert(!r.m_creating&&r.m_queue.players==5&&r.queueing());
 wall+=std::chrono::seconds(24);assert(r.queueWaitingSeconds()==24);
 r.pollQueue();auto oldPoll=web::pending.back();assert(oldPoll.operation.path==queueEndpoint("/heartbeat"));
 r.queueAction("/leave",{});auto leave=web::pending.back();assert(r.m_queueRequest.cancelled==2);
 matjson::Value found;found["status"]="MATCHED";found["matchId"]="shared-game";
 oldPoll.done({201,found});assert(r.assigned.empty()&&r.m_creating);
 matjson::Value left;left["status"]="LEFT";leave.done({201,left});assert(!r.queueing());
 r.m_queue.status="JOINING";r.queueAction("/join",{});web::pending.back().done({503,{}});
 assert(r.m_needRecovery&&!r.m_creating&&r.m_queue.status=="LEFT"&&!r.m_queue.error.empty());
 r.queueAction("/join",{});auto retired=web::pending.back();RankedRuntime::get().token="replacement";
 retired.done({201,found});assert(r.assigned.empty());RankedRuntime::get().token="session-a";r.m_creating=false;
 r.m_pollBusy=true;r.m_nextPoll=wall+std::chrono::seconds(30);
  const auto previousPollCancels=r.m_pollRequest.cancelled;
  r.observeQueue(found);assert(r.assigned.size()==1&&r.assigned[0]=="shared-game");
  assert(!r.m_pollBusy&&r.m_pollRequest.cancelled==previousPollCancels+1);
 r.m_view.online=true;r.m_view.debug=true;assert(r.matchEndpoint("/matches/m")==onlineEndpoint("/matches/m"));
 r.m_view.online=false;
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
 assert(r.matchEndpoint("/matches/m")==debugEndpoint("/matches/m"));
 r.m_view.debug=false;r.m_queue.status="QUEUED";r.fillBots("fixture-password");auto fill=web::pending.back();
 assert(fill.operation.path==queueEndpoint("/fill-bots"));assert(fill.operation.body["password"].asString().unwrapOr("")=="fixture-password");
 assert(fill.operation.body.contains("installedMods")&&!fill.operation.body.contains("bots"));
 fill.done({201,found});assert(r.assigned.size()==2);
#else
 assert(r.matchEndpoint("/matches/m")==onlineEndpoint("/matches/m"));
#endif
}
`;
 writeFileSync(join(directory,'test.cpp'),cpp);
 execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',...(debug?['-DCORUM_RANKED_DEBUG_BOT_MATCH=1']:[]),join(directory,'test.cpp'),join(root,'src/domain/ServerClock.cpp'),'-o',join(directory,'test')],{stdio:'pipe'});
 execFileSync(join(directory,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(directory,{recursive:true,force:true});}
});
