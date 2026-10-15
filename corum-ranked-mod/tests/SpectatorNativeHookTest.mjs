import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const source = readFileSync(join(root, 'src/PlayLayer.cpp'), 'utf8');

// Compile and execute the actual production callback bodies, not retyped hook
// decisions. GD/Geode itself is represented by observable lifecycle doubles;
// this cannot prove ABI, native physics or device rendering correctness.
function method(signature, from = 0) {
    const start = source.indexOf(signature, from);
    assert(start >= 0, signature);
    const brace = source.indexOf('{', start);
    let depth = 1;
    for (let i = brace + 1; i < source.length; ++i) {
        if (source[i] === '{') ++depth;
        if (source[i] === '}' && --depth === 0) return source.slice(start, i + 1);
    }
    assert.fail(signature);
}

test('production native safety hook bodies execute through death/completion/save/physics lifecycle doubles', () => {
    const callbacks = [
        'void syncSpectatorLocalSafety()', 'bool blocksLocalEvidence()',
        'bool suppressLocalCompletion()', 'bool freezeLocalDummy(',
        'bool blocksLevelPersistence(', 'bool scoringEnabled()',
        'void destroyPlayer(', 'void levelComplete()', 'void showCompleteText()',
        'void checkForEnd()', 'void playEndAnimationToPos(',
        'void playPlatformerEndAnimationToPos(', 'void showEndLayer()', 'void showNewBest(', 'void updateAttempts()',
        'void onQuit()', 'void onExit()',
    ].map(signature => method(signature)).join('\n');
    const player = method('void update(float dt)', source.indexOf('class $modify(CorumRankedSpectatorDummyPlayer'));
    const saves = method('void savePercentage(');
    const reset = method('void resetLevel()');
    const dummyReset = reset.slice(reset.indexOf('if (blocksLocalEvidence())'), reset.indexOf('        if (\n            scoringEnabled()'));
    const cpp = `
#include "${join(root, 'src/domain/SpectatorLocalSafety.hpp')}"
#include "${join(root, 'src/domain/SpectatorRemotePolicy.hpp')}"
#include "${join(root, 'src/domain/RankedGameplayPolicy.hpp')}"
#include "${join(root, 'src/spectator/ScopedSpectatorPersistenceMode.hpp')}"
#include <algorithm>
#include <cassert>
#include <functional>
#include "${join(root, 'src/domain/SpectatorNativeProbe.hpp')}"
#include <memory>
#include <optional>
#include <string>
struct SurvivalDialog {void cancel(){}};struct SurvivalDialogRef{SurvivalDialog* lock(){return nullptr;}};
namespace gd { using string = std::string; }
namespace geode::log { template<class... T> void info(T...) {} template<class... T> void error(T...) {} }
using namespace geode;
struct AppDelegate {bool m_gamePaused=false;static AppDelegate* get(){static AppDelegate a;return &a;}};
struct CCPoint { float x, y; };
struct GameObject { int m_uniqueID = 0; };
struct PlayerObject {
    int updates = 0; bool m_isDead=false,m_wasTeleported=false;
    void setVisible(bool) {} void update(float) { ++updates; }
    CCPoint getPosition()const{return {100,50};} float getRotation()const{return 0;}
};
struct GJGameLevel {
    int best = 41, scores = 0;
    void savePercentage(int p, bool, int, int, bool) { best = std::max(best, p); }
    void storeNewLocalScore(int, int) { ++scores; }
};
void persist(GJGameLevel*, int);
struct PlayLayer {
    struct {bool m_isDualMode=false;} m_gameState;
    PlayerObject* m_player1 = nullptr;
    PlayerObject* m_player2 = nullptr;
    GJGameLevel* m_level = nullptr;
    GameObject* m_anticheatSpike = nullptr;
    bool m_isTestMode = false, m_isPracticeMode = false, m_hasCompletedLevel = false;
    bool ignoreNativeDeath = false, completeDuringNativeDeath = false;
    std::function<void()> completionCleanup;
    std::function<void()> beforeNativeDeath;
    int deaths = 0, completions = 0, checks = 0, texts = 0, animations = 0, endLayers = 0, newBest = 0;
    double percent = 0;
    void destroyPlayer(PlayerObject* player, GameObject*) {
        ++deaths;
        if(beforeNativeDeath)beforeNativeDeath();
        if (completeDuringNativeDeath) {m_hasCompletedLevel=true; return;}
        if (ignoreNativeDeath || m_hasCompletedLevel) return;
        if(player)player->m_isDead=true;
        persist(m_level, int(percent));
    }
    void levelComplete() { ++completions; m_hasCompletedLevel=true; persist(m_level, 100); }
    void showCompleteText() { ++texts; }
    void checkForEnd() { ++checks; }
    void playEndAnimationToPos(CCPoint) { ++animations; if(completionCleanup)completionCleanup(); }
    void playPlatformerEndAnimationToPos(CCPoint, bool) { ++animations; if(completionCleanup)completionCleanup(); }
    void showEndLayer() { ++endLayers; }
    void showNewBest(bool, int, int, bool, bool, bool) { ++newBest; }
    int attempts=0;void updateAttempts(){++attempts;}
    int quits=0,exits=0,resets=0;
    bool m_isPaused=false;void onQuit();void onExit();void resetLevel();
};
namespace corum::ranked {struct SurvivalRuntime {static SurvivalRuntime&get(){static SurvivalRuntime r;return r;}bool freePlayReturnPending()const{return false;}void beginFreePlayNativeQuit(GJGameLevel*){}void finishFreePlayNativeQuit(){}void endAttempt(double){}bool attemptLimitReached(){return false;}bool canPlay(){return false;}};
inline bool resumeSurvivalFreePlayPauseBeforeQuit(PlayLayer*){return true;}
inline bool resumeRankedPauseBeforeQuit(PlayLayer*) { return true; } // tested by RankedPauseQuitNativeTest
inline SpectatorGhostPose captureSpectatorPose(PlayerObject* p,bool dead=false,bool finished=false,bool dual=false){
    auto xy=p->getPosition(); SpectatorGhostPose pose{xy.x,xy.y,p->getRotation(),dead,finished};pose.dual=dual;return pose;
}
struct SpectatorLiveRuntime {
    int terminals=0; static SpectatorLiveRuntime& get(){static SpectatorLiveRuntime l;return l;}
    void publishTerminal(SpectatorGhostPose const&,bool){++terminals;}
    void reset(){}
};
struct ScopedRankedRoundQuit {explicit ScopedRankedRoundQuit(PlayLayer*){}};
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
namespace debug {
struct DebugSpectateRuntime {
    bool authorized=false;int terminals=0;
    static DebugSpectateRuntime& get(){static DebugSpectateRuntime d;return d;}
    bool playbackLevel(int id)const{return authorized && id==88350599;}
    void recordPose(SpectatorGhostPose const&,bool terminal){terminals+=terminal;}
    void exitScene(){authorized=false;}
};
}
#endif
struct RankedRuntime {
    bool serverSpectator = false, ownAlive = true, placement = false;
    void const* owner = nullptr;
    int ends = 0, clears = 0;
    static RankedRuntime& get() { static RankedRuntime value; return value; }
    bool hasLocalGameplayAuthority() { return ownAlive; }
    bool isPlacement() { return placement; }
    void reportPracticePresence(bool){}
    bool isSpectating() { return spectatorAuthorityReady(serverSpectator, placement, ownAlive); }
    void setSpectatorLocalTransportBlocked(void const* p, bool b) { if (b) owner = p; else if (owner == p) owner = nullptr; }
    bool reportAttemptEnd(int, double, bool clear, std::optional<double>, bool = false) {
        if (spectatorAttemptTransportBlocked(isSpectating(), owner != nullptr)) return false;
        ++ends; clears += clear ? 1 : 0; ownAlive = false; return true;
    }
};
}
${source.slice(source.indexOf('thread_local PlayLayer* persistenceOwner'), source.indexOf('struct ScopedRealGameplayEnd {'))}
${source.slice(source.indexOf('struct ScopedRealGameplayEnd {'), source.indexOf('std::int64_t steadyNowMicros()'))}
double rankedProgressPercent(PlayLayer* layer) { return layer->percent; }
struct CorumRankedPlayLayer : PlayLayer {
    struct EntryOverlay { int dismissed=0; void dismiss(){++dismissed;} };
    corum::ranked::SpectatorGhostPose observedSpectatorPose(bool dead=false,bool finished=false) {
        return corum::ranked::captureSpectatorPose(m_player1,dead,finished,m_gameState.m_isDualMode);
    }
    void observeSpectatorNativeUpdate(PlayerObject*) {}
    bool observesSpectatorPlayer(PlayerObject*) {return false;}
    void observeSpectatorNativeSnap(PlayerObject*,corum::ranked::SpectatorDiscontinuity,corum::ranked::SpectatorGhostPose const&) {}
    struct Fields {std::optional<SurvivalDialogRef> survivalSurrender;bool survivalLevel=false,survivalWaiting=false,survivalFreePlayHud=false,survivalNativeClearFinished=false;
        bool nativeCompletionStarted = false;
        EntryOverlay* entryOverlay = nullptr;
        bool nativeEntryHeld = false;
        bool spectatorPresentationRetired=false,spectatorScenePrepared=false,autoExitRequested=false;
        bool entryAttemptCountDeferred = false;
        bool safetyRetired = false, vanillaGameplayEndInProgress = false;
        bool attemptStartReported = true, attemptEndReported = false;
        bool pauseQuitBlockedLogged = false;
        bool rankedLevel = true, rankedInitialized = true, rankedPractice = false;
        bool spectatorMode = false, spectatorPending = false;
        bool suppressedDeathLogged = false, suppressedCompletionLogged = false, suppressedSaveLogged = false;
        int levelId = 123; double qualifyingPercent = 50;
        corum::ranked::SpectatorLocalSafety localSafety;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        bool debugRecordScene=false,debugPlaybackScene=false;
#endif
    };
    std::unique_ptr<Fields> m_fields = std::make_unique<Fields>();
    void cleanupSpectatorPresentation(){}
    void spectatorFrame(float){}
    void unschedule(int){}
    #define schedule_selector(x) 0
    void testDummyResetBoundary(){${dummyReset}}
    ${callbacks}
};
CorumRankedPlayLayer* currentLayer = nullptr;
CorumRankedPlayLayer* localSafetyLayer() { return persistenceOwner ? static_cast<CorumRankedPlayLayer*>(persistenceOwner) : currentLayer; }
struct PlayerHook : PlayerObject { ${player} };
struct SaveHook : GJGameLevel { ${saves} };
void persist(GJGameLevel* level, int percent) {
    if (level) {
        static_cast<SaveHook*>(level)->savePercentage(percent, false, 0, 0, true);
        if (percent == 100) static_cast<SaveHook*>(level)->storeNewLocalScore(100, 0);
    }
}
void PlayLayer::onQuit(){++quits;currentLayer=nullptr;if(!m_isTestMode)persist(m_level,int(percent));}
void PlayLayer::onExit(){++exits;currentLayer=nullptr;if(!m_isTestMode)persist(m_level,int(percent));}
void PlayLayer::resetLevel(){++resets;if(!m_isTestMode)persist(m_level,int(percent));}
int main() {
    using corum::ranked::RankedRuntime;
    using namespace corum::ranked;
    auto& runtime = RankedRuntime::get();
    {
        runtime={};PlayerHook first;SaveHook localLevel;CorumRankedPlayLayer real;
        currentLayer=&real;real.m_player1=&first;real.m_level=&localLevel;real.percent=99.12;
        real.beforeNativeDeath=[&]{real.playEndAnimationToPos({100,0});};
        real.destroyPlayer(&first,nullptr);
        assert(first.m_isDead && runtime.ends==0 && real.m_fields->nativeCompletionStarted);
        real.levelComplete();assert(runtime.ends==1&&runtime.clears==1);
    }
    // Native end animations can dispose of the dual avatar before the finish
    // flag is set. That accepted cleanup death must not beat the genuine clear.
    for(bool platformer:{false,true}) {
        runtime={};PlayerHook first,second;SaveHook localLevel;CorumRankedPlayLayer real;
        currentLayer=&real;real.m_player1=&first;real.m_player2=&second;real.m_level=&localLevel;
        real.percent=99.12;real.m_gameState.m_isDualMode=true;
        real.completionCleanup=[&]{real.destroyPlayer(&second,nullptr);};
        if(platformer)real.playPlatformerEndAnimationToPos({100,0},true);
        else real.playEndAnimationToPos({100,0});
        assert(real.m_fields->nativeCompletionStarted && runtime.ends==0 && second.m_isDead);
        real.levelComplete();real.showCompleteText();real.showEndLayer();
        assert(runtime.ends==1 && runtime.clears==1);
    }
    runtime={};
    for(int clear=1;clear<=3;++clear) {
        runtime.ownAlive=true;runtime.serverSpectator=clear==3;
        PlayerHook first,second;SaveHook localLevel;CorumRankedPlayLayer real;
        currentLayer=&real;real.m_player1=&first;real.m_player2=&second;real.m_level=&localLevel;
        real.percent=99.12;real.m_gameState.m_isDualMode=true;
        real.playEndAnimationToPos({100,0});real.destroyPlayer(&second,nullptr);
        assert(runtime.clears==clear-1 && runtime.ownAlive);
        // Final/expired clock permission cannot preempt this already-started
        // Attempt while it is completing. Genuine native saves still finish.
        real.levelComplete();real.showCompleteText();
        assert(runtime.ends==clear && runtime.clears==clear && localLevel.best==100);
    }
    // A callback is not a confirmed death. Native completion cleanup / ignored
    // collisions cannot close the journal at BLOZ's real ~99.1% finish point.
    for (int outcome=0;outcome<3;++outcome) {
        runtime={};PlayerHook localPlayer;SaveHook localLevel;CorumRankedPlayLayer real;
        currentLayer=&real;real.m_player1=&localPlayer;real.m_level=&localLevel;real.percent=99.12;
        real.ignoreNativeDeath=outcome==0;
        real.m_hasCompletedLevel=outcome==1;
        real.completeDuringNativeDeath=outcome==2;
        real.destroyPlayer(&localPlayer,nullptr);
        assert(runtime.ends==0 && !real.m_fields->attemptEndReported);
        if(outcome==0) real.levelComplete();
        else real.showEndLayer(); // native completion flag backs up omitted callbacks
        real.showCompleteText();real.showEndLayer();
        assert(runtime.ends==1 && runtime.clears==1);
    }
    // A genuine death at the very same percentage remains a non-clear. Inactive
    // second players and foreign players cannot end the local scoring Attempt.
    for(int mode=0;mode<4;++mode) {
        runtime={};PlayerHook first,second,foreign;SaveHook localLevel;CorumRankedPlayLayer real;
        currentLayer=&real;real.m_player1=&first;real.m_player2=&second;real.m_level=&localLevel;
        real.percent=99.12;real.m_gameState.m_isDualMode=mode==2;
        auto* target=mode==0 ? &first : mode==3 ? &foreign : &second;
        real.destroyPlayer(target,nullptr);real.destroyPlayer(target,nullptr);
        assert(runtime.ends==((mode==0||mode==2)?1:0) && runtime.clears==0);
    }
    runtime={};
    PlayerHook local, other;
    SaveHook level, unrelated;
    CorumRankedPlayLayer scene;
    currentLayer = &scene; scene.m_player1 = &local; scene.m_level = &level;
    // Normal final death receives server permission during End handling. The
    // ORIGINAL vanilla writer still records its genuine 83% before conversion.
    runtime.serverSpectator = true; scene.percent = 83;
    scene.destroyPlayer(&local, nullptr);
    assert(runtime.ends == 1 && scene.deaths == 1 && level.best == 83);
    assert(!scene.m_fields->localSafety.active());
    scene.syncSpectatorLocalSafety();
    assert(scene.m_fields->localSafety.active());
    scene.destroyPlayer(&local, nullptr);
    assert(scene.deaths == 1 && runtime.ends == 1);
    scene.destroyPlayer(&other, nullptr);
    assert(scene.deaths == 2 && runtime.ends == 1);
    scene.percent = 10; local.update(0.01f); other.update(0.01f);
    assert(local.updates == 0 && other.updates == 1); // viewer freezes dummy immediately
    scene.percent = 99; local.update(0.01f); other.update(0.01f);
    assert(local.updates == 0 && other.updates == 2); // independent 99% backstop
    scene.percent = 100;
    scene.levelComplete(); scene.showCompleteText(); scene.checkForEnd();
    scene.playEndAnimationToPos({0, 0}); scene.playPlatformerEndAnimationToPos({0, 0}, true);
    scene.showEndLayer(); scene.showNewBest(false, 0, 0, false, false, false);
    scene.updateAttempts();assert(scene.attempts==0);
    level.savePercentage(100, false, 0, 0, true);
    for(int i=0;i<20;++i){scene.levelComplete();scene.checkForEnd();scene.showCompleteText();}
    assert(scene.completions == 0 && scene.texts == 0 && scene.checks == 0 && scene.animations == 0);
    assert(scene.endLayers == 0 && scene.newBest == 0 && runtime.clears == 0);
    assert(level.best == 83 && level.scores == 0 && !scene.m_isPracticeMode);
    unrelated.savePercentage(95, false, 0, 0, true);
    unrelated.storeNewLocalScore(100,0);
    assert(unrelated.best == 95 && unrelated.scores == 1);
    // Fresh normal scene has all original callbacks and persistence intact.
    CorumRankedPlayLayer normal;
    SaveHook normalLevel;
    currentLayer = &normal; normal.m_player1 = &local; normal.m_level = &normalLevel;
    runtime = {}; normal.percent = 100; local.update(0.01f);
    normal.levelComplete(); normal.showCompleteText(); normal.checkForEnd();
    normal.showNewBest(false, 0, 0, false, false, false);
    assert(normal.completions == 1 && normal.texts == 1 && normal.checks == 1 && normal.newBest == 1);
    assert(normalLevel.best == 100 && normalLevel.scores == 1 && runtime.clears == 1 && local.updates == 1);
    // Real local / Ranked / Placement saves share the same vanilla boundary.
    // Placement final accepted Attempt survives both timers reaching zero;
    // MAP_SETTLING is still gameplay, never spectator authority.
    for (int scenario = 0; scenario < 4; ++scenario) {
        runtime = {}; runtime.placement = scenario >= 2;
        CorumRankedPlayLayer real; SaveHook realLevel;
        currentLayer = &real; real.m_player1 = &local; real.m_level = &realLevel;
        real.m_fields->rankedLevel = scenario != 0;
        real.percent = 100;
        real.syncSpectatorLocalSafety();
        assert(!real.m_fields->localSafety.active());
        real.levelComplete(); real.showCompleteText(); real.showCompleteText();
        assert(real.completions == 1 && realLevel.scores == 1 && realLevel.best == 100);
        assert(runtime.clears == (scenario == 0 ? 0 : 1));
        assert(!real.blocksLevelPersistence(&realLevel));
    }
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    runtime={};debug::DebugSpectateRuntime::get().authorized=true;
    CorumRankedPlayLayer viewer; SaveHook protectedLevel;
    currentLayer=&viewer;viewer.m_level=&protectedLevel;viewer.m_player1=&local;
    viewer.m_fields->rankedLevel=false;viewer.m_fields->debugPlaybackScene=true;viewer.m_fields->levelId=88350599;
    viewer.syncSpectatorLocalSafety();
    viewer.percent=100;viewer.destroyPlayer(&local,nullptr);viewer.levelComplete();viewer.showCompleteText();
    viewer.checkForEnd();viewer.playEndAnimationToPos({0,0});viewer.showEndLayer();
    viewer.showNewBest(false,0,0,false,false,false);protectedLevel.savePercentage(100,false,0,0,true);
    viewer.levelComplete();viewer.showCompleteText();
    assert(viewer.deaths==0 && viewer.completions==0 && viewer.texts==0 && viewer.newBest==0);
    assert(protectedLevel.best==41 && protectedLevel.scores==0 && runtime.ends==0 && runtime.clears==0);
    local.update(0.01f);assert(viewer.freezeLocalDummy(&local));
    CorumRankedPlayLayer recorder;SaveHook realRecordLevel;
    currentLayer=&recorder;recorder.m_level=&realRecordLevel;recorder.m_player1=&local;
    recorder.m_fields->rankedLevel=false;recorder.m_fields->debugRecordScene=true;recorder.m_fields->levelId=88350599;
    recorder.percent=73;recorder.destroyPlayer(&local,nullptr);
    assert(recorder.deaths==1 && !recorder.m_fields->localSafety.active() && realRecordLevel.best==73);
    assert(debug::DebugSpectateRuntime::get().terminals==1 && runtime.ends==0 && runtime.clears==0);
    recorder.levelComplete();
    assert(realRecordLevel.best==100&&realRecordLevel.scores==1&&debug::DebugSpectateRuntime::get().terminals==2);
#endif
    // Ordinary local/Ranked/Placement cleanup keeps GD saves enabled even after
    // PlayLayer::get() is cleared. Spectator cleanup runs vanilla in scoped
    // no-save mode and restores the flag; result guards stay independent.
    for(bool spectator : {false,true}){
        runtime={};runtime.serverSpectator=spectator;runtime.ownAlive=!spectator;
        CorumRankedPlayLayer teardown;SaveHook teardownLevel;
        teardown.m_level=&teardownLevel;teardown.m_player1=&local;teardown.percent=100;
        teardown.m_fields->attemptStartReported=!spectator;
        teardown.m_fields->attemptEndReported=true;
        currentLayer=&teardown;teardown.syncSpectatorLocalSafety();
        if(spectator){teardown.testDummyResetBoundary();
            assert(teardown.resets==1&&teardownLevel.scores==0&&!teardown.m_isTestMode);}
        teardown.onQuit();
        assert(teardown.quits==1&&!teardown.m_isTestMode);
        assert(teardownLevel.scores==(spectator?0:1));
        assert(teardownLevel.best==(spectator?41:100));
        CorumRankedPlayLayer::EntryOverlay entryOverlay;
        teardown.m_fields->entryOverlay=&entryOverlay;
        currentLayer=&teardown;teardown.onExit();
        assert(entryOverlay.dismissed==1 && teardown.m_fields->entryOverlay==nullptr);
        assert(teardown.exits==1&&!teardown.m_isTestMode);
        assert(teardownLevel.scores==(spectator?0:2));
        assert(runtime.ends==0);
    }
    // Native no-save flag restoration is exception-safe and nesting-safe.
    bool nativeMode=false;
    try{ScopedSpectatorPersistenceMode outer(nativeMode,true);assert(nativeMode);
        {ScopedSpectatorPersistenceMode inner(nativeMode,false);assert(nativeMode);}
        assert(nativeMode);throw 1;
    }catch(int){}
    assert(!nativeMode);
    nativeMode=true;{ScopedSpectatorPersistenceMode scope(nativeMode,false);assert(nativeMode);}assert(nativeMode);
    nativeMode=false;{ScopedSpectatorPersistenceMode ordinary(nativeMode,false);nativeMode=true;}assert(nativeMode);
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-native-safety-'));
    try {
        const file = join(dir, 'hooks.cpp');
        const binary = join(dir, 'hooks');
        writeFileSync(file, cpp);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', file, '-o', binary], { stdio: 'pipe' });
        execFileSync(binary, [], { stdio: 'pipe' });
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
            '-DCORUM_RANKED_DEBUG_SPECTATE=1', file, '-o', binary], { stdio: 'pipe' });
        execFileSync(binary, [], { stdio: 'pipe' });
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
});
