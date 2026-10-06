import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const read = path => readFileSync(new URL(path, import.meta.url), 'utf8');
const play = read('../src/PlayLayer.cpp');
const runtime = read('../src/RankedRuntime.cpp');
const ux = read('../src/GameplayUx.cpp');
const policy = read('../src/domain/SpectatorLocalSafety.hpp');
const debugWorkflow = read('../../.github/workflows/build-ranked-debug.yml');

function body(source, signature) {
    const at = source.indexOf(signature);
    assert(at >= 0, `Missing ${signature}`);
    const begin = source.indexOf('{', at);
    let depth = 1;
    for (let i = begin + 1; i < source.length; ++i) {
        if (source[i] === '{') ++depth;
        if (source[i] === '}' && --depth === 0) return source.slice(begin + 1, i);
    }
    assert.fail(`Unterminated ${signature}`);
}

test('authority activates before vanilla init; live own Attempt and user Practice exclude it', () => {
    const init = body(play, 'bool init(GJGameLevel* level');
    assert(init.indexOf('syncSpectatorLocalSafety()') < init.indexOf('PlayLayer::init('));
    const sync = body(play, 'void syncSpectatorLocalSafety()');
    for (const evidence of ['runtime.isSpectating()', 'runtime.hasLocalGameplayAuthority()',
        'runtime.isPlacement()', 'm_isTestMode', 'm_fields->rankedPractice || m_isPracticeMode',
        'm_fields->attemptStartReported && !m_fields->attemptEndReported']) assert(sync.includes(evidence));
    assert.match(body(runtime, 'bool RankedRuntime::isSpectating()'), /spectatorAuthorityReady/);
    assert.match(body(runtime, 'bool RankedRuntime::hasLocalGameplayAuthority()'), /hasLocalOpenAttempt\(\) \|\| m_gameplayStartEligible/);
});

test('destroyPlayer noclip is exact local dummy only; normal vanilla death still runs', () => {
    const death = body(play, 'void destroyPlayer(PlayerObject* player');
    assert.match(death, /localSafety\.suppressDeath\(player && player == m_player1\)/);
    assert.doesNotMatch(death, /if \(runtime\.isSpectating\(\) \|\| m_fields->spectatorPending\)/);
    assert.match(death, /scoringEnabled\(\).*reportAttemptEnd/s);
    assert.match(death, /PlayLayer::destroyPlayer\(player, object\)/);
    assert.match(policy, /return m_active && exactLocalPlayer/);
});

test('99 safety freezes only PlayerObject update, not the PlayLayer or remote world', () => {
    const player = play.slice(play.indexOf('class $modify(CorumRankedSpectatorDummyPlayer'));
    assert.match(body(player, 'void update(float dt)'), /freezeLocalDummy\(this\)\) return;[\s\S]*PlayerObject::update\(dt\)/);
    assert.match(body(play, 'bool freezeLocalDummy('), /player != m_player1\) return false/);
    assert.match(policy, /progress >= 99\.0/);
    assert.doesNotMatch(body(play, 'bool freezeLocalDummy('), /onQuit\(|pauseGame\(|resetLevel\(/);
    assert.match(body(play, 'void postUpdate(float dt)'), /PlayLayer::postUpdate\(dt\)/);
});

test('vanilla completion has independent guards before every relevant original/evidence path', () => {
    for (const signature of ['void checkForEnd()', 'void levelComplete()', 'void showCompleteText()',
        'void playEndAnimationToPos(', 'void playPlatformerEndAnimationToPos(', 'void showEndLayer()']) {
        const callback = body(play, signature);
        assert.match(callback, /if \(suppressLocalCompletion\(\)\) return;/);
        assert(callback.indexOf('suppressLocalCompletion()') < callback.indexOf('PlayLayer::'));
        if (callback.includes('reportAttemptEnd(')) assert(callback.indexOf('suppressLocalCompletion()') < callback.indexOf('reportAttemptEnd('));
    }
});

test('New Best and both percentage/record persistence sinks use explicit exact-object guard', () => {
    assert.match(body(play, 'void showNewBest('), /if \(blocksLocalEvidence\(\)\) return;/);
    const exact = body(play, 'bool blocksLevelPersistence(');
    assert.match(exact, /level && level == m_level/);
    assert.doesNotMatch(exact, /m_levelID/);
    for (const signature of ['void savePercentage(', 'void saveNewScore(']) {
        const save = body(play, signature);
        assert.match(save, /if \(layer && layer->blocksLevelPersistence\(this\)\) return;/);
        assert(save.indexOf('blocksLevelPersistence(this)') < save.indexOf('GJGameLevel::'));
        assert.doesNotMatch(save, /m_isPracticeMode|m_isTestMode/);
    }
});

test('quit/reset keep exact owner available even when vanilla clears PlayLayer::get()', () => {
    for (const signature of ['void onQuit()', 'void onExit()', 'void resetLevel()']) {
        assert.match(body(play, signature), /ScopedPersistenceOwner persistence\(this\)/);
    }
    assert.match(play, /persistenceOwner \? persistenceOwner : PlayLayer::get\(\)/);
    const exit = body(play, 'void onExit()');
    assert(exit.indexOf('PlayLayer::onExit()') < exit.indexOf('setSpectatorLocalTransportBlocked(this, false)'));
    assert.match(exit, /safetyRetired = true/);
    assert.doesNotMatch(exit, /localSafety\.deactivate\(/);
});

test('the final genuine death/Clear save finishes before dummy conversion', () => {
    assert.match(body(play, 'void syncSpectatorLocalSafety()'), /vanillaGameplayEndInProgress \|\|/);
    assert.match(body(play, 'bool blocksLevelPersistence('), /vanillaGameplayEndInProgress && !m_fields->localSafety\.active\(\)\) return false/);
    for (const signature of ['void destroyPlayer(', 'void levelComplete()', 'void showCompleteText()', 'void onQuit()', 'void resetLevel()']) {
        assert.match(body(play, signature), /ScopedRealGameplayEnd finishing/);
    }
    assert.match(body(play, 'bool scoringEnabled()'), /m_fields->rankedInitialized &&/);
});

test('runtime firewall precedes armed Start/self-healing Clear and does not drop prior FIFO', () => {
    for (const signature of ['bool RankedRuntime::reportAttemptStart(', 'void RankedRuntime::reportAttemptProgress(', 'bool RankedRuntime::reportAttemptEnd(', 'void RankedRuntime::flushProgressTelemetry()', 'void RankedRuntime::sendAttemptProgress()']) {
        const transport = body(runtime, signature);
        assert.match(transport.trimStart(), /^if \(spectatorAttemptTransportBlocked\(isSpectating\(\), m_spectatorLocalTransportOwner != nullptr\)\) return/);
    }
    assert.match(body(runtime, 'void RankedRuntime::reportPracticePresence('), /isSpectating\(\) \|\| m_spectatorLocalTransportOwner != nullptr/);
    assert.match(body(runtime, 'void RankedRuntime::setSpectatorLocalTransportBlocked('), /m_progressRequest\.cancel\(\)/);
    assert.doesNotMatch(body(runtime, 'void RankedRuntime::flushAttemptEvents()'), /spectatorAttemptTransportBlocked|m_spectatorLocalTransportOwner/);
    assert.match(body(runtime, 'double RankedRuntime::localDisplayScore('), /m_spectatorLocalTransportOwner != nullptr\) progressPercent = -1\.0/);
});

test('pending handoff blocks transport before vanilla reset without enabling early noclip', () => {
    const reset = body(play, 'void resetLevel()');
    const pending = reset.slice(reset.indexOf('m_fields->spectatorPending = true;'));
    assert(pending.indexOf('syncSpectatorLocalSafety()') < pending.indexOf('PlayLayer::resetLevel()'));
    assert.match(body(play, 'void syncSpectatorLocalSafety()'), /localSafety\.blocksEvidence\(m_fields->spectatorPending\)/);
    assert.doesNotMatch(body(policy, 'bool suppressDeath('), /handoffPending/);
});

test('Phase 1 never overwrites local dummy pose with opponent and preserves polling HUD', () => {
    const update = body(play, 'void postUpdate(float dt)');
    assert.match(update, /if \(m_fields->spectatorMode\) \{[\s\S]*?return;/);
    const spectator = body(play, 'void updateSpectatorView(float dt)');
    assert.doesNotMatch(spectator, /m_player1->setPosition\(|m_player1->setRotation\(|m_player1->setVisible\(true\)/);
    assert.match(body(play, 'bool spectatorCameraActive()'), /localSafety\.active\(\)/);
    assert.match(body(play, 'bool beginSpectatorCameraFrame()'), /remotePlayer\.update/);
    assert.doesNotMatch(spectator, /updateCamera\(|driveSpectatorCamera/);
    assert.match(spectator, /m_player1->setVisible\(false\)/);
    assert.match(spectator, /updateRankedHud\(\)/);
    assert.doesNotMatch(spectator, /reportAttemptStart\(|reportAttemptProgress\(|reportAttemptEnd\(/);
});

test('safety is independent of Practice flag and converted scene never becomes scoring again', () => {
    assert.doesNotMatch(ux, /CorumRankedSpectatorPracticeGuard|m_isPracticeMode\s*=\s*true/);
    assert.doesNotMatch(play, /m_fields->spectatorMode\s*=\s*false/);
    assert.match(body(play, 'bool scoringEnabled()'), /!m_fields->spectatorMode[\s\S]*!m_fields->spectatorPending/);
    assert.match(body(play, 'void togglePracticeMode('), /practiceTransition[\s\S]*PlayLayer::togglePracticeMode\(true\)/);
});

test('transition logs are bounded and automatic Debug CI includes both regression layers', () => {
    for (const guard of ['suppressedDeathLogged', 'suppressedCompletionLogged', 'suppressedSaveLogged']) {
        assert.match(play, new RegExp(`if \\((?:blocked && )?!m_fields->${guard}\\)`));
    }
    assert.match(debugWorkflow, /SpectatorLocalSafetyTest\.cpp/);
    assert.match(debugWorkflow, /SpectatorLocalSafetySourceTest\.mjs/);
});
