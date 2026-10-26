import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const source = readFileSync(join(root, 'src/PlayLayer.cpp'), 'utf8');
const completion = readFileSync(join(root, 'src/EndLevelLayer.cpp'), 'utf8');
const method = signature => {
    const start = source.indexOf(signature);
    assert(start >= 0, signature);
    const open = source.indexOf('{', start);
    let depth = 1, end = open + 1;
    while (depth && end < source.length) {
        if (source[end] === '{') ++depth;
        else if (source[end] === '}') --depth;
        ++end;
    }
    assert.equal(depth, 0);
    return source.slice(start, end);
};

test('actual scene callbacks hand off only after End/authority, even with vanilla postUpdate stopped', () => {
    const callbacks = [
        'void syncSpectatorLocalSafety()', 'corum::ranked::SpectatorSceneContext currentSpectatorSceneContext()',
        'bool watchableSpectatorScene()', 'bool prepareSpectatorScene()',
        'void cleanupSpectatorPresentation()', 'void updateSpectatorView(float dt)',
        'bool finishTerminalGameplayHandoff()', 'bool finishConcededGameplayHandoff()', 'void spectatorFrame(float dt)',
    ].map(method).join('\n');
    const cpp = `
#include "${join(root, 'src/domain/SpectatorCameraPolicy.hpp')}"
#include "${join(root, 'src/domain/SpectatorLocalSafety.hpp')}"
#include <cassert>
#include <chrono>
#include <memory>
#include <vector>
#include <functional>
namespace geode::log { template<class... T> void info(T...) {} }
namespace geode {template<class T>struct WeakRef {T* p;explicit WeakRef(T* v):p(v){}struct Ref{T* p;T* data(){return p;}T* operator->(){return p;}explicit operator bool(){return p;}};Ref lock()const{return {p};}};}
std::vector<std::function<void()>> pendingSceneReturns;
void queueInMainThread(std::function<void()> f){pendingSceneReturns.push_back(f);}
using namespace geode;
struct PlayerObject { bool m_isDead=false,visible=true; void setVisible(bool b){visible=b;} void stopAllActions(){} };
struct CCLabelBMFont { void setString(char const*){} };
struct GJBaseGameLayer { int cameras=0; void updateCamera(float){++cameras;} };
struct CCDirector {void* scene=this;static CCDirector* sharedDirector(){static CCDirector d;return &d;}void* getRunningScene(){return scene;}};
struct PlayLayer: GJBaseGameLayer {
 static inline PlayLayer* current=nullptr;PlayLayer(){current=this;}static PlayLayer* get(){return current;}bool isRunning(){return true;}
    PlayerObject* m_player1=nullptr;
    bool m_isTestMode=false,m_isPracticeMode=false,m_hasCompletedLevel=false;
    int resets=0;
    void onQuit(); // Binding entry dispatches to the installed mock hook below.
    void* getParent(){return CCDirector::sharedDirector()->getRunningScene();}
    void resetLevel(){++resets; m_hasCompletedLevel=false; if(m_player1)m_player1->m_isDead=false;}
};
struct ScopedRealGameplayEnd{bool&f;bool prev;ScopedRealGameplayEnd(bool&flag,bool v):f(flag),prev(flag){f=v;}~ScopedRealGameplayEnd(){f=prev;}};
struct ScopedPersistenceOwner { explicit ScopedPersistenceOwner(PlayLayer*){} };
template<class T> struct GeodeFields {
    std::unique_ptr<T> value=std::make_unique<T>();
    T* operator->(){return value.get();} // Actual Geode Fields accessor is NON-const.
};
namespace corum::ranked {
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
namespace debug {
constexpr int kDebugSpectateLevelId=88350599;
struct DebugSpectateRuntime {
    bool authorized=false;
    static DebugSpectateRuntime& get(){static DebugSpectateRuntime d;return d;}
    bool playbackLevel(int id)const{return authorized && id==kDebugSpectateLevelId;}
    std::string contextId()const{return "debug-spectate:server-approved";}
    bool playbackComplete()const{return false;}
};
}
#endif
bool quitThroughRankedCompletionOverlay(PlayLayer*){return false;}
bool quitRankedPauseForConcession(PlayLayer*){return false;}
enum class RuntimeStage { Matched, Idle };
struct RankedRuntime {
    struct Match {std::string matchId="match",state="ROUND_PLAYING"; int roundNumber=1,deathmatchSequence=0;bool spectatorLiveEnabled=true;};
    struct View {RuntimeStage stage=RuntimeStage::Matched; Match match; int revision=1;} data;
    bool serverSpectator=false,ownAlive=true,placement=false,canEnter=false,attemptInFlight=false;
    bool blocked=false; int ticks=0,levelId=123;
    static RankedRuntime& get(){static RankedRuntime r;return r;}
    View const& view()const{return data;}
    bool conceded=false;bool gameplayConceded(int,int)const{return conceded;}bool mapSkipped=false;bool placementMapSkipped(std::string const& run,int stage,int map)const{return mapSkipped&&run=="match"&&stage==1&&map==1;}
    bool isSpectating()const{return serverSpectator&&!ownAlive&&!placement;}
    bool isPlacement()const{return placement;}
    bool hasLocalGameplayAuthority()const{return ownAlive;}
    bool hasLocalAttemptInFlight()const{return attemptInFlight;}
    bool canEnterCurrentLevel()const{return canEnter;}
    int currentLevelId()const{return levelId;}
    void tick(){++ticks;}
    void setSpectatorLocalTransportBlocked(void const*,bool b){blocked=b;}
};
struct SpectatorLiveRuntime {
void requestSceneRestore(){}

    std::optional<SpectatorRemoteFrame> frame;
    int viewers=0,resets=0;
    static SpectatorLiveRuntime& get(){static SpectatorLiveRuntime l;return l;}
    void tickPublisher(SpectatorGhostPose const&,bool){}
    void tickViewer(){++viewers;}
    auto presentationFrame()const{return frame;}
    enum class Status {Waiting,Fallback,Buffering};
    Status status()const{return Status::Waiting;}
    void reset(){++resets;frame.reset();}
};
using SpectatorLiveStatus=SpectatorLiveRuntime::Status;
struct FakeRemote {
    std::optional<SpectatorRemoteFrame> frame; int creates=0;
    void update(PlayLayer*,bool active,std::optional<SpectatorRemoteFrame> f){
        if(active&&f&&!frame){++creates;} frame=active?f:std::nullopt;
    }
    auto cameraFrame()const{return frame;}
    void cleanup(){frame.reset();}
};
struct FakeCamera {
    SpectatorCameraPolicy policy;
    void attach(GJBaseGameLayer*){policy.update(true,{150,20},{600,300},std::nullopt,std::nullopt);}
    void update(GJBaseGameLayer*,bool active,std::optional<SpectatorRemoteContext> c,std::optional<SpectatorRemoteFrame> f){
        policy.update(active,{150,20},{600,300},c,f);
    }
    void cleanup(GJBaseGameLayer*){policy.reset();}
};
}
struct CorumRankedPlayLayer: PlayLayer {
 bool blockEntryGameplayUpdate(){return false;}
    struct Fields {
        bool safetyRetired=false,vanillaGameplayEndInProgress=false,rankedInitialized=true,rankedLevel=true;
        bool rankedPractice=false,spectatorMode=false,spectatorPending=false,spectatorScenePrepared=false;
        bool spectatorPresentationRetired=false,spectatorWorldResetInProgress=false,spectatorWorldPoseApplied=false,spectatorCollisionChecked=false;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        bool debugRecordScene=false,debugPlaybackScene=false;
        CCLabelBMFont* debugSpectateHud=nullptr;
#endif
        bool attemptStartReported=true,attemptEndReported=false,autoExitRequested=false;
        int levelId=123,renderedRevision=0;
        std::chrono::steady_clock::time_point nextHudRefreshAt{},spectatorHandoffReadyAt{};
        corum::ranked::SpectatorLocalSafety localSafety;
        corum::ranked::SpectatorSceneContext sceneContext{"match",1,0,123};int placementMapIndex=0;
        corum::ranked::FakeRemote remotePlayer;
        corum::ranked::FakeCamera spectatorCamera;
        std::optional<corum::ranked::SpectatorRemoteFrame> spectatorRenderedFrame;std::optional<unsigned> spectatorCameraFrameNumber,spectatorPresentationFrameNumber,spectatorWorldFrameNumber;
        struct Driver {void reset(){}} spectatorWorldDriver;
    };
    GeodeFields<Fields> m_fields;
    int quits=0,huds=0;
    struct DummyFrameGuard{};
    void ensureSpectatorCameraFrame(float dt){
        if(!m_fields->spectatorMode||m_fields->autoExitRequested)return;
        auto frame=corum::ranked::SpectatorLiveRuntime::get().presentationFrame();
        m_fields->remotePlayer.update(this,true,frame);
        GJBaseGameLayer::updateCamera(dt);
        m_fields->spectatorCamera.update(this,true,frame?std::optional(frame->context):std::nullopt,m_fields->remotePlayer.cameraFrame());
    }
    bool freezeLocalDummy(PlayerObject*){return m_fields->localSafety.active();}
    void updateRankedHud(){++huds;}
    void onQuit(){++quits;cleanupSpectatorPresentation();}
    ${callbacks}
};
void PlayLayer::onQuit(){static_cast<CorumRankedPlayLayer*>(this)->onQuit();}
int main(){
    using namespace corum::ranked;
    auto& runtime=RankedRuntime::get(); auto& live=SpectatorLiveRuntime::get();
    {CorumRankedPlayLayer surrendered;runtime.conceded=true;
     assert(surrendered.finishConcededGameplayHandoff());
     surrendered.spectatorFrame(0.01f);assert(surrendered.quits==0&&pendingSceneReturns.size()==1);
     assert(surrendered.m_fields->attemptEndReported&&surrendered.m_fields->autoExitRequested);
     // Server advances before the deferred return. The old scene still returns exactly once.
     runtime.data.match.roundNumber=2;pendingSceneReturns.front()();pendingSceneReturns.clear();
     assert(surrendered.quits==1);runtime.conceded=false;runtime.data.match.roundNumber=1;}
    {CorumRankedPlayLayer old;runtime.conceded=true;old.finishConcededGameplayHandoff();
     CorumRankedPlayLayer newer;pendingSceneReturns.front()();pendingSceneReturns.clear();
     assert(old.quits==0&&newer.quits==0);runtime.conceded=false;}
    {CorumRankedPlayLayer skipped;skipped.m_fields->placementMapIndex=1;runtime.mapSkipped=true;
     assert(skipped.finishConcededGameplayHandoff());pendingSceneReturns.front()();pendingSceneReturns.clear();
     assert(skipped.quits==1);
     CorumRankedPlayLayer second;second.m_fields->placementMapIndex=2;
     assert(!second.finishConcededGameplayHandoff()&&pendingSceneReturns.empty()&&second.quits==0);
     runtime.mapSkipped=false;}
    CorumRankedPlayLayer scene; PlayerObject local; scene.m_player1=&local;
    runtime.serverSpectator=true; scene.spectatorFrame(0.01f);
    assert(!scene.m_fields->spectatorMode && scene.cameras==0 && local.visible);
    // Real local Attempt ends; no normal gameplay postUpdate is called again.
    runtime.serverSpectator=false;runtime.ownAlive=false;
    scene.m_fields->attemptEndReported=true;scene.m_hasCompletedLevel=true;
    scene.spectatorFrame(0.01f);
    assert(scene.m_fields->spectatorPending && runtime.blocked && scene.resets==0 && scene.cameras==0);
    runtime.serverSpectator=true; scene.spectatorFrame(0.01f);
    assert(scene.m_fields->localSafety.active() && scene.m_fields->spectatorScenePrepared);
    assert(!local.visible && scene.resets==1 && !scene.m_hasCompletedLevel && scene.cameras==1);
    assert(scene.m_fields->spectatorCamera.policy.state()==SpectatorCameraState::Waiting);
    live.frame=SpectatorRemoteFrame{{"match",1,0,123,"A",1,1},0,SpectatorGhostPose{1000,400,90,false,false}};
    scene.spectatorFrame(0.01f);scene.spectatorFrame(0.01f);
    assert(scene.resets==1 && scene.m_fields->remotePlayer.creates==1 && scene.cameras==3);
    assert(scene.m_fields->spectatorCamera.policy.origin().x==150 && runtime.blocked);
    runtime.serverSpectator=false;scene.spectatorFrame(0.01f);
    assert(scene.quits==1 && !scene.m_fields->remotePlayer.frame);
    assert(!scene.m_fields->spectatorCamera.policy.ownsCamera() && scene.m_fields->localSafety.active());
    // Same level ID in a different round/DM sequence/match is NOT the same scene.
    for(int dimension=0;dimension<4;++dimension){
        runtime={};runtime.ownAlive=false;runtime.serverSpectator=true;
        CorumRankedPlayLayer other;other.m_player1=&local;other.m_fields->attemptEndReported=true;
        other.spectatorFrame(0.01f);
        if(dimension==0)runtime.data.match.matchId="next";
        if(dimension==1)++runtime.data.match.roundNumber;
        if(dimension==2)++runtime.data.match.deathmatchSequence;
        if(dimension==3)++runtime.levelId;
        other.spectatorFrame(0.01f);assert(other.quits==1 && !other.m_fields->spectatorCamera.policy.ownsCamera());
    }
    for(int excluded=0;excluded<3;++excluded){
        runtime={};runtime.serverSpectator=true;runtime.ownAlive=false;
        CorumRankedPlayLayer ordinary;ordinary.m_player1=&local;ordinary.m_fields->attemptEndReported=true;
        if(excluded==0)runtime.placement=true;
        if(excluded==1)ordinary.m_isPracticeMode=true;
        if(excluded==2)runtime.ownAlive=true;
        ordinary.spectatorFrame(0.01f);
        assert(!ordinary.m_fields->localSafety.active() && ordinary.cameras==0 && ordinary.resets==0);
    }
    runtime={};runtime.serverSpectator=true;runtime.ownAlive=false;runtime.data.match.spectatorLiveEnabled=false;
    CorumRankedPlayLayer disabled;disabled.m_player1=&local;disabled.m_fields->attemptEndReported=true;
    disabled.spectatorFrame(0.01f);
    assert(disabled.quits==1 && disabled.m_fields->localSafety.active() && runtime.blocked);
    assert(!disabled.m_fields->spectatorCamera.policy.ownsCamera() && !disabled.m_fields->remotePlayer.frame);
    // OFF must also exit when spectator permission has not arrived. A terminal
    // death/clear must stay alive until its End ACK, never until ROUND_RESULT.
    for(bool clear: {false,true}){
        runtime={};runtime.data.match.state="ROUND_SETTLING";
        runtime.data.match.spectatorLiveEnabled=false;
        CorumRankedPlayLayer finalAttempt;finalAttempt.m_player1=&local;
        finalAttempt.spectatorFrame(0.01f);
        assert(finalAttempt.quits==0&&!finalAttempt.m_fields->spectatorPending);
        runtime.ownAlive=false;runtime.attemptInFlight=true;
        finalAttempt.m_fields->attemptEndReported=true;finalAttempt.m_hasCompletedLevel=clear;
        finalAttempt.spectatorFrame(0.01f);finalAttempt.spectatorFrame(0.01f);
        assert(finalAttempt.quits==0&&finalAttempt.m_fields->spectatorPending&&runtime.blocked);
        assert(finalAttempt.resets==0&&finalAttempt.cameras==0);
        runtime.attemptInFlight=false;finalAttempt.spectatorFrame(0.01f);
        assert(finalAttempt.quits==1&&finalAttempt.m_fields->autoExitRequested);
        finalAttempt.spectatorFrame(0.01f);assert(finalAttempt.quits==1);
    }
    // ON may attach to the same native scene after delayed ACK + eligibility,
    // but failed eligibility after ACK has bounded percentage-view fallback.
    runtime={};runtime.ownAlive=false;runtime.attemptInFlight=true;
    CorumRankedPlayLayer delayed;delayed.m_player1=&local;delayed.m_fields->attemptEndReported=true;
    delayed.spectatorFrame(0.01f);assert(delayed.quits==0&&!delayed.m_fields->spectatorScenePrepared);
    runtime.serverSpectator=true;delayed.spectatorFrame(0.01f);
    assert(delayed.quits==0&&!delayed.m_fields->spectatorScenePrepared);
    runtime.attemptInFlight=false;delayed.spectatorFrame(0.01f);
    assert(delayed.quits==0&&delayed.m_fields->spectatorScenePrepared&&delayed.cameras==1);
    runtime={};runtime.ownAlive=false;
    CorumRankedPlayLayer unavailable;unavailable.m_player1=&local;unavailable.m_fields->attemptEndReported=true;
    unavailable.spectatorFrame(0.01f);assert(unavailable.quits==0&&unavailable.m_fields->spectatorPending);
    unavailable.m_fields->spectatorHandoffReadyAt=std::chrono::steady_clock::now()-std::chrono::seconds(6);
    unavailable.spectatorFrame(0.01f);assert(unavailable.quits==1);
    // Legal next Attempts and an actually alive final Attempt do not wait/quit.
    runtime={};runtime.ownAlive=false;runtime.canEnter=true;
    CorumRankedPlayLayer nextAttempt;nextAttempt.m_player1=&local;nextAttempt.m_fields->attemptEndReported=true;
    nextAttempt.spectatorFrame(0.01f);assert(nextAttempt.quits==0&&!nextAttempt.m_fields->spectatorPending);
    runtime={};runtime.data.match.state="ROUND_SETTLING";runtime.data.match.spectatorLiveEnabled=false;
    CorumRankedPlayLayer alive;alive.m_player1=&local;alive.m_fields->attemptEndReported=true;
    alive.spectatorFrame(0.01f);assert(alive.quits==0&&!alive.m_fields->spectatorPending);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    runtime={}; runtime.data.stage=RuntimeStage::Idle;runtime.ownAlive=false;
    CorumRankedPlayLayer recorder;recorder.m_player1=&local;recorder.m_fields->rankedLevel=false;
    recorder.m_fields->debugRecordScene=true;recorder.syncSpectatorLocalSafety();
    assert(!recorder.m_fields->localSafety.active() && runtime.blocked); // Real vanilla RECORD, no Ranked transport.
    CorumRankedPlayLayer test;test.m_player1=&local;test.m_fields->rankedLevel=false;
    test.m_fields->debugPlaybackScene=true;test.m_fields->levelId=debug::kDebugSpectateLevelId;
    test.m_fields->sceneContext={"debug-spectate:server-approved",1,0,debug::kDebugSpectateLevelId};
    test.syncSpectatorLocalSafety();assert(!test.m_fields->localSafety.active());
    debug::DebugSpectateRuntime::get().authorized=true;
    test.syncSpectatorLocalSafety(); // Exact init handoff after server-issued scope.
    live.frame=SpectatorRemoteFrame{{"debug-spectate:server-approved",1,0,debug::kDebugSpectateLevelId,"",0,10},-1,std::nullopt};
    test.spectatorFrame(0.01f);
    assert(test.m_fields->localSafety.active() && !local.visible && test.m_fields->spectatorScenePrepared);
    assert(test.m_fields->spectatorCamera.policy.state()==SpectatorCameraState::Waiting);
    live.frame=SpectatorRemoteFrame{{"debug-spectate:server-approved",1,0,debug::kDebugSpectateLevelId,"recording",1,11},0,SpectatorGhostPose{1500,500,30,false,false}};
    test.spectatorFrame(0.01f);
    assert(test.m_fields->spectatorCamera.policy.origin().x==150 && runtime.blocked && test.quits==0);
    assert(test.m_fields->localSafety.suppressDeath(true) && test.m_fields->localSafety.blockPersistence(true));
    assert(test.m_fields->localSafety.freezeAtCompletionBoundary(true,99));
    auto origin=test.m_fields->spectatorCamera.policy.origin();
    live.frame.reset();test.spectatorFrame(0.01f);
    assert(test.m_fields->spectatorCamera.policy.origin().x==origin.x); // No 0,0 camera while waiting.
    debug::DebugSpectateRuntime::get().authorized=false;test.spectatorFrame(0.01f);
    assert(test.quits==1 && !test.m_fields->remotePlayer.frame && !test.m_fields->spectatorCamera.policy.ownsCamera());
    assert(test.m_fields->localSafety.active()); // Async exit saves still blocked.
#endif
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-scene-native-'));
    try {
        // Prove the double rejects the same unsupported const access that
        // beta20's real Geode build found (unique_ptr alone concealed it).
        const invalid = 'struct F { int value; }; struct Fields { F* operator->(); };\nvoid bad(Fields const& f) { (void)f->value; }\n';
        writeFileSync(join(dir, 'const-fields.cpp'), invalid);
        assert.throws(() => execFileSync('g++', ['-std=c++23', '-c', join(dir, 'const-fields.cpp'),
            '-o', join(dir, 'const-fields.o')], { stdio: 'pipe' }), error => /operator->/.test(error.stderr.toString()));
        writeFileSync(join(dir, 'scene.cpp'), cpp);
        const binary = join(dir, 'scene');
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', join(dir, 'scene.cpp'), '-o', binary], { stdio: 'pipe' });
        execFileSync(binary, [], { stdio: 'pipe' });
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
            '-DCORUM_RANKED_DEBUG_SPECTATE=1', join(dir, 'scene.cpp'), '-o', binary], { stdio: 'pipe' });
        execFileSync(binary, [], { stdio: 'pipe' });
    } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('programmatic scene exits preserve scoped round return and completion overlay ACK gate', () => {
    assert.equal((source.match(/PlayLayer::onQuit\(\);/g) ?? []).length, 1,
        'Only the canonical original call in the cleanup-owning onQuit hook is permitted');
    const canonicalQuit = method('void onQuit()');
    assert(canonicalQuit.indexOf('cleanupSpectatorPresentation()') < canonicalQuit.indexOf('ScopedRankedRoundQuit'));
    assert(canonicalQuit.indexOf('ScopedRankedRoundQuit') < canonicalQuit.indexOf('PlayLayer::onQuit()'));
    const handoff = method('bool finishTerminalGameplayHandoff()');
    assert(handoff.indexOf('hasLocalGameplayAuthority()') < handoff.indexOf('hasLocalAttemptInFlight()'));
    assert(handoff.indexOf('hasLocalAttemptInFlight()') < handoff.indexOf('quitThroughRankedCompletionOverlay'));
    const watch = completion.slice(completion.indexOf('void watchRankedExit(float)'), completion.indexOf('void onReplay('));
    assert(watch.indexOf('hasLocalAttemptInFlight()') < watch.indexOf('runtime.isSpectating()'));
    assert(watch.indexOf('hasLocalAttemptInFlight()') < watch.indexOf('EndLevelLayer::onMenu'));
    assert.match(method('bool prepareSpectatorScene()'), /rankedLevel && runtime\.hasLocalAttemptInFlight\(\)\) return false/);
});

test('actual completion heartbeat drains End ACK before OFF exit or ON native handoff', () => {
    const start = completion.indexOf('void watchRankedExit(float)');
    const open = completion.indexOf('{', start);
    let depth = 1, end = open + 1;
    while (depth && end < completion.length) {
        if (completion[end] === '{') ++depth;
        else if (completion[end] === '}') --depth;
        ++end;
    }
    assert.equal(depth, 0);
    const callback = completion.slice(start, end);
    const cpp = `
#include <cassert>
#include <string>
#define schedule_selector(...) 0
namespace geode::log {template<class... T>void info(T...) {}}
using namespace geode;
template<class T>struct Ref {explicit Ref(T*){}};
struct PlayLayer {};
struct EndLevelLayer {PlayLayer* m_playLayer=nullptr;int exits=0,removed=0;
void onMenu(void*){++exits;}void removeFromParentAndCleanup(bool){++removed;}void unschedule(int){}
};
namespace corum::ranked {
struct RankedRuntime {
    struct Match {std::string state="ROUND_SETTLING";bool spectatorLiveEnabled=false;};
    struct View {Match match;} data;
    bool inFlight=true,spectating=false,authority=false,canEnter=true;int ticks=0;
    static RankedRuntime& get(){static RankedRuntime runtime;return runtime;}
    void tick(){++ticks;}bool hasLocalAttemptInFlight()const{return inFlight;}
    bool hasLocalGameplayAuthority()const{return authority;}bool canEnterCurrentLevel()const{return canEnter;}
    bool isSpectating()const{return spectating;}View const& view()const{return data;}
};
bool prepared=false;int prepareCalls=0;
bool prepareSpectatorScene(PlayLayer*){++prepareCalls;return prepared;}
}
bool authoritativeExitState(std::string const& state){return state=="ROUND_RESULT";}
struct CorumRankedEndLevelLayer:EndLevelLayer {
    struct Fields {bool rankedLevel=true,rankedExitRequested=false;} fields;
    Fields* m_fields=&fields;
    ${callback}
};
int main(){
    using namespace corum::ranked;
    auto& runtime=RankedRuntime::get();
    CorumRankedEndLevelLayer off;
    runtime.spectating=true;off.watchRankedExit(0);
    assert(off.exits==0&&off.removed==0&&prepareCalls==0);
    runtime.inFlight=false;off.watchRankedExit(0);off.watchRankedExit(0);
    assert(off.exits==1&&off.removed==0&&prepareCalls==0);
    runtime={};runtime.spectating=true;runtime.data.match.spectatorLiveEnabled=true;
    CorumRankedEndLevelLayer on;prepared=true;on.watchRankedExit(0);
    assert(on.exits==0&&on.removed==0&&prepareCalls==0);
    runtime.inFlight=false;on.watchRankedExit(0);on.watchRankedExit(0);
    assert(on.exits==0&&on.removed==1&&prepareCalls==1);
    runtime={};runtime.data.match.state="ROUND_RESULT";
    CorumRankedEndLevelLayer result;result.watchRankedExit(0);assert(result.exits==0);
    runtime.inFlight=false;result.watchRankedExit(0);assert(result.exits==1);
    runtime.data.match.state="ROUND_SETTLING";runtime.spectating=false;runtime.canEnter=false;
    CorumRankedEndLevelLayer pending;runtime.inFlight=true;pending.watchRankedExit(0);assert(pending.exits==0);
    runtime.inFlight=false;runtime.authority=true;pending.watchRankedExit(0);assert(pending.exits==0);
    runtime.authority=false;runtime.canEnter=true;pending.watchRankedExit(0);assert(pending.exits==0);
    runtime.canEnter=false;pending.watchRankedExit(0);pending.watchRankedExit(0);assert(pending.exits==1);
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-completion-native-'));
    try {
        writeFileSync(join(dir, 'completion.cpp'), cpp);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
            join(dir, 'completion.cpp'), '-o', join(dir, 'completion')], { stdio: 'pipe' });
        execFileSync(join(dir, 'completion'), [], { stdio: 'pipe' });
    } finally { rmSync(dir, { recursive: true, force: true }); }
});
