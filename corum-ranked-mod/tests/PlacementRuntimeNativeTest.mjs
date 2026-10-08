import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = p => readFileSync(join(root, p), 'utf8');
const source = read('src/RankedRuntime.cpp');
function method(signature) {
    const start = source.indexOf(signature); assert(start >= 0, signature);
    const open = source.indexOf('{', start); let depth = 1, end = open + 1;
    while (depth) { if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end; }
    return source.slice(start, end);
}

test('actual Placement Start/End and state decoder keep the final Attempt and reject post-ACK stale snapshots', () => {
    const viewer = read('tests/SpectatorLiveViewerNativeTest.mjs');
    const json = viewer.slice(viewer.indexOf('namespace matjson {'), viewer.indexOf('${readFileSync(join(root,'))
        .replace('bool isErr()const', 'bool isOk()const{return value.has_value();}\n    bool isErr()const');
    // Compile production view/transport fields and callback bodies. HTTP and
    // server clock are deterministic doubles; GD ABI/device Clear is covered
    // separately and cannot be proved by this host fixture.
    const header = read('src/RankedRuntime.hpp').replace(/^#include <Geode.*$|^#pragma once$/gm, '').replaceAll('private:', 'public:');
    const callbacks = ['void RankedRuntime::parsePlacementState(', 'void RankedRuntime::sendAttemptStart()',
        'void RankedRuntime::sendAttemptEnd()', 'bool RankedRuntime::attemptStartWindowOpen() const',
        'bool RankedRuntime::hasLocalAttemptInFlight() const', 'bool RankedRuntime::hasLocalOpenAttempt() const']
        .map(method).join('\n');
    const cpp = `
#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>
${json}
namespace geode::utils::web {
struct WebResponse {matjson::Value root;int status=200;int code()const{return status;}matjson::Result<matjson::Value> json()const{return {root};}};
struct Operation {std::string url;matjson::Value body;};
struct WebRequest {matjson::Value body;void bodyJSON(matjson::Value v){body=v;}Operation post(std::string url){return {url,body};}};
struct Pending {Operation request;std::function<void(WebResponse)> callback;};
std::vector<Pending> pending;
}
namespace web=geode::utils::web;
namespace geode::async {template<class T> struct TaskHolder {
void cancel(){} void spawn(web::Operation r,std::function<void(T)> c){web::pending.push_back({r,c});}
};}
namespace fmt {template<class... T> std::string format(std::string s,T const&...){return s;}}
${header}
namespace corum::ranked {
namespace log {template<class... T> void debug(T...){}template<class... T>void info(T...){}template<class... T>void warn(T...){}template<class... T>void error(T...){} }
struct TransitionTrace {static TransitionTrace& get(){static TransitionTrace t;return t;}void begin(std::string const&,std::string const&){} };
struct SpectatorLiveRuntime {static SpectatorLiveRuntime& get(){static SpectatorLiveRuntime s;return s;}void tickTerminal(){} };
bool successful(web::WebResponse const& r){return r.status>=200&&r.status<300;}
bool permanentClientError(int s){return s>=400&&s<500;}
std::string responseError(web::WebResponse const&){return "error";}
web::WebRequest baseRequest(std::string const&,std::string const&){return {};}
std::optional<RankedMapView> parseMap(matjson::Value const& v){if(!v.isObject())return {};RankedMapView m;m.levelId=int(v["levelId"].asInt().unwrapOr(0));return m;}
int placementGradeRank(std::string const&){return 0;}
AchievementUnlockNoticeView parseAchievementNotice(matjson::Value const&){return {};}
bool RankedRuntime::isPlacement()const{return m_view.match.placement;}
bool RankedRuntime::nativeEntryStartGranted()const{return false;}
void RankedRuntime::observeServerNow(matjson::Value const&){}
void RankedRuntime::setTransientError(std::string s){m_view.error=s;}
std::string RankedRuntime::endpoint(std::string const& p)const{return p;}
std::string RankedRuntime::currentAttemptContextKey()const{return m_view.match.matchId;}
std::optional<std::int64_t> RankedRuntime::signedDeadlineMillis()const{return -10000;}
std::optional<std::int64_t> RankedRuntime::deadlineMillis()const{return 0;}
void RankedRuntime::applyAttemptSnapshot(matjson::Value const&){}
void RankedRuntime::promoteQueuedAttempt(){}
void RankedRuntime::cleanupAttemptTransportIfIdle(){}
void RankedRuntime::flushAttemptEvents(){}
void RankedRuntime::flushProgressTelemetry(){}
${callbacks}
}
matjson::Value state(std::string id,std::string phase="MAP_PLAYING",int version=1){
matjson::Value s;s["runId"]="run";s["state"]=phase;s["stateVersion"]=version;s["currentMap"]["levelId"]=123;
if(!id.empty()){s["activeAttempt"]["attemptId"]=id;}return s;}
int main(){
using namespace corum::ranked;RankedRuntime r;r.parsePlacementState(state(""));
r.m_pendingStart=RankedRuntime::PendingStart{123,"start-A","","run"};r.sendAttemptStart();
assert(web::pending.size()==1);matjson::Value start;start["accepted"]=true;start["attemptId"]="A";
web::pending[0].callback({start});assert(r.m_attemptId=="A"&&r.hasLocalOpenAttempt());
// Both timers zero. The same acknowledged A survives settling; no new Start.
r.parsePlacementState(state("A","MAP_SETTLING",2));assert(r.m_attemptId=="A"&&r.hasLocalOpenAttempt());
assert(!r.attemptStartWindowOpen());
auto lateState=state("A","MAP_SETTLING",2); // GET starts before End ACK
r.m_pendingEnd=RankedRuntime::PendingEnd{123,100,true,"end-A","","run",0,1};r.sendAttemptEnd();
assert(web::pending.size()==2);assert(web::pending[1].request.body["cleared"].asBool().unwrapOr(false));
matjson::Value end;end["accepted"]=true;end["mapScore"]=250.0;end["clearCount"]=1;
web::pending[1].callback({end});assert(r.m_attemptId.empty()&&!r.m_pendingEnd);
r.sendAttemptEnd();assert(web::pending.size()==2); // End exactly once
r.parsePlacementState(lateState);assert(r.m_attemptId.empty()&&r.m_view.placement.activeAttemptId.empty());
assert(r.m_view.placement.currentMapScore==250);
r.parsePlacementState(state("","MAP_RESULT",3));assert(r.m_view.match.state=="MAP_RESULT");
r.parsePlacementState(lateState);assert(r.m_view.match.state=="MAP_RESULT"&&r.m_attemptId.empty());
// Same-version active identity B is authoritative; no broad state suppression.
r.parsePlacementState(state("B","MAP_PLAYING",3));assert(r.m_attemptId=="B");
r.m_pendingEnd=RankedRuntime::PendingEnd{123,50,false,"end-B","","run",0,0};r.sendAttemptEnd();
end["accepted"]=false;end["reason"]="ATTEMPT_ALREADY_ENDED";web::pending.back().callback({end});
r.parsePlacementState(state("B","MAP_PLAYING",3));assert(r.m_attemptId.empty());
r.parsePlacementState(state("C","MAP_PLAYING",3));assert(r.m_attemptId=="C");
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-placement-runtime-'));
    try {
        const file = join(dir, 'placement.cpp'), binary = join(dir, 'placement');
        writeFileSync(file, cpp);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Werror', '-I', join(root, 'src'), file, '-o', binary], { stdio: 'pipe' });
        execFileSync(binary, [], { stdio: 'pipe' });
    } finally { rmSync(dir, { recursive: true, force: true }); }
});
