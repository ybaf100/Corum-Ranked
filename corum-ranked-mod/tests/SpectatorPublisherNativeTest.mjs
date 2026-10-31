import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { execFileSync } from 'node:child_process';
const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = p => readFileSync(join(root, p), 'utf8');
const source = read('src/SpectatorLiveRuntime.cpp');
const method = name => {
    const start = source.indexOf(name); assert(start >= 0);
    const open = source.indexOf('{', start); let depth = 1, end = open + 1;
    while (depth) { if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end; }
    return source.slice(start, end);
};
for(const actualJson of [false,true])test('actual publisher ordered ACK/terminal transport, '+(actualJson?'real Geode JSON':'JSON boundary double'), () => {
    const viewer = read('tests/SpectatorLiveViewerNativeTest.mjs');
    const model = viewer.slice(viewer.indexOf('namespace matjson {'), viewer.indexOf('${readFileSync(join(root,'))

        // Exercise the distinct JSON/chrono 64-bit integer types seen by the
        // real iOS SDK; an untyped std::max used to compile only on the host.
        .replace('Result<std::int64_t> asInt()const{', 'Result<long long> asInt()const{');
    const rankedHeader = read('src/RankedRuntime.hpp');
    const begin = rankedHeader.indexOf('[[nodiscard]] std::string const& spectatorLiveAttemptId()');
    const getter = rankedHeader.slice(begin, rankedHeader.indexOf('\n    }', begin) + 6);
    const header = read('src/SpectatorLiveRuntime.hpp');
    const fields = header.slice(header.indexOf('    std::string m_publishMatchId;'), header.indexOf('    std::string m_viewMatchId;'));
    const methods = ['void SpectatorLiveRuntime::resetPublisher(', 'void SpectatorLiveRuntime::startPublisherGeneration(',
        'void SpectatorLiveRuntime::publisherPaused(', 'std::int64_t SpectatorLiveRuntime::publisherElapsedMs()',
        'bool SpectatorLiveRuntime::tickPublisher(', 'void SpectatorLiveRuntime::publish(',
        'void SpectatorLiveRuntime::publishTerminal(', 'void SpectatorLiveRuntime::tickTerminal('].map(method).join('\n')
        .replaceAll('std::chrono::steady_clock::now()', 'testClockNow()');
    const cpp = `
#include "${join(root, 'src/domain/SpectatorRemotePolicy.hpp')}"
#include "${join(root, 'src/domain/SpectatorPublishQueue.hpp')}"
#include <algorithm>
#include <cassert>
#include <set>
#include <chrono>
#include <map>
#include <variant>
#include <vector>
#include <functional>
${actualJson?'#include <matjson.hpp>':model}
${read('src/spectator/SpectatorWorldEventsCodec.hpp').replace(/^#include.*$|^#pragma once$/gm, '')}
${read('src/spectator/SpectatorPoseCodec.hpp').replace(/^#include.*$|^#pragma once$/gm, '')}
namespace web {
struct WebResponse {int status=200;matjson::Value data={};int code()const{return status;}struct Json{matjson::Value v;matjson::Value unwrapOr(matjson::Value)const{return v;}};Json json()const{return {data};}};
struct Operation {std::string url,session,token;matjson::Value body;};
struct WebRequest {
    std::string session,token;matjson::Value body;
    void bodyJSON(matjson::Value value){body=std::move(value);}
    Operation post(std::string url){return {url,session,token,body};}
};
struct Pending {Operation request;std::function<void(WebResponse)> callback;};
std::vector<Pending> pending;
}
namespace geode::async {template<class T> struct TaskHolder {
    void spawn(web::Operation request,std::function<void(T)> callback){web::pending.push_back({request,callback});}
};}
namespace corum::ranked {
auto publishTestClock=std::chrono::steady_clock::time_point(std::chrono::seconds(10));
auto testClockNow(){return publishTestClock;}
namespace log {template<class... T> void debug(T...){} template<class... T> void info(T...){} template<class... T> void warn(T...){} }
enum class RuntimeStage {Ready,Matched};
struct RankedRuntime {
    struct Map{int levelId=123;};struct Match {std::optional<Map>currentMap=Map{};std::string matchId="match",state="ROUND_PLAYING";int roundNumber=1,deathmatchSequence=0;bool spectatorLiveEnabled=true;};
    struct View {RuntimeStage stage=RuntimeStage::Matched;Match match;} state;
    bool placement=false,spectating=false,m_pendingStart=false;std::vector<int> m_attemptBacklog;std::string m_attemptId="attempt-1";
    static RankedRuntime& get(){static RankedRuntime r;return r;}
    View const& view()const{return state;}
    bool isPlacement()const{return placement;} bool isSpectating()const{return spectating;}
    ${getter}
    std::string spectatorLiveSessionToken()const{return "session";}
    std::string spectatorLiveMatchToken()const{return "match-token";}
};
${header.slice(header.indexOf('struct SurvivalSpectatorTransportContext'),header.indexOf('class SpectatorLiveRuntime final'))}
struct SpectatorLiveRuntime {
    std::optional<SurvivalSpectatorTransportContext> m_external;
    ${header.match(/    std::string publishPath\(\)const\{[\s\S]*?\}/)[0]}

${fields}
    geode::async::TaskHolder<web::WebResponse> m_publishRequest,m_terminalRequest;
    void resetPublisher();void startPublisherGeneration(std::string const&,std::string const&);
    void publisherPaused(bool);
    bool tickPublisher(SpectatorGhostPose const&,bool);void publish(SpectatorGhostPose const&);
    void publishTerminal(SpectatorGhostPose const&,bool);void tickTerminal();
    std::string endpoint(std::string const& path)const{return path;}
};
constexpr int kProtocolVersion=2;
constexpr auto kPublishInterval=std::chrono::milliseconds(50),kPacketTimeout=std::chrono::milliseconds(1500);
bool finitePose(SpectatorGhostPose const& pose){return finiteSpectatorPose(pose);}
bool successful(web::WebResponse const& r){return r.code()>=200&&r.code()<300;}
web::WebRequest spectatorRequest(std::string session,std::string token){return {session,token,{}};}
${methods}
}
int main(){
    using namespace corum::ranked;auto& ranked=RankedRuntime::get();SpectatorLiveRuntime live;
    SpectatorGhostPose pose{100,400,90,false,false};pose.mode=SpectatorGameMode::Ship;pose.mini=true;pose.upsideDown=true;
    pose.camera=SpectatorCameraOrigin{50,75};
    ranked.placement=true;live.tickPublisher(pose,false);assert(web::pending.empty());ranked.placement=false;
    ranked.spectating=true;live.tickPublisher(pose,false);assert(web::pending.empty());ranked.spectating=false;
    live.tickPublisher(pose,true);assert(web::pending.empty());
    ranked.state.match.spectatorLiveEnabled=false;live.tickPublisher(pose,false);assert(web::pending.empty());ranked.state.match.spectatorLiveEnabled=true;
    ranked.m_pendingStart=true;live.tickPublisher(pose,false);assert(web::pending.empty());ranked.m_pendingStart=false;
    ranked.m_attemptBacklog.push_back(1);live.tickPublisher(pose,false);assert(web::pending.empty());ranked.m_attemptBacklog.clear();
    assert(live.tickPublisher(pose,false));assert(web::pending.size()==1);
    auto first=web::pending[0].request.body["packets"].asArray().unwrap();assert(first.size()==1);
    auto decoded=decodeSpectatorPose(first[0]["keyframe"]);
    assert(decoded&&decoded->mode==SpectatorGameMode::Ship&&decoded->mini&&decoded->upsideDown&&decoded->camera->x==50);
    assert(!live.tickPublisher(pose,false));
    publishTestClock+=std::chrono::milliseconds(50);pose.x+=15;
    assert(live.tickPublisher(pose,false));assert(web::pending.size()==1&&live.m_publishQueue.size()==2);
    publishTestClock+=std::chrono::milliseconds(10);auto from=pose;
    pose.mode=SpectatorGameMode::Spider;pose.upsideDown=false;pose.y=40;
    pose.discontinuity=SpectatorSnapEvidence{SpectatorDiscontinuity::SpiderSnap,from.x,from.y,pose.x,pose.y};
    assert(live.tickPublisher(pose,false));assert(web::pending.size()==1&&live.m_publishQueue.size()==3);
    pose.discontinuity.reset();pose.dead=true;live.publishTerminal(pose,false);
    assert(web::pending.size()==1&&live.m_terminalBody["sequence"].asInt().unwrapOr(-1)==3);
    web::pending[0].callback({200});live.tickTerminal();assert(web::pending.size()==2);
    auto batch=web::pending[1].request.body["packets"].asArray().unwrap();assert(batch.size()==2);
    assert(batch[0]["elapsedMs"].asInt().unwrapOr(-1)==50&&batch[1]["elapsedMs"].asInt().unwrapOr(-1)==60);
    assert(batch[1]["keyframe"]["discontinuity"]["type"].asString().unwrapOr("")=="SPIDER_SNAP");
    web::pending[1].callback({200});live.tickTerminal();assert(web::pending.size()==3);
    assert(web::pending[2].request.body["sequence"].asInt().unwrapOr(-1)==3);
    assert(web::pending[2].request.body["elapsedMs"].asInt().unwrapOr(-1)==60);
    assert(web::pending[2].request.body["keyframe"]["dead"].asBool().unwrapOr(false));
    ranked.m_attemptId.clear();ranked.spectating=true;web::pending[2].callback({500});
    live.m_nextTerminalAt={};live.tickTerminal();assert(web::pending.size()==4);
    assert(web::pending[3].request.token=="match-token");web::pending[3].callback({200});assert(!live.m_terminalPending);
    live.publishTerminal(pose,false);assert(web::pending.size()==4);
    // An early terminal still waits for a real server Start identity.
    ranked.spectating=false;SpectatorLiveRuntime early;early.publishTerminal(pose,false);assert(early.m_deferredTerminalPose);
    ranked.m_attemptId="server-issued-early-attempt";early.tickTerminal();assert(web::pending.size()==5);
    assert(web::pending[4].request.body["sequence"].asInt().unwrapOr(-1)==0);
    web::pending[4].callback({200});
    // startsAt is authoritative even when the next HTTP state projection is
    // still DEATHMATCH_ENTERING. First poses and terminals carry the exact DM
    // sequence; roundNumber retains the server's currentRound wire identity.
    ranked={};web::pending.clear();SpectatorLiveRuntime entering;
    ranked.state.match.state="DEATHMATCH_ENTERING";
    ranked.state.match.roundNumber=3;ranked.state.match.deathmatchSequence=2;
    pose={100,40,0,false,false};assert(entering.tickPublisher(pose,false));
    auto dmPacket=web::pending.back().request.body["packets"].asArray().unwrap().front();
    assert(dmPacket["roundNumber"].asInt().unwrapOr(-1)==3);
    assert(dmPacket["deathmatchSequence"].asInt().unwrapOr(-1)==2);
    auto dmGeneration=entering.m_publishGeneration;web::pending.back().callback({200});
    pose.dead=true;entering.publishTerminal(pose,false);
    assert(web::pending.size()==2);
    assert(web::pending.back().request.body["deathmatchSequence"].asInt().unwrapOr(-1)==2);
    assert(web::pending.back().request.body["roundNumber"].asInt().unwrapOr(-1)==3);
    assert(web::pending.back().request.body["keyframe"]["dead"].asBool().unwrapOr(false));
    ranked.state.match.state="DEATHMATCH_PLAYING";web::pending.back().callback({200});
    assert(!entering.tickPublisher(pose,false)&&entering.m_publishGeneration==dmGeneration);
    entering.publishTerminal(pose,false);assert(web::pending.size()==2);
    // A new real Attempt uses the same context across ENTERING -> PLAYING and
    // does not reset the publisher generation merely because state caught up.
    ranked.m_attemptId="deathmatch-attempt-2";ranked.state.match.state="DEATHMATCH_ENTERING";
    pose.dead=false;assert(entering.tickPublisher(pose,false));
    dmGeneration=entering.m_publishGeneration;web::pending.back().callback({200});
    ranked.state.match.state="DEATHMATCH_PLAYING";publishTestClock+=std::chrono::milliseconds(50);
    assert(entering.tickPublisher(pose,false)&&entering.m_publishGeneration==dmGeneration);
    assert(web::pending.back().request.body["packets"].asArray().unwrap().front()["deathmatchSequence"].asInt().unwrapOr(-1)==2);
    web::pending.back().callback({200});
    // Early Clear before Start ACK is deferred rather than losing DM identity.
    ranked={};web::pending.clear();SpectatorLiveRuntime dmEarly;
    ranked.state.match.state="DEATHMATCH_ENTERING";ranked.state.match.roundNumber=0;
    ranked.state.match.deathmatchSequence=3;ranked.m_attemptId.clear();pose.finished=true;
    dmEarly.publishTerminal(pose,false);assert(dmEarly.m_deferredTerminalPose&&web::pending.empty());
    ranked.state.match.state="DEATHMATCH_PLAYING";ranked.m_attemptId="deathmatch-clear-attempt";
    dmEarly.tickTerminal();assert(web::pending.size()==1);
    assert(web::pending.back().request.body["roundNumber"].asInt().unwrapOr(-1)==0);
    assert(web::pending.back().request.body["deathmatchSequence"].asInt().unwrapOr(-1)==3);
    assert(web::pending.back().request.body["keyframe"]["finished"].asBool().unwrapOr(false));
    web::pending.back().callback({200});
    // A 50ms capture clock continues at RTT110; serialized HTTP sends 2-3
    // packets per ACK rather than sampling once every RTT+50 (160ms).
    ranked={};SpectatorLiveRuntime serial;web::pending.clear();pose={100,40,0,false,false};
    int response=0;std::vector<std::int64_t> times;auto sentAt=publishTestClock;
    for(int wall=0;wall<=2000;wall+=10){
        pose.x=100+wall*0.3f;serial.tickPublisher(pose,false);
        if(serial.m_publishBusy && publishTestClock-sentAt>=std::chrono::milliseconds(110)){
            auto body=web::pending[response].request.body;
            auto items=body["packets"].asArray().unwrap();
            for(auto const& item:items) times.push_back(item["elapsedMs"].asInt().unwrapOr(-1));
            auto cb=web::pending[response++].callback;cb({200});
        }
        auto before=web::pending.size();serial.tickPublisher(pose,false);
        if(web::pending.size()>before)sentAt=publishTestClock;
        assert(web::pending.size()<=static_cast<std::size_t>(response+1));
        publishTestClock+=std::chrono::milliseconds(10);
    }
    assert(times.size()>=36&&response>=17);
    for(std::size_t i=0;i<times.size();++i)assert(times[i]==static_cast<std::int64_t>(i)*50);
    // Transport failure preserves the exact batch and does not consume future
    // native capture / evidence until an ACK retires those immutable samples.
    auto sent=web::pending.back().request.body;web::pending.back().callback({500});
    publishTestClock+=std::chrono::milliseconds(100);serial.tickPublisher(pose,false);
    auto retried=web::pending.back().request.body;
    assert(retried["packets"].asArray().unwrap().size()==sent["packets"].asArray().unwrap().size());
    assert(retried["packets"].asArray().unwrap().front()["elapsedMs"].asInt().unwrapOr(-1)==sent["packets"].asArray().unwrap().front()["elapsedMs"].asInt().unwrapOr(-2));
    // Pause is wall-clock budget, not 12 seconds of missing gameplay samples.
    ranked={};SpectatorLiveRuntime paused;web::pending.clear();
    assert(paused.tickPublisher(pose,false));web::pending.back().callback({200});
    auto generation=paused.m_publishGeneration;paused.publisherPaused(true);
    auto elapsed=paused.publisherElapsedMs();publishTestClock+=std::chrono::seconds(12);
    assert(!paused.tickPublisher(pose,false)&&paused.publisherElapsedMs()==elapsed&&web::pending.size()==1);
    paused.publisherPaused(false);publishTestClock+=std::chrono::milliseconds(50);
    assert(paused.tickPublisher(pose,false)&&paused.m_publishGeneration==generation);
    assert(web::pending.back().request.body["packets"].asArray().unwrap().front()["elapsedMs"].asInt().unwrapOr(-1)==50);
    web::pending.back().callback({409});publishTestClock+=std::chrono::milliseconds(100);
    assert(paused.tickPublisher(pose,false));
    assert(web::pending.back().request.body["packets"].asArray().unwrap().front()["elapsedMs"].asInt().unwrapOr(-1)==150);
    assert(paused.m_publishGeneration==generation+1); // Same Attempt clock retains the prior 50ms, without adding paused wall time

    // Capturing a full queue never overlaps the current POST or rewinds its generation.
    ranked={};SpectatorLiveRuntime pressure;web::pending.clear();assert(pressure.tickPublisher(pose,false));
    auto pressureGeneration=pressure.m_publishGeneration;
    for(int i=0;i<100;i++){publishTestClock+=std::chrono::milliseconds(50);pressure.tickPublisher(pose,false);}
    assert(web::pending.size()==1&&pressure.m_publishQueue.size()==64&&pressure.m_publishGeneration==pressureGeneration);
    web::pending.front().callback({200});publishTestClock+=std::chrono::milliseconds(50);pressure.tickPublisher(pose,false);
    assert(web::pending.size()==2&&web::pending.back().request.body["packets"].asArray().unwrap().front()["sequence"].asInt().unwrapOr(-1)==1);
    // An unavailable relay cannot retry this Attempt forever; ordinary gameplay remains independent.
    ranked={};SpectatorLiveRuntime offline;web::pending.clear();
    for(int i=0;i<12;i++){publishTestClock+=std::chrono::seconds(5);offline.tickPublisher(pose,false);assert(web::pending.size()==unsigned(i+1));web::pending.back().callback({503});}
    assert(offline.m_attemptPublishFailures==12);auto pendingCount=web::pending.size();publishTestClock+=std::chrono::seconds(10);offline.tickPublisher(pose,false);assert(web::pending.size()==pendingCount);
    ranked.m_attemptId="next-budget";assert(offline.tickPublisher(pose,false));assert(offline.m_attemptPublishFailures==0&&web::pending.size()==pendingCount+1);
    // Independent Survival transport cannot infer publisher authority from a local UUID.
    ranked={};ranked.state.stage=RuntimeStage::Ready;web::pending.clear();SpectatorLiveRuntime survival;
    survival.m_external=SurvivalSpectatorTransportContext{"survival","R1:target","survival-session","R1","human","",2,456,7,false,true};
    pose={100,40,0,false,false};assert(!survival.tickPublisher(pose,false)&&web::pending.empty());
    survival.m_external->attemptId="ACKed-survival-attempt";assert(survival.tickPublisher(pose,false));
    auto survivalBody=web::pending.back().request.body;
    assert(web::pending.back().request.url=="/api/ranked/survival/online/matches/survival/spectator-live/publish");
    assert(web::pending.back().request.session=="survival-session");
    assert(survivalBody["roundKey"].asString().unwrapOr("")=="R1"&&survivalBody["mapGeneration"].asInt().unwrapOr(0)==7);
    assert(survivalBody["packets"].asArray().unwrap().front()["levelId"].asInt().unwrapOr(0)==456);
    web::pending.back().callback({200});pose.dead=true;survival.publishTerminal(pose,false);
    assert(web::pending.back().request.body["packets"].asArray().unwrap().back()["keyframe"]["dead"].asBool().unwrapOr(false));
    web::pending.back().callback({200});auto oldCount=web::pending.size();survival.publishTerminal(pose,false);assert(web::pending.size()==oldCount);
    survival.m_external->publisher=false;assert(!survival.tickPublisher(pose,false));
    assert(ranked.m_attemptId=="attempt-1"); // no scoring-side Attempt creation/changes
    // Native gameplay time is authoritative even when capture/HTTP starts late.
    ranked={};SpectatorLiveRuntime nativeClock;web::pending.clear();pose.nativeElapsedMs=60050;
    assert(nativeClock.tickPublisher(pose,false));assert(web::pending.back().request.body["packets"].asArray().unwrap().front()["elapsedMs"].asInt().unwrapOr(-1)==60050);
    web::pending.back().callback({409});publishTestClock+=std::chrono::milliseconds(100);pose.nativeElapsedMs=60100;
    assert(nativeClock.tickPublisher(pose,false));assert(web::pending.back().request.body["packets"].asArray().unwrap().front()["elapsedMs"].asInt().unwrapOr(-1)==60100);
    // beta.122 regression: Entropie 70-73s PUBLISH evidence logging must not
    // insert discontinuity/player2 into the packet the relay validates.
    ranked={};ranked.state.stage=RuntimeStage::Ready;web::pending.clear();SpectatorLiveRuntime entropie;
    entropie.m_external=SurvivalSpectatorTransportContext{"survival","R1:target","survival-session","R1","human","entropie-attempt",2,143308401,7,false,true};
    pose={25000,4074,0,false,false};pose.nativeElapsedMs=71100;
    assert(entropie.tickPublisher(pose,false));
    {auto packets=web::pending.back().request.body["packets"].asArray().unwrap();assert(!packets.empty());
     for(auto const& packet:packets){auto const& keyframe=packet["keyframe"];
        assert(packet["levelId"].asInt().unwrapOr(0)==143308401&&packet["elapsedMs"].asInt().unwrapOr(-1)==71100);
        assert(!keyframe.contains("discontinuity")&&!keyframe.contains("player2"));}}
    // P0-A: Survival "no viewers" 409s are demand, not transport failures.
    web::pending.back().callback({200});
    for(int i=0;i<20;i++){publishTestClock+=std::chrono::seconds(2);pose.nativeElapsedMs=72000.0+i*2000;assert(entropie.tickPublisher(pose,false));
        matjson::Value body;body["message"]="SURVIVAL_NO_VIEWERS";web::pending.back().callback({409,body});}
    assert(entropie.m_attemptPublishFailures==0);
    for(int i=0;i<12;i++){publishTestClock+=std::chrono::seconds(2);pose.nativeElapsedMs=120000.0+i*2000;entropie.tickPublisher(pose,false);web::pending.back().callback({503});}
    assert(entropie.m_attemptPublishFailures==12);
}

`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-publisher-native-'));
    try {
        const file = join(dir, 'publisher.cpp'), binary = join(dir, 'publisher');writeFileSync(file, cpp);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', ...(actualJson?[]:['-Wpedantic']), '-Werror', ...(actualJson?['-isystem',process.env.CORUM_MATJSON_SOURCE+'/include','-isystem',process.env.CORUM_RESULT_INCLUDE,...['value','impl','parser','dump','external/dragonbox'].map(p=>process.env.CORUM_MATJSON_SOURCE+'/src/'+p+'.cpp')]:[]),file, '-o', binary], {stdio:'pipe'});
        execFileSync(binary, [], {stdio:'pipe'});
    } finally {rmSync(dir, {recursive:true,force:true});}
});
