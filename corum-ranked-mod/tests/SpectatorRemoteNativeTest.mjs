import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = path => readFileSync(join(root,path),'utf8');
const wire = process.env.CORUM_SPECTATOR_WIRE_FIXTURE ? JSON.parse(readFileSync(process.env.CORUM_SPECTATOR_WIRE_FIXTURE,'utf8')) : null;
function method(source,name) {
    const start=source.indexOf(name);assert(start>=0,name);
    const open=source.indexOf('{',start);let depth=1,end=open+1;
    while(depth){if(source[end]==='{')++depth;if(source[end]==='}')--depth;++end;}return source.slice(start,end);
}
function cppValue(value) {
    if(value===null)return 'matjson::Value{}';
    if(typeof value==='number')return Number.isInteger(value)?`matjson::Value(std::int64_t(${value}))`:`matjson::Value(double(${value}))`;
    if(typeof value==='boolean'||typeof value==='string')return `matjson::Value(${JSON.stringify(value)})`;
    if(Array.isArray(value))return `matjson::Value(matjson::Value::Array{${value.map(cppValue).join(',')}})`;
    return `matjson::Value(matjson::Value::Object{${Object.entries(value).map(([k,v])=>`{${JSON.stringify(k)},${cppValue(v)}}`).join(',')}})`;
}
const viewerSource=read('src/SpectatorLiveRuntime.cpp'), viewerHeader=read('src/SpectatorLiveRuntime.hpp');
const viewerTest=read('tests/SpectatorLiveViewerNativeTest.mjs');
const valueModel=viewerTest.slice(viewerTest.indexOf('namespace matjson {'),viewerTest.indexOf('${readFileSync(join(root,'));
const decoderHarness=`
#include "${join(root, 'src/domain/SpectatorInterpolationBuffer.hpp')}"
#include <chrono>
#include <limits>
#include <map>
#include <variant>
${valueModel}
${read('src/spectator/SpectatorPoseCodec.hpp').replace(/^#include.*$|^#pragma once$/gm,'')}
auto wireClock=std::chrono::steady_clock::time_point{};auto testClockNow(){return wireClock;}
constexpr auto kViewerPollInterval=std::chrono::milliseconds(80);
constexpr int kProtocolVersion=2;constexpr float kPoseDiscontinuityDistance=400;
${method(viewerSource,'float poseDistance(')}
namespace corum::ranked {
namespace log=geode::log;
struct RankedRuntime {
 struct Match{int roundNumber=1,deathmatchSequence=0;};struct View{Match match;}data;
 static RankedRuntime& get(){static RankedRuntime r;return r;}View const& view()const{return data;}
};
enum class SpectatorLiveStatus{Idle,Waiting,Live,Fallback};
struct SpectatorLiveRuntime {
${viewerHeader.slice(viewerHeader.indexOf('    std::string m_viewMatchId;'),viewerHeader.indexOf('    geode::async::TaskHolder'))}
 void applyViewerResponse(matjson::Value const&);void setFallback(std::string);
 std::optional<SpectatorGhostPose> interpolatedPose()const;
};
${['void SpectatorLiveRuntime::applyViewerResponse(', 'void SpectatorLiveRuntime::setFallback(', 'std::optional<SpectatorGhostPose> SpectatorLiveRuntime::interpolatedPose()'].map(n=>method(viewerSource,n)).join('\n').replaceAll('std::chrono::steady_clock::now()','testClockNow()')}
}
`;

// Compile the COMPLETE production renderer translation unit. These observable
// native lifecycle doubles verify ownership/control flow, not GD ABI or visuals.
test('production Remote Player + camera follow two-client presentation without local gameplay writes', () => {
    const dir = mkdtempSync(join(tmpdir(), 'corum-remote-native-'));
    try {
        mkdirSync(join(dir, 'Geode/binding'), { recursive: true });
        const header = read('tests/helpers/SpectatorNativeDouble.hpp');
        writeFileSync(join(dir, 'Geode/Geode.hpp'), header);
        for (const name of ['PlayLayer', 'GJBaseGameLayer', 'PlayerObject', 'GJGameLevel', 'GhostTrailEffect', 'HardStreak']) {
            writeFileSync(join(dir, `Geode/binding/${name}.hpp`), '#include <Geode/Geode.hpp>\n');
        }
        // Verify the double actually reproduces the native overload failure.
        writeFileSync(join(dir, 'ambiguous.cpp'), '#include <Geode/Geode.hpp>\nvoid bad() { cocos2d::CCPoint p; p = {1, 2}; }\n');
        assert.throws(() => execFileSync('g++', ['-std=c++23', '-I', dir, '-c', join(dir, 'ambiguous.cpp'),
            '-o', join(dir, 'ambiguous.o')], { stdio: 'pipe' }), error => /ambiguous/.test(error.stderr.toString()));
        writeFileSync(join(dir, 'test.cpp'), `
#include "${join(root, 'src/spectator/CorumRemotePlayer.hpp')}"
#include "${join(root, 'src/spectator/CorumSpectatorCamera.hpp')}"
#include "${join(root, 'src/domain/SpectatorLocalSafety.hpp')}"
#include "${join(root, 'src/spectator/SpectatorPlayerCapture.hpp')}"
#include "${join(root, 'src/spectator/ScopedCameraTargetOverride.hpp')}"
#include <iostream>
using namespace corum::ranked; using namespace cocos2d; using namespace geode::prelude;
${decoderHarness}
int main() {
  {
    GJGameLevel level; PlayerObject local, other; PlayLayer layer;
    layer.m_level=&level; layer.m_player1=&local; layer.m_player2=&other;
    auto* world=CCLayer::create(); geode::Ref<CCLayer> worldOwner(world); layer.m_objectLayer=world;
    local.setPosition({11,22}); local.setRotation(33);
    SpectatorRemoteFrame f{{"match",1,0,123,"attempt",1,1},0,std::nullopt};
    CorumRemotePlayer remote;
    CorumSpectatorCamera camera;
    auto drive=[&](bool authorized) {
        auto frame=remote.cameraFrame();
        if(authorized)camera.attach(&layer);
        auto target=authorized && frame && frame->pose && !frame->pose->camera && !frame->pose->cameraInvalid
          ?std::optional(CCPoint{frame->pose->x,frame->pose->y}):std::nullopt;
        {ScopedCameraTargetOverride overrideTarget(&local,target);layer.updateCamera(0.01f);}
        camera.update(&layer,authorized,f.context,frame);
    };
    layer.m_gameState.m_cameraPosition=CCPoint{150,20};
    camera.update(&layer,false,f.context,std::nullopt); assert(!camera.ownsCamera() && layer.m_gameState.m_cameraPosition.x==150);
    remote.update(&layer,false,f); assert(!remote.player() && PlayerObject::creates==0);
    remote.update(&layer,true,f); assert(!remote.player() && PlayerObject::creates==0);
    drive(true); assert(camera.state()==SpectatorCameraState::Waiting && layer.m_gameState.m_cameraPosition.x==150);
    CCObject::drain();
    for(int i=0;i<1000;++i)remote.update(&layer,true,f);
    assert(PlayerObject::creates==0 && !remote.player());
    f.pose=SpectatorGhostPose{50,70,90,false,false}; remote.update(&layer,true,f);
    auto* first=remote.player(); assert(first && first!=&local && first!=&other && PlayerObject::creates==1);
    assert(first->position.x==50 && first->position.y==70 && first->rotation==90 && first->visible);
    assert(first->unschedules>0 && !first->m_regularTrail->visible);
    assert(local.position.x==11 && local.position.y==22 && local.rotation==33 && local.positionWrites>=1);
    // Every mode uses the SAME visual object, with native state observation only.
    int modeIndex=0;
    for(auto mode : {SpectatorGameMode::Cube,SpectatorGameMode::Ship,SpectatorGameMode::Ball,
        SpectatorGameMode::UFO,SpectatorGameMode::Wave,SpectatorGameMode::Robot,SpectatorGameMode::Spider,SpectatorGameMode::Swing}) {
        f.pose->mode=mode; ++f.sequence; remote.update(&layer,true,f);
        assert(remote.player()==first && first->frame==modeIndex++);
        auto calls=first->nativeModeCalls; ++f.sequence; remote.update(&layer,true,f);
        assert(first->nativeModeCalls==calls); // no repeated initialization
        auto native=captureSpectatorPose(first); assert(native.mode==mode);
    }
    for(bool mini : {true,false}) {
        f.pose->mini=mini; ++f.sequence; remote.update(&layer,true,f);
        assert(captureSpectatorPose(first).mini==mini);
    }
    for(bool gravity : {true,false}) {
        f.pose->upsideDown=gravity; f.pose->flipped=gravity; ++f.sequence; remote.update(&layer,true,f);
        auto captured=captureSpectatorPose(first); assert(captured.upsideDown==gravity && captured.flipped==gravity);
    }
    assert(local.nativeModeCalls==0 && other.nativeModeCalls==0 && local.m_vehicleSize==1);
    for(auto mode : {SpectatorGameMode::Ship,SpectatorGameMode::Wave,SpectatorGameMode::Spider}) {
        f.pose->mode=mode;f.pose->x=1000;f.pose->y=800;f.pose->upsideDown=true;++f.sequence;
        remote.update(&layer,true,f);drive(true);
        assert(layer.m_gameState.m_cameraPosition.y==700 && camera.state()==SpectatorCameraState::Following);
    }
    // Mock Client A telemetry -> B's COMPLETE production renderer + camera.
    PlayerObject clientA; clientA.setPosition({1000,400}); clientA.setRotation(120);
    f.pose=SpectatorGhostPose{clientA.position.x,clientA.position.y,clientA.rotation,false,false}; ++f.sequence;
    remote.update(&layer,true,f); drive(true);
    assert(remote.player()->position.x==1000 && layer.m_gameState.m_cameraPosition.x==835);
    assert(layer.m_gameState.m_cameraPosition.y==300 && local.positionWrites>=1 && local.rotationWrites==1);
    local.setPosition({9000,9000}); drive(true);
    assert(layer.m_gameState.m_cameraPosition.x==835); // dummy cannot steer camera
    // Stale/rejected/no packet never exposes a renderer target to the camera.
    auto saved=f; --f.sequence; remote.update(&layer,true,f); drive(true);
    assert(!remote.cameraFrame() && camera.state()==SpectatorCameraState::Waiting);
    f=saved; f.pose.reset(); remote.update(&layer,true,f); drive(true);
    assert(!first->visible && layer.m_gameState.m_cameraPosition.x==835);
    f=saved; f.pose->x=1100; ++f.sequence; remote.update(&layer,true,f); drive(true);
    assert(layer.m_gameState.m_cameraPosition.x==935); // recovered stream
    f.pose=SpectatorGhostPose{50,70,90,false,false}; ++f.sequence; remote.update(&layer,true,f);
    f.pose->dead=true; ++f.sequence; remote.update(&layer,true,f); assert(first->opacity==0 && first->m_isDead);
    drive(true); assert(camera.state()==SpectatorCameraState::Terminal);
    f.pose->dead=false; f.pose->x=200; ++f.sequence; remote.update(&layer,true,f);
    assert(first->opacity==0 && first->position.x==50);
    drive(true); assert(layer.m_gameState.m_cameraPosition.x==-115); // frozen DEAD renderer, not later pose
    geode::Ref<PlayerObject> old(first); ++f.context.epoch;
    remote.update(&layer,true,f); assert(remote.player()!=first && !first->parent && !first->m_regularTrail);
    auto* second=remote.player(); f.pose->finished=true; f.pose->x=300; ++f.sequence;
    remote.update(&layer,true,f); assert(second->visible && second->position.x==300);
    f.pose->x=400; ++f.sequence; remote.update(&layer,true,f); assert(second->position.x==300);
    drive(true); assert(layer.m_gameState.m_cameraPosition.x==135); // terminal camera matches rendered pose
    f.pose.reset(); remote.update(&layer,true,f); assert(!second->visible);
    remote.update(&layer,false,f); assert(!remote.player() && world->children.empty());
    camera.cleanup(&layer); camera.cleanup(&layer);
    assert(!camera.ownsCamera() && !layer.m_gameState.m_unkBool4 && !layer.m_gameState.m_unkBool5);
    assert(layer.m_gameState.m_cameraPosition.x==150);
    ${wire ? `
    // Real Nest HTTP -> PostgreSQL -> server playback clock/relay responses
    // enter the production C++ decoder, renderer and camera. No local replay clock.
    remote.cleanup();camera.cleanup(&layer);level.m_levelID=88350599;
    SpectatorLiveRuntime viewer;viewer.m_viewMatchId=${JSON.stringify(wire[0].stream.matchId)};
    PlayerObject* wirePlayer=nullptr;std::uint64_t wireEpoch=0;
    ${wire.map((response,i)=>`
    wireClock=std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(${response.packets.at(-1).elapsedMs});
    viewer.applyViewerResponse(${cppValue(response)});
    assert(viewer.m_status==SpectatorLiveStatus::Live);
    f.context={viewer.m_viewMatchId,1,0,88350599,viewer.m_viewAttemptId,viewer.m_viewGeneration,viewer.m_viewPresentationEpoch};
    f.sequence=viewer.m_lastViewSequence;f.pose=viewer.interpolatedPose();
    remote.update(&layer,true,f);drive(true);assert(remote.player() && f.pose->camera);
    ${i===0?'wirePlayer=remote.player();wireEpoch=viewer.m_viewPresentationEpoch;': 'assert(remote.player()==wirePlayer && viewer.m_viewPresentationEpoch==wireEpoch);'}
    assert(layer.m_gameState.m_cameraPosition.x==f.pose->camera->x && layer.m_gameState.m_cameraPosition.y==f.pose->camera->y);
    ${i===1?'assert(viewer.m_sampleBuffer.size()>=1 && remote.player()->position.x==f.pose->x);':''}
    `).join('\n')}
    // The final HTTP packet is still ahead of the rendered clock. A snap must
    // NOT jump that clock forward. Drain the remaining publisher timeline,
    // then validate exact terminal presentation and its indefinitely held pose.
    assert(!f.pose->dead && !f.pose->finished);
    wireClock+=std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double,std::milli>(viewer.m_sampleBuffer.latestTimeMs()-viewer.m_sampleBuffer.renderTimeMs()));
    f.pose=viewer.interpolatedPose();remote.update(&layer,true,f);drive(true);
    assert(f.pose->dead || f.pose->finished);auto held=remote.player()->position;
    wireClock+=std::chrono::seconds(5);
    f.pose=viewer.interpolatedPose();remote.update(&layer,true,f);drive(true);
    assert(remote.player()==wirePlayer && remote.player()->position.x==held.x);
    ` : ''}
    remote.cleanup(); remote.cleanup();
    f.pose=SpectatorGhostPose{50,70,90,false,false};
    PlayerObject::fail=true; ++f.context.generation; remote.update(&layer,true,f);
    auto creates=PlayerObject::creates; for(int i=0;i<20;++i)remote.update(&layer,true,f);
    assert(PlayerObject::creates==creates && !remote.player() && world->children.empty());
    PlayerObject::fail=false; ++f.context.generation; remote.update(&layer,true,f); assert(remote.player());
    CCObject::drain();
    // Simulated parent teardown before wrapper cleanup; retained visual/root
    // ownership prevents use-after-free. Cleanup removes all owned companions.
    worldOwner=nullptr; layer.m_objectLayer=nullptr; remote.cleanup(); CCObject::drain();
    assert(local.position.x==9000 && local.rotation==33);
  }
  CCObject::drain(); assert(CCObject::alive==0);
  std::cout << "production remote lifecycle/pose/ownership scenarios passed\\n";
}
`);
        const binary = join(dir, 'test');
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I', dir,
            join(root, 'src/spectator/CorumRemotePlayer.cpp'), join(root, 'src/spectator/SpectatorPlayerCapture.cpp'), join(root, 'src/spectator/CorumSpectatorCamera.cpp'),
            join(dir, 'test.cpp'), '-o', binary], { stdio: 'pipe' });
        assert.match(execFileSync(binary, { encoding: 'utf8' }), /scenarios passed/);
    } finally { rmSync(dir, { recursive: true, force: true }); }
});
