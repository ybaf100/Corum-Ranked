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
test('actual production publisher sends shared v2 state and terminal capture across in-flight ACK without cadence loss', () => {
    const viewer = read('tests/SpectatorLiveViewerNativeTest.mjs');
    const model = viewer.slice(viewer.indexOf('namespace matjson {'), viewer.indexOf('${readFileSync(join(root,'));
    const rankedHeader = read('src/RankedRuntime.hpp');
    const begin = rankedHeader.indexOf('[[nodiscard]] std::string const& spectatorLiveAttemptId()');
    const getter = rankedHeader.slice(begin, rankedHeader.indexOf('\n    }', begin) + 6);
    const header = read('src/SpectatorLiveRuntime.hpp');
    const fields = header.slice(header.indexOf('    std::string m_publishMatchId;'), header.indexOf('    std::string m_viewMatchId;'));
    const methods = ['void SpectatorLiveRuntime::resetPublisher(', 'void SpectatorLiveRuntime::startPublisherGeneration(',
        'void SpectatorLiveRuntime::tickPublisher(', 'void SpectatorLiveRuntime::publish(',
        'void SpectatorLiveRuntime::publishTerminal(', 'void SpectatorLiveRuntime::tickTerminal('].map(method).join('\n');
    const cpp = `
#include "${join(root, 'src/domain/SpectatorRemotePolicy.hpp')}"
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
namespace log {template<class... T> void debug(T...){} }
enum class RuntimeStage {Ready,Matched};
struct RankedRuntime {
    struct Match {std::string matchId="match",state="ROUND_PLAYING";int roundNumber=1,deathmatchSequence=0;};
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
    void tickPublisher(SpectatorGhostPose const&,bool);void publish(SpectatorGhostPose const&);
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
    ranked.placement=true;live.tickPublisher(pose,false);assert(web::pending.empty());ranked.placement=false;
    ranked.spectating=true;live.tickPublisher(pose,false);assert(web::pending.empty());ranked.spectating=false;
    live.tickPublisher(pose,true);assert(web::pending.empty());
    ranked.m_pendingStart=true;live.tickPublisher(pose,false);assert(web::pending.empty());ranked.m_pendingStart=false;
    ranked.m_attemptBacklog.push_back(1);live.tickPublisher(pose,false);assert(web::pending.empty());ranked.m_attemptBacklog.clear();
    live.tickPublisher(pose,false);assert(web::pending.size()==1);
    assert(web::pending[0].request.body["protocolVersion"].asInt().unwrapOr(0)==2);
    auto decoded=decodeSpectatorPose(web::pending[0].request.body["keyframe"]);
    assert(decoded&&decoded->mode==SpectatorGameMode::Ship&&decoded->mini&&decoded->upsideDown);
    pose.dead=true;live.publishTerminal(pose,false);assert(web::pending.size()==1); // pose preserved while live ACK outstanding
    live.publishTerminal(pose,false);assert(web::pending.size()==1);
    web::pending[0].callback({200});live.tickTerminal();assert(web::pending.size()==2);
    assert(web::pending[1].request.body["sequence"].asInt().unwrapOr(-1)==1);
    assert(web::pending[1].request.body["keyframe"]["dead"].asBool().unwrapOr(false));
    assert(web::pending[1].request.url=="/api/ranked/matches/match/spectator-live/publish");
    live.publishTerminal(pose,false);assert(web::pending.size()==2); // duplicate hooks
    pose.dead=false;live.tickPublisher(pose,false);assert(web::pending.size()==2);
    // End ACK and spectator activation cannot erase a previously legal terminal retry.
    ranked.m_attemptId.clear();ranked.spectating=true;web::pending[1].callback({500});
    live.m_nextTerminalAt={};live.tickTerminal();assert(web::pending.size()==3);
    assert(web::pending[2].request.token=="match-token");web::pending[2].callback({200});assert(!live.m_terminalPending);
    ranked.m_attemptId="attempt-2";ranked.spectating=false;ranked.state.match.state="DEATHMATCH_PLAYING";ranked.state.match.deathmatchSequence=2;
    live.tickPublisher(pose,false);assert(web::pending.size()==4);
    assert(web::pending[3].request.body["sequence"].asInt().unwrapOr(-1)==0);
    assert(web::pending[3].request.body["generation"].asInt().unwrapOr(0)==2);
    pose.finished=true;live.publishTerminal(pose,false);assert(web::pending.size()==4);
    web::pending[3].callback({200});live.tickTerminal();assert(web::pending.size()==5);
    assert(web::pending[4].request.body["deathmatchSequence"].asInt().unwrapOr(0)==2);
    live.m_terminalDeadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);live.tickTerminal();assert(!live.m_terminalPending);
    ranked.m_attemptId="attempt-3";ranked.placement=true;live.publishTerminal(pose,false);assert(web::pending.size()==5);
    ranked.placement=false;ranked.spectating=true;live.publishTerminal(pose,false);assert(web::pending.size()==5);
    ranked.spectating=false;ranked.m_attemptId.clear();SpectatorLiveRuntime early;
    early.publishTerminal(pose,false);assert(web::pending.size()==5 && early.m_deferredTerminalPose);
    ranked.m_attemptId="server-issued-early-attempt";early.tickTerminal();assert(web::pending.size()==6);
    assert(web::pending[5].request.body["attemptId"].asString().unwrapOr("")==ranked.m_attemptId);
    assert(web::pending[5].request.body["sequence"].asInt().unwrapOr(-1)==0);
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-publisher-native-'));
    try {
        const file = join(dir, 'publisher.cpp'), binary = join(dir, 'publisher');writeFileSync(file, cpp);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', file, '-o', binary], {stdio:'pipe'});
        execFileSync(binary, [], {stdio:'pipe'});
    } finally {rmSync(dir, {recursive:true,force:true});}
});
