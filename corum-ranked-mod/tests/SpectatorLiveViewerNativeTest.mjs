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
namespace corum::ranked::log {template<class... T> void info(T...){} template<class... T> void warn(T...){} template<class... T> void debug(T...){} }
constexpr int kProtocolVersion=1;
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
    matjson::Value packet;packet["protocolVersion"]=1;packet["matchId"]="match";
    packet["roundNumber"]=1;packet["attemptId"]=attempt;packet["generation"]=generation;
    packet["sequence"]=sequence;packet["keyframe"]["x"]=x;packet["keyframe"]["y"]=80.0;
    packet["keyframe"]["rotation"]=0.0;
    matjson::Value root;root["protocolVersion"]=1;
    root["stream"]["matchId"]="match";root["stream"]["roundNumber"]=1;
    root["stream"]["attemptId"]=attempt;root["stream"]["generation"]=generation;
    root["packets"]=matjson::Value::Array{packet};return root;
}
int main(){
    using namespace corum::ranked;
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
    auto wrongRound=response(2,40,"B",2);wrongRound["stream"]["roundNumber"]=2;
    viewer.applyViewerResponse(wrongRound);assert(viewer.m_reason=="STREAM_CONTEXT_MISMATCH");
    matjson::Value waiting;waiting["protocolVersion"]=1;waiting["waiting"]=true;
    viewer.applyViewerResponse(waiting);assert(viewer.m_status==SpectatorLiveStatus::Waiting && !viewer.interpolatedPose());
    assert(viewer.m_viewAttemptId.empty() && viewer.m_viewGeneration==0);
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
