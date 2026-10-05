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
        'void playPlatformerEndAnimationToPos(', 'void showEndLayer()', 'void showNewBest(',
    ].map(signature => method(signature)).join('\n');
    const player = method('void update(float dt)', source.indexOf('class $modify(CorumRankedSpectatorDummyPlayer'));
    const saves = [method('void savePercentage('), method('void saveNewScore(')].join('\n');
    const cpp = `
#include "${join(root, 'src/domain/SpectatorLocalSafety.hpp')}"
#include "${join(root, 'src/domain/RankedGameplayPolicy.hpp')}"
#include <algorithm>
#include <cassert>
#include <memory>
#include <optional>
#include <string>
namespace gd { using string = std::string; }
namespace geode::log { template<class... T> void info(T...) {} }
using namespace geode;
struct CCPoint { float x, y; };
struct GameObject { int m_uniqueID = 0; };
struct PlayerObject { int updates = 0; void setVisible(bool) {} void update(float) { ++updates; } };
struct GJGameLevel {
    int best = 41, scores = 0;
    void savePercentage(int p, bool, int, int, bool) { best = std::max(best, p); }
    void saveNewScore(int, int, int, int, int, gd::string, bool) { ++scores; }
};
void persist(GJGameLevel*, int);
struct PlayLayer {
    PlayerObject* m_player1 = nullptr;
    GJGameLevel* m_level = nullptr;
    GameObject* m_anticheatSpike = nullptr;
    bool m_isTestMode = false, m_isPracticeMode = false;
    int deaths = 0, completions = 0, checks = 0, texts = 0, animations = 0, endLayers = 0, newBest = 0;
    double percent = 0;
    void destroyPlayer(PlayerObject*, GameObject*) { ++deaths; persist(m_level, int(percent)); }
    void levelComplete() { ++completions; persist(m_level, 100); }
    void showCompleteText() { ++texts; }
    void checkForEnd() { ++checks; }
    void playEndAnimationToPos(CCPoint) { ++animations; }
    void playPlatformerEndAnimationToPos(CCPoint, bool) { ++animations; }
    void showEndLayer() { ++endLayers; }
    void showNewBest(bool, int, int, bool, bool, bool) { ++newBest; }
};
namespace corum::ranked {
struct RankedRuntime {
    bool serverSpectator = false, ownAlive = true, placement = false;
    void const* owner = nullptr;
    int ends = 0, clears = 0;
    static RankedRuntime& get() { static RankedRuntime value; return value; }
    bool hasLocalGameplayAuthority() { return ownAlive; }
    bool isPlacement() { return placement; }
    bool isSpectating() { return spectatorAuthorityReady(serverSpectator, placement, ownAlive); }
    void setSpectatorLocalTransportBlocked(void const* p, bool b) { if (b) owner = p; else if (owner == p) owner = nullptr; }
    bool reportAttemptEnd(int, double, bool clear, std::optional<double>) {
        if (spectatorAttemptTransportBlocked(isSpectating(), owner != nullptr)) return false;
        ++ends; clears += clear ? 1 : 0; ownAlive = false; return true;
    }
};
}
struct ScopedPersistenceOwner { explicit ScopedPersistenceOwner(PlayLayer*) {} };
${source.slice(source.indexOf('struct ScopedRealGameplayEnd {'), source.indexOf('std::int64_t steadyNowMicros()'))}
double rankedProgressPercent(PlayLayer* layer) { return layer->percent; }
struct CorumRankedPlayLayer : PlayLayer {
    struct Fields {
        bool safetyRetired = false, vanillaGameplayEndInProgress = false;
        bool attemptStartReported = true, attemptEndReported = false;
        bool rankedLevel = true, rankedInitialized = true, rankedPractice = false;
        bool spectatorMode = false, spectatorPending = false;
        bool suppressedDeathLogged = false, suppressedCompletionLogged = false, suppressedSaveLogged = false;
        int levelId = 123; double qualifyingPercent = 50;
        corum::ranked::SpectatorLocalSafety localSafety;
    };
    std::unique_ptr<Fields> m_fields = std::make_unique<Fields>();
    ${callbacks}
};
CorumRankedPlayLayer* currentLayer = nullptr;
CorumRankedPlayLayer* localSafetyLayer() { return currentLayer; }
struct PlayerHook : PlayerObject { ${player} };
struct SaveHook : GJGameLevel { ${saves} };
void persist(GJGameLevel* level, int percent) {
    if (level) static_cast<SaveHook*>(level)->savePercentage(percent, false, 0, 0, true);
}
int main() {
    using corum::ranked::RankedRuntime;
    auto& runtime = RankedRuntime::get();
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
    level.savePercentage(100, false, 0, 0, true);
    level.saveNewScore(100, 0, 0, 0, 0, "", true);
    assert(scene.completions == 0 && scene.texts == 0 && scene.checks == 0 && scene.animations == 0);
    assert(scene.endLayers == 0 && scene.newBest == 0 && runtime.clears == 0);
    assert(level.best == 83 && level.scores == 0 && !scene.m_isPracticeMode);
    unrelated.savePercentage(95, false, 0, 0, true);
    assert(unrelated.best == 95);
    // Fresh normal scene has all original callbacks and persistence intact.
    CorumRankedPlayLayer normal;
    SaveHook normalLevel;
    currentLayer = &normal; normal.m_player1 = &local; normal.m_level = &normalLevel;
    runtime = {}; normal.percent = 100; local.update(0.01f);
    normal.levelComplete(); normal.showCompleteText(); normal.checkForEnd();
    normal.showNewBest(false, 0, 0, false, false, false);
    assert(normal.completions == 1 && normal.texts == 1 && normal.checks == 1 && normal.newBest == 1);
    assert(normalLevel.best == 100 && runtime.clears == 1 && local.updates == 1);
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-native-safety-'));
    try {
        const file = join(dir, 'hooks.cpp');
        const binary = join(dir, 'hooks');
        writeFileSync(file, cpp);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', file, '-o', binary], { stdio: 'pipe' });
        execFileSync(binary, [], { stdio: 'pipe' });
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
});
