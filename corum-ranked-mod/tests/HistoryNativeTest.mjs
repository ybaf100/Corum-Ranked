import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = p => readFileSync(join(root, p), 'utf8');
function method(source, signature) {
    const start = source.indexOf(signature); assert(start >= 0, signature);
    let end = source.indexOf('{', start) + 1, depth = 1;
    while (depth) { if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end; }
    return source.slice(start, end);
}
function run(cpp) {
    const dir = mkdtempSync(join(tmpdir(), 'corum-history-'));
    try {
        writeFileSync(join(dir, 'test.cpp'), cpp);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I', join(root, 'src'), join(dir, 'test.cpp'), '-o', join(dir, 'test')], { stdio: 'pipe' });
        execFileSync(join(dir, 'test'), [], { stdio: 'pipe' });
    } catch (error) { throw new Error(error.stderr?.toString() || error.message); }
    finally { rmSync(dir, { recursive: true, force: true }); }
}

test('real history transport cancels rapid tab/Refresh reads and rejects retired or mismatched responses', () => {
    const viewer = read('tests/SpectatorLiveViewerNativeTest.mjs');
    const model = viewer.slice(viewer.indexOf('namespace matjson {'), viewer.indexOf('${readFileSync(join(root,'))
        .replace('bool isErr()const{return !value;}', 'bool isErr()const{return !value;}bool isOk()const{return bool(value);}')
        .replace('bool isObject()const{', 'bool isArray()const{return std::holds_alternative<Array>(data);}\n    bool isObject()const{');
    const runtime = read('src/RankedRuntime.cpp');
    const history = method(read('src/RankedRuntime.hpp'), 'struct HistoryMatchView') + ';';
    run(`
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include "domain/RankedMode.hpp"
${model}
namespace web {
struct WebResponse {int code=200;matjson::Value body;struct Json {matjson::Value body;matjson::Value unwrapOr(matjson::Value)const{return body;}};Json json()const{return {body};}};
struct Request {std::string url;};
struct WebRequest {Request get(std::string url)const{return {url};}};
struct Job {Request request;std::function<void(WebResponse)> callback;bool cancelled=false;};
inline std::vector<std::shared_ptr<Job>> jobs;
}
namespace geode::async {template<class T>struct TaskHolder {
 std::shared_ptr<web::Job> job;void cancel(){if(job)job->cancelled=true;}
 template<class F>void spawn(web::Request request,F fn){job=std::make_shared<web::Job>(web::Job{request,fn,false});web::jobs.push_back(job);}
};}
web::WebRequest baseRequest(std::string const&){return {};}
bool successful(web::WebResponse const&r){return r.code==200;}
std::string responseError(web::WebResponse const&){return "failure";}
namespace fmt {std::string format(char const*,char const*scope){return std::string("/api/ranked/history?scope=")+scope;}}
namespace corum::ranked {
using RoundSummaryView=int;using DeathmatchSummaryView=int;
${history}
int parseRoundSummary(matjson::Value const&){return 1;}int parseDeathmatchSummary(matjson::Value const&){return 2;}
struct RankedRuntime {
 std::string m_sessionToken="session";bool m_rankedOpen=true,m_controlBusy=true;std::uint64_t m_sessionGeneration=1,m_historyGeneration=0;
 geode::async::TaskHolder<web::WebResponse> m_historyRequest,m_historyDetailRequest;
 std::uint64_t m_historyDetailGeneration=0;std::string m_historyOrder="NEWEST",m_historyResult="ALL",m_historyDetailMatch;
 struct {bool historyDetailLoading=false;std::string historyDetailError;bool historyLoading=false;HistoryScope historyScope=HistoryScope::All;std::string historyError;
   std::uint64_t revision=0,historyRevision=0;std::vector<HistoryMatchView> history;}m_view;
 std::string endpoint(std::string url)const{return url;}void observeServerNow(matjson::Value const&){}
 void fetchHistory(RankedMode);void fetchHistoryScope(HistoryScope);
};
${method(runtime, 'static corum::ranked::HistoryMatchView parseHistoryMatch(')}
${method(runtime, 'void RankedRuntime::fetchHistory(')}
${method(runtime, 'void RankedRuntime::fetchHistoryScope(')}
}
matjson::Value response(std::string scope,std::string id="match"){
 matjson::Value entry;entry["matchId"]=id;entry["mode"]="SOLO_RANKED";entry["side"]="A";entry["winnerSide"]="A";
 entry["rankedScoreDelta"]["A"]=10;entry["profileAfter"]["A"]["visibleRankedScore"]=950;
 entry["rounds"]=matjson::Value::Array{matjson::Value::Object{}};
 entry["deathmatches"]=matjson::Value::Array{matjson::Value::Object{}};
 matjson::Value body;body["scope"]=scope;body["matches"]=matjson::Value::Array{entry};return body;
}
int main(){using namespace corum::ranked;
 RankedRuntime r;r.fetchHistoryScope(HistoryScope::All);auto all=web::jobs.back();
 assert(all->request.url=="/api/ranked/history?scope=ALL&summary=true&order=NEWEST&result=ALL"&&r.m_controlBusy&&r.m_view.historyLoading);
 r.fetchHistoryScope(HistoryScope::Survival);auto sur=web::jobs.back();assert(all->cancelled);
 r.fetchHistory(RankedMode::SoloRanked);auto solo=web::jobs.back();assert(sur->cancelled);
 all->callback({200,response("ALL","stale")});sur->callback({500,{}});
 assert(r.m_view.history.empty()&&r.m_view.historyLoading&&r.m_view.historyError.empty());
 solo->callback({200,response("SOLO_RANKED")});assert(!r.m_view.historyLoading&&r.m_view.history.size()==1);
 auto match=r.m_view.history[0];assert(match.matchId=="match"&&match.mode==RankedMode::SoloRanked);
 assert(match.ownRankedScoreDelta==10&&match.ownRankedScoreAfter==950&&match.rounds.size()==1&&match.deathmatches.size()==1);
 auto revision=r.m_view.historyRevision;r.fetchHistoryScope(HistoryScope::SoloRanked);auto refresh=web::jobs.back();
 assert(r.m_view.history.empty()&&r.m_view.historyLoading&&r.m_view.historyRevision>revision);
 r.fetchHistoryScope(HistoryScope::SoloRanked);assert(refresh->cancelled);auto current=web::jobs.back();
 refresh->callback({500,{}});assert(r.m_view.historyLoading&&r.m_view.historyError.empty());
 current->callback({200,response("SURVIVAL")});assert(!r.m_view.historyLoading&&!r.m_view.historyError.empty()&&r.m_view.history.empty());
 r.fetchHistoryScope(HistoryScope::All);auto pending=web::jobs.back();++r.m_sessionGeneration;r.m_rankedOpen=false;
 pending->callback({200,response("ALL")});assert(r.m_view.history.empty());
 auto count=web::jobs.size();r.fetchHistoryScope(HistoryScope::All);assert(web::jobs.size()==count);
 r.m_rankedOpen=true;r.m_sessionToken.clear();r.fetchHistoryScope(HistoryScope::All);assert(web::jobs.size()==count);
}
`);
});

test('real lobby callbacks choose contextual history/achievement tabs and preserve a selected history tab on retry', () => {
    const popup = read('src/RankedPopup.cpp');
    const methods = ['    void onHistory(', '    void onHistoryScope(', '    void onAchievements('].map(s => method(popup, s)).join('\n');
    run(`
#include <cassert>
#include <string>
#include <optional>
#include "domain/RankedMode.hpp"
using namespace corum::ranked;
struct CCObject{int tag=0;int getTag()const{return tag;}};
struct RankedRuntime{int reads=0,achievements=0;HistoryScope scope=HistoryScope::All;
 static RankedRuntime&get(){static RankedRuntime r;return r;}void setHistoryFilters(std::string,std::string){}void fetchHistoryScope(HistoryScope s){++reads;scope=s;}void fetchAchievements(){++achievements;}};
struct Layer{
 enum class Page{Live,HistoryList,HistoryDetail,Achievements};Page m_page=Page::Live;
 ModeLobbyTransition m_modeTransition;HistoryScope m_historyScope=HistoryScope::All;std::size_t m_historyIndex=10,m_achievementPage=10;
 std::string m_achievementScope,m_titleSelectorAchievement,m_historyOrder="NEWEST",m_historyResult="ALL";std::optional<float> m_historyListOffset;
 ${methods}
};
int main(){
 for(int selection:{-1,0,1}){Layer l;if(selection>=0){assert(l.m_modeTransition.select(static_cast<RankedMode>(selection)));l.m_modeTransition.finish();}
  l.onHistory(nullptr);auto expected=selection<0?HistoryScope::All:selection==0?HistoryScope::Survival:HistoryScope::SoloRanked;
  assert(l.m_page==Layer::Page::HistoryList&&RankedRuntime::get().scope==expected);
  CCObject tab{selection==0?1:0};l.onHistoryScope(&tab);auto switched=RankedRuntime::get().scope;
  assert(switched!=expected&&l.m_historyIndex==0);l.onHistory(nullptr);assert(RankedRuntime::get().scope==switched);
  l.onAchievements(nullptr);assert(l.m_achievementScope==(selection<0?"SHARED":selection==0?"SURVIVAL":"SOLO_RANKED")&&l.m_achievementPage==0);
 }
 Layer l;assert(l.m_modeTransition.select(RankedMode::SoloRanked));int reads=RankedRuntime::get().reads,achievements=RankedRuntime::get().achievements;
 l.onHistory(nullptr);l.onAchievements(nullptr);CCObject tab{1};l.onHistoryScope(&tab);
 assert(RankedRuntime::get().reads==reads&&RankedRuntime::get().achievements==achievements&&l.m_page==Layer::Page::Live);
}
`);
});
