import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = p => readFileSync(join(root, p), 'utf8');
const source = read('src/RankedRuntime.cpp');
function method(signature) {
  const start = source.indexOf(signature); assert(start >= 0, signature);
  let end = source.indexOf('{', start) + 1, depth = 1;
  while (depth) { depth += (source[end] === '{') - (source[end] === '}'); ++end; }
  return source.slice(start, end);
}

test('actual Placement map skip waits for frozen End ACK, retries exact identity, retires late responses and previews Top 2 grades', () => {
  const viewer = read('tests/SpectatorLiveViewerNativeTest.mjs');
  const json = viewer.slice(viewer.indexOf('namespace matjson {'), viewer.indexOf('${readFileSync(join(root,'))
    .replace('bool isErr()const', 'bool isOk()const{return value.has_value();}\n    bool isErr()const');
  const header = read('src/RankedRuntime.hpp').replace(/^#include <Geode.*$|^#pragma once$/gm, '').replaceAll('private:', 'public:');
  const callbacks = ['bool RankedRuntime::placementMapSkipped(', 'void RankedRuntime::skipPlacementMap(',
    'void RankedRuntime::flushPlacementMapSkip()', 'void RankedRuntime::resetPlacementMapSkip()',
    'bool RankedRuntime::hasLocalAttemptInFlight() const', 'bool RankedRuntime::hasLocalOpenAttempt() const',
    'bool RankedRuntime::attemptStartWindowOpen() const', 'std::string RankedRuntime::placementDisplayGrade(',
    'double RankedRuntime::localDisplayScore('].map(method).join('\n')
    .replaceAll('std::chrono::steady_clock::now()', 'testNow()');
  const cpp = `
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>
#include "${join(root, 'src/domain/AttemptScoring.cpp')}"
${json}
namespace geode::utils::web {
struct WebResponse {matjson::Value root;int status=200;int code()const{return status;}matjson::Result<matjson::Value> json()const{return {root};}};
struct Operation {std::string url;matjson::Value body;};
struct WebRequest {matjson::Value body;void bodyJSON(matjson::Value v){body=v;}Operation post(std::string u){return {u,body};}};
struct Pending {Operation request;std::function<void(WebResponse)> callback;};std::vector<Pending> pending;
}
namespace web=geode::utils::web;
namespace geode::async {template<class T> struct TaskHolder {void cancel(){}void spawn(web::Operation r,std::function<void(T)> cb){web::pending.push_back({r,cb});}};}
namespace fmt {
template<class... T>std::string format(std::string s,T const&... values){
 auto put=[&](auto const& value){std::ostringstream out;out<<value;auto at=s.find("{}");if(at!=std::string::npos)s.replace(at,2,out.str());};
 (put(values),...);return s;
}}
${header}
auto testClock=std::chrono::steady_clock::time_point{};
auto testNow(){return testClock;}
bool refuseEnd=false;double frozenProgress=-1;int ends=0,flushes=0,parsed=0,events=0;
namespace corum::ranked {
namespace log {template<class... T>void info(T...){} }
bool successful(web::WebResponse const& r){return r.status>=200&&r.status<300;}
std::string responseError(web::WebResponse const&){return "error";}
web::WebRequest baseRequest(std::string const&){return {};}
bool RankedRuntime::isPlacement()const{return m_view.match.placement&&m_view.placement.active;}
bool RankedRuntime::isSpectating()const{return m_view.match.spectatorActive;}
bool RankedRuntime::nativeEntryStartGranted()const{return false;}
std::optional<std::int64_t> RankedRuntime::signedDeadlineMillis()const{return 1000;}
std::optional<std::int64_t> RankedRuntime::deadlineMillis()const{return 1000;}
std::string RankedRuntime::endpoint(std::string const& p)const{return p;}
std::string RankedRuntime::newEventId(std::string_view){return "skip-"+std::to_string(++events);}
void RankedRuntime::setTransientError(std::string s){m_view.error=s;}
void RankedRuntime::parsePlacementState(matjson::Value const& s){++parsed;m_view.placement.state=s["state"].asString().unwrapOr(m_view.placement.state);}
void RankedRuntime::flushAttemptEvents(){++flushes;}
bool RankedRuntime::reportAttemptEnd(int level,double progress,bool cleared,std::optional<double>,bool){
 assert(!cleared);if(refuseEnd)return false;++ends;frozenProgress=progress;
 m_pendingEnd=PendingEnd{level,progress,false,"frozen-end","","run",0,0};return true;
}
std::string RankedRuntime::currentAttemptContextKey()const{return "unused";}
double RankedRuntime::optimisticScoreForContext(std::string const&)const{return 0;}
int RankedRuntime::localDisplayClears()const{return 0;}
${callbacks}
}
matjson::Value ack(int stage,int map){matjson::Value a;a["accepted"]=true;a["state"]="MAP_RESULT";
 a["runId"]="run";a["skippedMap"]["levelId"]="123";
 a["skippedMap"]["stageIndex"]=stage;a["skippedMap"]["mapIndex"]=map;return a;}
int main(){using namespace corum::ranked;using Status=RankedRuntime::SurrenderStatus;
 RankedRuntime r;r.m_view.stage=RuntimeStage::Matched;r.m_view.match.placement=true;
 auto& p=r.m_view.placement;p.active=true;p.runId="run";p.state="MAP_PLAYING";p.currentStageIndex=1;p.currentMapIndex=1;
 r.m_view.match.state="MAP_PLAYING";r.m_view.match.currentMap=RankedMapView{};r.m_view.match.currentMap->levelId=123;
 r.m_view.match.currentMap->qualifyingPercent=50;r.m_attemptId="attempt";
 r.skipPlacementMap(999,75);assert(r.placementMapSkipStatus()==Status::Rejected&&web::pending.empty());
 refuseEnd=true;r.skipPlacementMap(123,75);assert(!r.placementMapSkipPending()&&web::pending.empty());refuseEnd=false;
 r.skipPlacementMap(123,75.125);assert(ends==1&&frozenProgress==75.125&&web::pending.empty());
 assert(r.placementMapSkipPending()&&r.placementMapSkipStatus()==Status::WaitingAttempt&&!r.attemptStartWindowOpen());
 r.skipPlacementMap(123,99);assert(ends==1&&frozenProgress==75.125); // cannot overwrite frozen progress
 r.flushPlacementMapSkip();assert(web::pending.empty());
 r.m_attemptId.clear();r.m_pendingEnd.reset();r.flushPlacementMapSkip();assert(web::pending.size()==1);
 auto first=web::pending[0].request;assert(first.url=="/api/ranked/placement/run/map/skip");
 assert(first.body["stageIndex"].asInt().unwrapOr(0)==1&&first.body["mapIndex"].asInt().unwrapOr(0)==1);
 assert(first.body["levelId"].asString().unwrapOr("")=="123");
 auto fail=web::pending[0].callback;fail({{},503});assert(r.placementMapSkipStatus()==Status::Retrying);
 r.flushPlacementMapSkip();assert(web::pending.size()==1);testClock+=std::chrono::milliseconds(500);
 r.flushPlacementMapSkip();assert(web::pending.size()==2);
 assert(web::pending[1].request.body["clientEventId"].asString().unwrapOr("")==first.body["clientEventId"].asString().unwrapOr("x"));
 auto success=web::pending[1].callback;success({ack(1,1)});assert(parsed==1&&!r.placementMapSkipPending());
 assert(r.placementMapSkipStatus()==Status::Accepted&&r.placementMapSkipped("run",1,1));
 assert(!r.placementMapSkipped("run",1,2)&&!r.placementMapSkipped("next-run",1,1));
 p.currentMapIndex=2;p.state="MAP_PLAYING";r.skipPlacementMap(123,0);assert(web::pending.size()==3);
 auto old=web::pending[2].callback;r.resetPlacementMapSkip();old({ack(1,2)});
 assert(parsed==1&&!r.placementMapSkipped("run",1,2)&&r.placementMapSkipStatus()==Status::Idle);
 r.skipPlacementMap(123,0);auto invalid=web::pending.back().callback;invalid({ack(1,1)});
 assert(r.placementMapSkipStatus()==Status::Rejected&&!r.placementMapSkipPending());
 r.skipPlacementMap(123,0);auto rejected=web::pending.back().callback;rejected({{},409});
 assert(r.placementMapSkipStatus()==Status::Rejected&&r.attemptStartWindowOpen());
 r.skipPlacementMap(123,0);auto count=web::pending.size();auto changed=web::pending.back().callback;
 changed({{},503});p.currentStageIndex=2;testClock+=std::chrono::milliseconds(500);r.flushPlacementMapSkip();
 assert(web::pending.size()==count&&!r.placementMapSkipPending());
 // Score HUD previews one live attempt plus the best completed attempt, not
 // the sum of both old Top 2 and the active attempt. Server commits remain final.
 p.currentStageIndex=1;p.currentMapIndex=2;p.currentMapScore=250;p.provisionalMapScore=250;
 p.currentMapTopAttempts={{"best",80,false,175},{"second",50,false,75}};r.m_attemptId="live";
 assert(r.localDisplayScore(75)==325&&r.localDisplayScore(-1)==250);
 assert(r.placementDisplayGrade(149)=="D"&&r.placementDisplayGrade(150)=="C");
 assert(r.placementDisplayGrade(250)=="B"&&r.placementDisplayGrade(350)=="A"&&r.placementDisplayGrade(425)=="S");
 p.mapGradeThresholds={100,200,300,400};assert(r.placementDisplayGrade(210)=="B");
 p.currentMapClears=2;assert(r.placementDisplayGrade(0)=="S");
 r.m_view.match.spectatorActive=true;assert(r.localDisplayScore(99)==250); // no viewer live evidence
}
`;
  const dir=mkdtempSync(join(tmpdir(),'corum-placement-map-skip-'));
  try {writeFileSync(join(dir,'test.cpp'),cpp);
    execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',`-I${join(root,'src')}`,join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
    execFileSync(join(dir,'test'),[],{stdio:'pipe'});
  } finally {rmSync(dir,{recursive:true,force:true});}
});

test('Placement preparation timer follows the server deadline and does not survive a menu rebuild', () => {
  const popup=read('src/RankedPopup.cpp');
  assert.match(popup,/if \(match.state == "MAP_PREPARE"\) \{[\s\S]*ranked-placement-prepare-timer/);
  assert.match(popup,/auto const remaining = runtime.deadlineSeconds\(\)/);
  assert.match(popup,/m_placementPrepareTimerLabel->setAnchorPoint\(\{1, 1\}\)/);
  const reset=popup.slice(popup.indexOf('void resetDynamicUiPointers()'),popup.indexOf('void clearUi()'));
  assert.match(reset,/m_placementPrepareTimerLabel = nullptr/);
});
