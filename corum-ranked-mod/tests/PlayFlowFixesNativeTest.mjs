import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';

const root = new URL('..', import.meta.url).pathname;
const read = path => readFileSync(join(root, path), 'utf8');

test('Solo pause is refused once 10 s or less remain on the round timer', () => {
    const dir = mkdtempSync(join(tmpdir(), 'corum-solo-pause-'));
    try {
        writeFileSync(join(dir, 'test.cpp'), String.raw`#include "${join(root, 'src/domain/RankedGameplayPolicy.hpp')}"
#include <cassert>
int main() {
    using namespace corum::ranked;
    assert(!soloPauseLockedAt(std::nullopt) && !soloPauseLockedAt(10'001) && !soloPauseLockedAt(300'000));
    assert(soloPauseLockedAt(10'000) && soloPauseLockedAt(1) && soloPauseLockedAt(0) && soloPauseLockedAt(-2'000));
}
`);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Werror', join(dir, 'test.cpp'), '-o', join(dir, 'test')], {stdio: 'pipe'});
        execFileSync(join(dir, 'test'), [], {stdio: 'pipe'});
    } finally { rmSync(dir, {recursive: true, force: true}); }
    const play = read('src/PlayLayer.cpp');
    const hook = play.slice(play.indexOf('bool soloPauseLocked() {'), play.indexOf('void onQuit() {'));
    // Solo gameplay only (not Survival, not while the entry holds the scene, not a spectator).
    assert.match(hook, /!m_fields->rankedLevel \|\| m_fields->survivalLevel \|\|\s*m_fields->nativeEntryHeld \|\| m_fields->spectatorMode \|\| m_isPaused\) return false;/);
    assert.match(hook, /300'000 \+ runtime\.signedDeadlineMillis\(\)\.value_or\(0\)/);
    assert.match(hook, /void pauseGame\(bool unfocused\) \{\s*if \(soloPauseLocked\(\)\) \{/);
    assert.match(hook, /PlayLayer::pauseGame\(unfocused\);/);
});

test('leaving the round after elimination returns to the lobby once the result is settled', () => {
    const runtime = read('src/SurvivalRuntime.cpp');
    assert.match(runtime, /void SurvivalRuntime::requestLeaveToMenu\(\)\{\s*if\(!active\(\) \|\| !m_view\.ownEliminated\)return;/);
    assert.match(runtime, /if\(leavingToMenu\(\) && \(m_view\.result\.isObject\(\) \|\| m_view\.state=="CANCELLED"\)\)acknowledge\(\);/);
    assert.match(runtime, /void SurvivalRuntime::leaveObserver\(\)\{\s*if\(!observing\(\)\)return;\s*requestLeaveToMenu\(\);/);
    assert.match(runtime, /if\(m_freePlay\.active && m_freePlay\.observer && m_view\.ownEliminated\)requestLeaveToMenu\(\);/);
    const board = read('src/ui/SurvivalGameLayer.cpp');
    assert.match(board, /else if\(runtime\.view\(\)\.ownEliminated\)runtime\.requestLeaveToMenu\(\);else confirmSurrender\(\);/);
    assert.match(board, /else SurvivalRuntime::get\(\)\.requestLeaveToMenu\(\);/);
    // The board closes to the Ranked lobby once the acknowledged match is gone.
    assert.match(board, /if\(!runtime\.active\(\) && !runtime\.creating\(\)\)\{m_closed=true;/);
});

test('the Survival collision manifest is built during the countdown, not on every restart', () => {
    const cbf = read('src/replay/SurvivalReplayCbfNative.cpp');
    assert.match(cbf, /if\(reset&&sourceRestart&&sameCollisionObjects\(layer,world\)\)\{world\.identities\.restartAttempt\(\);return;\}/);
    assert.match(cbf, /prepareCollisionWorld\(layer,newSourceAttempt,newSourceAttempt\);/);
    // Playback keeps re-proving the full manifest.
    assert.match(cbf, /std::string prepareCbfInitialCollisionLogs\([\s\S]*?prepareCollisionWorld\(layer,true\);/);
    const play = read('src/PlayLayer.cpp');
    assert.match(play, /if\(ownsScene && !std::exchange\(m_fields->collisionManifestPrewarmed,true\)\)\s*corum::ranked::replay::prewarmCbfCollisionWorld\(static_cast<PlayLayer\*>\(this\)\);/);
});
