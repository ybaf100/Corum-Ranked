import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = p => readFileSync(join(root, p), 'utf8');
const source = read('src/SpectatorLiveRuntime.cpp');
function method(signature) {
    const start = source.indexOf(signature); assert(start >= 0);
    const open = source.indexOf('{', start); let depth = 1, end = open + 1;
    while (depth) { if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end; }
    return source.slice(start, end).replaceAll('std::chrono::steady_clock::now()', 'testClockNow()');
}
test('actual HTTP callback chooses post-decode adaptive gaps, keeps one request and bounds error backoff', () => {
    const viewer = read('tests/SpectatorLiveViewerNativeTest.mjs');
    const json = viewer.slice(viewer.indexOf('namespace matjson {'), viewer.indexOf('${readFileSync(join(root,'));
    const header = read('src/SpectatorLiveRuntime.hpp');
    const fields = header.slice(header.indexOf('    std::string m_viewMatchId;'), header.indexOf('    geode::async::TaskHolder'));
    const cpp = `
#include "${join(root, 'src/domain/SpectatorInterpolationBuffer.hpp')}"
#include "${join(root, 'src/domain/SpectatorViewerPollPolicy.hpp')}"
#include "${join(root, 'src/domain/SpectatorRestoreTimeline.hpp')}"
#include <cassert>
#include <set>
#include <chrono>
#include <functional>
#include <map>
#include <variant>
#include <vector>
${json}
auto pollTestClock=std::chrono::steady_clock::time_point(std::chrono::seconds(10));
auto testClockNow(){return pollTestClock;}
namespace web {
struct WebResponse {matjson::Value root;int status=200;int code()const{return status;}matjson::Result<matjson::Value> json()const{return {root};}};
struct Operation {std::string url;};struct WebRequest {Operation get(std::string url){return {url};}};
struct Pending {Operation request;std::function<void(WebResponse)> callback;};std::vector<Pending> pending;
}
namespace geode::async {template<class T>struct TaskHolder {
void spawn(web::Operation r,std::function<void(T)> c){web::pending.push_back({r,c});}
};}
namespace fmt {template<class...T>std::string format(std::string s,T const&...){return s;}}
namespace corum::ranked {
constexpr int kProtocolVersion=2;
enum class SpectatorLiveStatus {Idle,Waiting,Live,Fallback};
enum class RuntimeStage{Matched,Ready};
struct RankedRuntime {
bool authorized=true,ackPending=false;bool isSpectating()const{return authorized;}bool hasLocalAttemptInFlight()const{return ackPending;}
bool isPlacement()const{return false;}
struct Match{std::string matchId="match";bool spectatorLiveEnabled=true;};struct View{Match match;RuntimeStage stage=RuntimeStage::Matched;}data;
static RankedRuntime& get(){static RankedRuntime r;return r;}auto const& view()const{return data;}
std::string spectatorLiveSessionToken()const{return "session";}std::string spectatorLiveMatchToken()const{return "token";}
};
std::string viewerContextKey(RankedRuntime::Match const&){return "context";}
web::WebRequest spectatorRequest(std::string const&,std::string const&){return {};}
bool successful(web::WebResponse const& r){return r.status>=200&&r.status<300;}
${header.slice(header.indexOf('struct SurvivalSpectatorTransportContext'),header.indexOf('class SpectatorLiveRuntime final'))}
struct SpectatorLiveRuntime {std::optional<SurvivalSpectatorTransportContext> m_external;
${fields}
geode::async::TaskHolder<web::WebResponse> m_viewRequest;
void pollViewer();void resetViewer(){m_viewBusy=false;++m_viewRequestGeneration;}
std::string endpoint(std::string const& p)const{return p;}
void setFallback(std::string const&){m_status=SpectatorLiveStatus::Fallback;m_sampleBuffer.clear();}
void applyViewerResponse(matjson::Value const& root){
 m_sampleBuffer.reset();m_status=root["fallback"].asBool().unwrapOr(false)?SpectatorLiveStatus::Fallback:SpectatorLiveStatus::Live;
 int head=int(root["headroom"].asInt().unwrapOr(400));
 m_sampleBuffer.noteResponse(0,110,80,head/50+1);
 for(int i=0;i<=head/50;++i){SpectatorGhostPose p;p.x=i;assert(m_sampleBuffer.push({i,i*50.0,p},false,0));}
 if(root["starve"].asBool().unwrapOr(false))m_sampleBuffer.render(2000);
}
};
${method('void SpectatorLiveRuntime::pollViewer()')}
}
int main(){
using namespace corum::ranked;SpectatorLiveRuntime live;live.m_viewMatchId="match";live.m_viewContextKey="context";
auto ack=[&](int head,bool starve=false,int status=200){
 live.pollViewer();auto size=web::pending.size();live.pollViewer();assert(web::pending.size()==size&&live.m_viewBusy);
 pollTestClock+=std::chrono::milliseconds(110);matjson::Value root;root["headroom"]=head;root["starve"]=starve;
 web::pending.back().callback({root,status});assert(!live.m_viewBusy);
 auto gap=std::chrono::duration_cast<std::chrono::milliseconds>(live.m_nextViewerPollAt-testClockNow()).count();
 pollTestClock=live.m_nextViewerPollAt;return gap;
};
assert(ack(400)==40&&live.m_lastViewerRttMs==110); // arrival 150ms vs old 190ms
assert(ack(150)==20); // arrival 130ms; fewer samples per response
assert(ack(0,true)==5); // arrival 115ms; starvation refill
assert(ack(0,false,500)==150);assert(ack(0,false,500)==300);assert(ack(0,false,500)==600);
assert(ack(0,false,500)==1000);assert(ack(0,false,500)==1000);
assert(ack(400)==40&&live.m_consecutiveViewerErrors==0);
assert(spectatorViewerPollGapMs(false,false,true,false,0,100,110,0)==100);
assert(spectatorViewerPollGapMs(true,false,true,true,0,100,110,0)==100);
assert(spectatorViewerPollGapMs(true,true,true,false,0,100,110,0)==150);
// Old response callback cannot clear a new request's busy ownership.
live.pollViewer();auto old=web::pending.back().callback;++live.m_viewRequestGeneration;
old({{},200});assert(live.m_viewBusy);
// A same-context response arriving after authority loss cannot revive the stream.
live.m_viewBusy=false;live.pollViewer();RankedRuntime::get().authorized=false;auto active=web::pending.back().callback;auto generation=live.m_viewRequestGeneration;active({{},200});assert(live.m_viewRequestGeneration>generation&&!live.m_viewBusy);
RankedRuntime::get().authorized=true;live.pollViewer();RankedRuntime::get().ackPending=true;generation=live.m_viewRequestGeneration;web::pending.back().callback({{},200});assert(live.m_viewRequestGeneration>generation);
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-viewer-poll-'));
    try {
        const file = join(dir, 'poll.cpp'), binary = join(dir, 'poll');writeFileSync(file, cpp);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Werror', file, '-o', binary], { stdio: 'pipe' });
        execFileSync(binary, [], { stdio: 'pipe' });
    } finally { rmSync(dir, { recursive: true, force: true }); }
});
