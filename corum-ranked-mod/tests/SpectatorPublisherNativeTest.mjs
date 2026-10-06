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
test('actual publisher captures during 110ms ACKs, sends immutable ordered batches and drains terminal exactly once', () => {
    const viewer = read('tests/SpectatorLiveViewerNativeTest.mjs');
    const model = viewer.slice(viewer.indexOf('namespace matjson {'), viewer.indexOf('${readFileSync(join(root,'))
        .replace('Value()=default;', 'Value()=default; static Value array(){return Value(Array{});} void push(Value v){std::get<Array>(data).push_back(std::move(v));}')
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
#include <chrono>
#include <map>
#include <variant>
#include <vector>
#include <functional>
${model}
${read('src/spectator/SpectatorPoseCodec.hpp').replace(/^#include.*$|^#pragma once$/gm, '')}
namespace web {
struct WebResponse {int status=200;int code()const{return status;}};
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
namespace log {template<class... T> void debug(T...){} }
enum class RuntimeStage {Ready,Matched};
struct RankedRuntime {
    struct Match {std::string matchId="match",state="ROUND_PLAYING";int roundNumber=1,deathmatchSequence=0;bool spectatorLiveEnabled=true;};
    struct View {RuntimeStage stage=RuntimeStage::Matched;Match match;} state;
    bool placement=false,spectating=false,m_pendingStart=false;std::vector<int> m_attemptBacklog;std::string m_attemptId="attempt-1";
    static RankedRuntime& get(){static RankedRuntime r;return r;}
    View const& view()const{return state;}
    bool isPlacement()const{return placement;} bool isSpectating()const{return spectating;}
    ${getter}
    std::string spectatorLiveSessionToken()const{return "session";}
    std::string spectatorLiveMatchToken()const{return "match-token";}
};
struct SpectatorLiveRuntime {
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
    assert(web::pending[2].request.body["elapsedMs"].asInt().unwrapOr(-1)==61);
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
    assert(web::pending.back().request.body["packets"].asArray().unwrap().front()["elapsedMs"].asInt().unwrapOr(-1)==100);
    assert(paused.m_publishGeneration==generation+1); // old pause time cannot delay a fresh generation
}

`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-publisher-native-'));
    try {
        const file = join(dir, 'publisher.cpp'), binary = join(dir, 'publisher');writeFileSync(file, cpp);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', file, '-o', binary], {stdio:'pipe'});
        execFileSync(binary, [], {stdio:'pipe'});
    } finally {rmSync(dir, {recursive:true,force:true});}
});
