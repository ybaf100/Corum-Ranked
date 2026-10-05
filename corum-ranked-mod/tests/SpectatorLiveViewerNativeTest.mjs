import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const source = readFileSync(join(root, 'src/SpectatorLiveRuntime.cpp'), 'utf8');
const header = readFileSync(join(root, 'src/SpectatorLiveRuntime.hpp'), 'utf8');
const evidence = JSON.parse(readFileSync(join(root, 'tests/fixtures/debug-spectate-ipados-beta26.json'), 'utf8'));
const method = signature => {
    const start = source.indexOf(signature); assert(start >= 0);
    const open = source.indexOf('{', start);
    let depth = 1, end = open + 1;
    while (depth && end < source.length) {
        if (source[end] === '{') ++depth;
        else if (source[end] === '}') --depth;
        ++end;
    }
    assert.equal(depth, 0); return source.slice(start, end);
};

test('actual viewer decoder recovers timeout only on fresh validated packets and keeps interpolation bounded', () => {
    const methods = ['void SpectatorLiveRuntime::applyViewerResponse(', 'void SpectatorLiveRuntime::setFallback(',
        'std::optional<SpectatorGhostPose> SpectatorLiveRuntime::interpolatedPose()'].map(method).join('\n');
    const fields = header.slice(header.indexOf('    std::string m_viewMatchId;'), header.indexOf('    geode::async::TaskHolder'));
    const cpp = `
#include "${join(root, 'src/domain/SpectatorRemotePolicy.hpp')}"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <map>
#include <variant>
#include <vector>
namespace matjson {
template<class T> struct Result {
    std::optional<T> value;
    bool isErr()const{return !value;}
    T unwrapOr(T fallback)const{return value?*value:fallback;}
    T const& unwrap()const{return *value;}
};
struct Value {
    using Object=std::map<std::string,Value>; using Array=std::vector<Value>;
    std::variant<std::monostate,bool,std::int64_t,double,std::string,Object,Array> data;
    Value()=default; Value(bool x):data(x){} Value(int x):data(std::int64_t(x)){}
    Value(std::int64_t x):data(x){} Value(double x):data(x){}
    Value(char const* x):data(std::string(x)){} Value(std::string x):data(std::move(x)){}
    Value(Array x):data(std::move(x)){} Value(Object x):data(std::move(x)){}
    bool isObject()const{return std::holds_alternative<Object>(data);}
    Value operator[](std::string const& k)const{
        if(auto p=std::get_if<Object>(&data)){auto i=p->find(k);if(i!=p->end())return i->second;}return {};
    }
    Value& operator[](std::string const& k){if(!isObject())data=Object{};return std::get<Object>(data)[k];}
    Result<std::int64_t> asInt()const{if(auto p=std::get_if<std::int64_t>(&data))return {*p};return {};}
    Result<double> asDouble()const{
        if(auto p=std::get_if<double>(&data)){return {*p};}
        if(auto p=std::get_if<std::int64_t>(&data)){return {double(*p)};}return {};
    }
    Result<bool> asBool()const{if(auto p=std::get_if<bool>(&data))return {*p};return {};}
    Result<std::string> asString()const{if(auto p=std::get_if<std::string>(&data))return {*p};return {};}
    Result<Array> asArray()const{if(auto p=std::get_if<Array>(&data))return {*p};return {};}
};
}
${readFileSync(join(root, "src/spectator/SpectatorPoseCodec.hpp"), "utf8").replace(/^#include.*$|^#pragma once$/gm, "")}
namespace corum::ranked::log {template<class... T> void info(T...){} template<class... T> void warn(T...){} template<class... T> void debug(T...){} }
constexpr int kProtocolVersion=2;
constexpr float kPoseDiscontinuityDistance=400;
using corum::ranked::SpectatorGhostPose;
${method('bool finitePose(')}
${method('float poseDistance(')}
namespace corum::ranked {
struct RankedRuntime {
    struct Match {int roundNumber=1,deathmatchSequence=0;}; struct View {Match match;} state;
    static RankedRuntime& get(){static RankedRuntime r;return r;}
    View const& view()const{return state;}
};
enum class SpectatorLiveStatus { Idle,Waiting,Live,Fallback };
struct SpectatorLiveRuntime {
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    std::string m_debugContextId;
#endif
${fields}
    void applyViewerResponse(matjson::Value const&);
    void setFallback(std::string);
    std::optional<SpectatorGhostPose> interpolatedPose()const;
};
${methods}
}
matjson::Value response(int sequence,double x,std::string attempt="A",int generation=1){
    matjson::Value packet;packet["protocolVersion"]=2;packet["matchId"]="match";
    packet["roundNumber"]=1;packet["attemptId"]=attempt;packet["generation"]=generation;
    packet["sequence"]=sequence;packet["keyframe"]["x"]=x;packet["keyframe"]["y"]=80.0;
    packet["keyframe"]["rotation"]=0.0;
    packet["keyframe"]["mode"]="CUBE";packet["keyframe"]["mini"]=false;
    packet["keyframe"]["upsideDown"]=false;packet["keyframe"]["flipped"]=false;
    packet["keyframe"]["dual"]=false;packet["keyframe"]["dead"]=false;packet["keyframe"]["finished"]=false;
    matjson::Value root;root["protocolVersion"]=2;
    root["stream"]["matchId"]="match";root["stream"]["roundNumber"]=1;
    root["stream"]["attemptId"]=attempt;root["stream"]["generation"]=generation;
    root["packets"]=matjson::Value::Array{packet};return root;
}
int main(){
    using namespace corum::ranked;
    // Real shared codec and decoder: all discrete states stay exact at alpha=0.
    for(auto mode : {SpectatorGameMode::Cube,SpectatorGameMode::Ship,SpectatorGameMode::Ball,
        SpectatorGameMode::UFO,SpectatorGameMode::Wave,SpectatorGameMode::Robot,SpectatorGameMode::Spider,SpectatorGameMode::Swing}) {
        SpectatorGhostPose native{100,20,30,false,false};native.mode=mode;
        native.mini=true;native.upsideDown=true;native.flipped=true;
        auto encoded=encodeSpectatorPose(native);auto decoded=decodeSpectatorPose(encoded);
        assert(decoded && decoded->mode==mode && decoded->mini && decoded->upsideDown && decoded->flipped);
        encoded["mode"]="UNKNOWN";assert(!decodeSpectatorPose(encoded));
        encoded=encodeSpectatorPose(native);encoded["mini"]=0;assert(!decodeSpectatorPose(encoded));
        encoded=encodeSpectatorPose(native);encoded["dead"]=true;encoded["finished"]=true;assert(!decodeSpectatorPose(encoded));
        SpectatorLiveRuntime modes;modes.m_viewMatchId="match";
        modes.applyViewerResponse(response(0,100));
        auto incoming=response(1,150);auto pp=incoming["packets"].asArray().unwrap();pp[0]["keyframe"]=encodeSpectatorPose(native);
        incoming["packets"]=pp;modes.applyViewerResponse(incoming);
        auto sample=modes.interpolatedPose();assert(sample && sample->mode==mode && sample->mini && sample->upsideDown);
    }
    auto replaceKeyframe=[](matjson::Value root,SpectatorGhostPose pose){
        auto p=root["packets"].asArray().unwrap();p[0]["keyframe"]=encodeSpectatorPose(pose);root["packets"]=p;return root;
    };
    // Preserve the actual iPadOS rejection magnitude/sequence. Coordinates and
    // native event endpoints here are deliberately synthetic, not recovered data.
    SpectatorLiveRuntime relocation;relocation.m_viewMatchId="match";
    SpectatorGhostPose start{1000,40,0,false,false};start.camera=SpectatorCameraOrigin{700,-20};
    relocation.applyViewerResponse(replaceKeyframe(response(0,1000),start));
    relocation.m_lastViewSequence=${evidence.previousSequence};
    auto identity=relocation.m_viewPresentationEpoch;
    auto snapped=start;snapped.x+=${evidence.distance};snapped.rotation=90;
    snapped.camera=SpectatorCameraOrigin{1429.7268f,200};
    snapped.discontinuity=SpectatorSnapEvidence{SpectatorDiscontinuity::Teleport,start.x,start.y,snapped.x,snapped.y};
    relocation.applyViewerResponse(replaceKeyframe(response(${evidence.rejectedSequence},snapped.x),snapped));
    assert(relocation.m_status==SpectatorLiveStatus::Live && relocation.m_viewPresentationEpoch==identity);
    assert(!relocation.m_previousPose && relocation.interpolatedPose()->x==snapped.x);
    assert(relocation.interpolatedPose()->rotation==90 && relocation.interpolatedPose()->camera->y==200);
    auto continuous=snapped;continuous.discontinuity.reset();continuous.x+=10;
    continuous.camera=SpectatorCameraOrigin{1439.7268f,210};
    relocation.applyViewerResponse(replaceKeyframe(response(21,continuous.x),continuous));
    relocation.m_poseReceivedAt=std::chrono::steady_clock::now()-std::chrono::milliseconds(25);
    auto smooth=relocation.interpolatedPose();assert(smooth->x>snapped.x && smooth->x<continuous.x);
    assert(smooth->camera->y>200 && smooth->camera->y<210);
    continuous.cameraSnap=true;continuous.camera->y=1500;
    relocation.applyViewerResponse(replaceKeyframe(response(22,continuous.x),continuous));
    assert(relocation.interpolatedPose()->camera->y==1500); // native camera relocation independent from pose
    continuous.dead=true;relocation.applyViewerResponse(replaceKeyframe(response(23,continuous.x),continuous));
    assert(relocation.interpolatedPose()->dead && relocation.interpolatedPose()->camera->y==1500);
    relocation.m_poseReceivedAt=std::chrono::steady_clock::now()-std::chrono::hours(1);
    assert(relocation.interpolatedPose()->x==continuous.x); // exact terminal, no extrapolation
    SpectatorLiveRuntime corrupt;corrupt.m_viewMatchId="match";
    corrupt.applyViewerResponse(replaceKeyframe(response(0,start.x),start));
    auto unverified=snapped;unverified.discontinuity.reset();
    corrupt.applyViewerResponse(replaceKeyframe(response(1,unverified.x),unverified));
    assert(corrupt.m_reason=="POSE_DISCONTINUITY"); // old/raw recordings are never given fabricated authority
    SpectatorLiveRuntime badProof;badProof.m_viewMatchId="match";
    badProof.applyViewerResponse(replaceKeyframe(response(0,start.x),start));
    snapped.discontinuity->fromX+=5000;
    badProof.applyViewerResponse(replaceKeyframe(response(1,snapped.x),snapped));
    assert(badProof.m_reason=="POSE_DISCONTINUITY");
    for(auto invalid : {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),1e20}) {
        auto encoded=encodeSpectatorPose(start);encoded["cameraX"]=invalid;
        auto fallback=decodeSpectatorPose(encoded);assert(fallback && !fallback->camera && fallback->x==1000);
    }
    SpectatorLiveRuntime spider;spider.m_viewMatchId="match";
    SpectatorGhostPose snap{100,30,0,false,false};snap.mode=SpectatorGameMode::Spider;
    spider.applyViewerResponse(replaceKeyframe(response(0,100),snap));
    snap.y=900;snap.upsideDown=true;spider.applyViewerResponse(replaceKeyframe(response(1,100),snap));
    assert(spider.m_status==SpectatorLiveStatus::Live && spider.interpolatedPose()->y==900); // snap, no in-between ground
    snap.y=4000;snap.upsideDown=false;spider.applyViewerResponse(replaceKeyframe(response(2,100),snap));
    assert(spider.m_reason=="POSE_DISCONTINUITY");
    SpectatorLiveRuntime dual;dual.m_viewMatchId="match";snap.dual=true;
    dual.applyViewerResponse(replaceKeyframe(response(0,100),snap));assert(dual.m_reason=="DUAL_UNSUPPORTED");
    SpectatorLiveRuntime legacy;legacy.m_viewMatchId="match";auto v1=response(0,100);v1["protocolVersion"]=1;
    legacy.applyViewerResponse(v1);assert(legacy.m_reason=="UNSUPPORTED_PROTOCOL_VERSION");
    SpectatorLiveRuntime viewer;viewer.m_viewMatchId="match";
    viewer.applyViewerResponse(response(0,100));assert(viewer.m_status==SpectatorLiveStatus::Live);
    viewer.applyViewerResponse(response(1,200));
    viewer.m_poseReceivedAt=std::chrono::steady_clock::now()-std::chrono::seconds(5);
    assert(viewer.interpolatedPose()->x==200); // NO extrapolation
    viewer.setFallback("STREAM_TIMEOUT");assert(!viewer.interpolatedPose());
    viewer.applyViewerResponse(response(1,200));assert(viewer.m_status==SpectatorLiveStatus::Fallback);
    auto empty=response(1,200);empty["packets"]=matjson::Value::Array{};
    viewer.applyViewerResponse(empty);assert(viewer.m_status==SpectatorLiveStatus::Fallback);
    viewer.applyViewerResponse(response(2,300));assert(viewer.m_status==SpectatorLiveStatus::Live);
    assert(viewer.interpolatedPose()->x==300); // same-generation fresh recovery
    auto stale=response(3,350);auto bad=stale["packets"].asArray().unwrap();bad[0]["generation"]=0;
    stale["packets"]=bad;viewer.applyViewerResponse(stale);
    assert(viewer.m_status==SpectatorLiveStatus::Fallback && !viewer.interpolatedPose());
    viewer.applyViewerResponse(response(3,350));assert(viewer.m_status==SpectatorLiveStatus::Live);
    viewer.applyViewerResponse(response(5,360));assert(viewer.m_reason=="SEQUENCE_GAP");
    auto epoch=viewer.m_viewPresentationEpoch;auto next=response(0,20,"B",2);next["reset"]=true;
    viewer.applyViewerResponse(next);assert(viewer.m_viewPresentationEpoch>epoch && viewer.interpolatedPose()->x==20);
    auto dead=response(1,30,"B",2);auto terminal=dead["packets"].asArray().unwrap();terminal[0]["keyframe"]["dead"]=true;
    dead["packets"]=terminal;viewer.applyViewerResponse(dead);
    assert(viewer.interpolatedPose()->x==30 && viewer.interpolatedPose()->dead);
    viewer.applyViewerResponse(response(2,40,"B",2));assert(viewer.m_reason=="TERMINAL_STREAM_CANNOT_RESUME");
    viewer.applyViewerResponse(response(3,45,"B",2));assert(viewer.m_reason=="TERMINAL_STREAM_CANNOT_RESUME");
    auto wrongRound=response(2,40,"B",2);wrongRound["stream"]["roundNumber"]=2;
    viewer.applyViewerResponse(wrongRound);assert(viewer.m_reason=="STREAM_CONTEXT_MISMATCH");
    matjson::Value waiting;waiting["protocolVersion"]=2;waiting["waiting"]=true;
    auto waitingEpoch=viewer.m_viewPresentationEpoch;
    viewer.applyViewerResponse(waiting);assert(viewer.m_status==SpectatorLiveStatus::Waiting && !viewer.interpolatedPose());
    assert(viewer.m_viewAttemptId=="B" && viewer.m_viewGeneration==2 && viewer.m_viewPresentationEpoch==waitingEpoch);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    viewer.m_debugContextId="debug-spectate:scope";viewer.m_viewMatchId=viewer.m_debugContextId;
    RankedRuntime::get().state.match.roundNumber=99;
    auto debug=response(0,120,"recording",1);
    debug["stream"]["matchId"]=viewer.m_debugContextId;
    auto packets=debug["packets"].asArray().unwrap();packets[0]["matchId"]=viewer.m_debugContextId;
    debug["packets"]=packets;viewer.applyViewerResponse(debug);
    assert(viewer.m_status==SpectatorLiveStatus::Live && viewer.interpolatedPose()->x==120);
    auto debugStale=debug;auto stalePackets=packets;stalePackets[0]["generation"]=0;
    debugStale["packets"]=stalePackets;viewer.applyViewerResponse(debugStale);
    assert(viewer.m_reason=="STALE_GENERATION" && !viewer.interpolatedPose());
    auto gap=debug;auto gapPackets=packets;gapPackets[0]["sequence"]=3;
    gap["packets"]=gapPackets;viewer.applyViewerResponse(gap);
    assert(viewer.m_reason=="SEQUENCE_GAP");
    debug["reset"]=true;debug["stream"]["generation"]=2;
    packets[0]["generation"]=2;packets[0]["keyframe"]["finished"]=true;
    debug["packets"]=packets;viewer.applyViewerResponse(debug);
    assert(viewer.interpolatedPose()->finished && viewer.interpolatedPose()->x==120);
#endif
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-viewer-native-'));
    try {
        writeFileSync(join(dir, 'viewer.cpp'), cpp);
        const binary = join(dir, 'viewer');
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', join(dir, 'viewer.cpp'), '-o', binary], { stdio: 'pipe' });
        execFileSync(binary, [], { stdio: 'pipe' });
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
            '-DCORUM_RANKED_DEBUG_SPECTATE=1', join(dir, 'viewer.cpp'), '-o', binary], { stdio: 'pipe' });
        execFileSync(binary, [], { stdio: 'pipe' });
    } finally { rmSync(dir, { recursive: true, force: true }); }
});
