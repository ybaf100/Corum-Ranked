import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
const read = p => readFileSync(new URL(`../${p}`, import.meta.url), 'utf8');
const runtime = read('src/debug/DebugSpectateRuntime.cpp');
const preparation = read('src/RankedLevelPreparation.cpp');
const play = read('src/PlayLayer.cpp');
const live = read('src/SpectatorLiveRuntime.cpp');
const level = read('src/LevelInfoLayer.cpp');
const server = p => readFileSync(new URL(`../../ranked/apps/server/src/${p}`, import.meta.url), 'utf8');

test('fixed VOICE constant and production cadence are shared; native verified download and song path are retained', () => {
    assert.match(read('src/debug/DebugSpectatePolicy.hpp'), /kDebugSpectateLevelId = 88350599/);
    assert.doesNotMatch(runtime + play + level, /88350599/);
    assert.match(read('src/domain/SpectatorProtocol.hpp'), /kSpectatorPublishIntervalMs = 50/);
    assert.match(live, /milliseconds\(corum::ranked::kSpectatorPublishIntervalMs\)/);
    assert.match(read('src/debug/DebugSpectatePolicy.hpp'), /time \+ kSpectatorPublishIntervalMs/);
    assert.match(level, /void prepareDebugSpectate\(float\)/);
    const gate = level.slice(level.indexOf('void prepareDebugSpectate('), level.indexOf('bool metadataReady()'));
    for (const text of ['authorizes(', 'LevelInfoLayer::downloadLevel()', 'songsReady(', 'LevelInfoLayer::onPlay(nullptr)']) assert(gate.includes(text));
    assert.doesNotMatch(runtime + gate, /GJGameLevel::create|m_levelString\s*=|getStoredLevel/);
});
test('TEST uses real HTTP viewer decoder/interpolation/renderer/camera; no local replay or pose injection', () => {
    assert.match(runtime, /startDebugViewer\(m_contextId, m_token/);
    assert.match(live, /request.get\(endpoint\(path\)\)/);
    assert.match(live, /path = m_debugPath \+ fmt::format/);
    assert.match(live, /applyViewerResponse\(root\)/);
    assert.match(live, /\.pose = interpolatedPose\(\)/);
    assert.match(play, /remotePlayer.update\([\s\S]*?live.presentationFrame\(\)/);
    assert.match(play, /remotePlayer.cameraFrame\(\)/);
    assert.doesNotMatch(runtime, /applyPose|applyViewerResponse|interpolatedPose|setPosition|setRotation|ifstream|readFile|recordedFrames|playbackClock/);
    assert.doesNotMatch(play, /m_player1->set(?:Position|Rotation)\(/);
});
test('verified Saved adoption and scene handoff precede native scene creation without trusting cached identity', () => {
    const adopt = preparation.slice(preparation.indexOf('bool RankedLevelPreparation::adoptVerifiedSavedLevel('), preparation.indexOf('void RankedLevelPreparation::invalidate('));
    assert(adopt.indexOf('authorizes(m_level.data(), context)') < adopt.indexOf('manager->getSavedLevel(context.levelId)'));
    for (const check of ['GJLevelType::Saved', 'm_levelString.empty()', 'm_levelNotDownloaded', 'authorizes(saved, context)', 'adopt(saved, context)']) assert(adopt.includes(check));
    assert.doesNotMatch(adopt, /\.attest\(|m_levelString\s*=|GJGameLevel::create/);
    const tick = runtime.slice(runtime.indexOf('void DebugSpectateRuntime::tick()'), runtime.indexOf('void DebugSpectateRuntime::sceneEntered('));
    assert(tick.indexOf('preparation.poll()') < tick.indexOf('adoptVerifiedSavedLevel('));
    assert(tick.indexOf('adoptVerifiedSavedLevel(') < tick.indexOf('LevelInfoLayer::scene('));
    assert(tick.indexOf('m_launched = true') < tick.indexOf('LevelInfoLayer::scene('));
    assert.match(read('src/debug/DebugSpectatePopup.cpp'), /cancelPopupPreparation\(\)/);
    assert.match(runtime, /if \(m_launched\) return;/);
    // Effective native/Jukebox path is required even if downloaded=true is stale.
    assert.match(level, /bool debugSpectateSongFilesReady\(/);
    assert.match(level, /!songsReady\(m_level, m_songWidget\) \|\| !debugSpectateSongFilesReady/);
    for (const transition of ['preparation metadata verified', 'using downloaded saved VOICE geometry', 'map ready', 'song acquisition requested song=', 'song ready path=', 'invoking vanilla Play', 'PlayLayer entered', 'RECORDING active']) assert((runtime + level + play).includes(transition));
});
test('RECORD remains non-Ranked vanilla; TEST activates Phase-1 safety before init and cannot reopen Practice', () => {
    assert.match(play, /if \(m_fields->debugRecordScene \|\| m_fields->debugPlaybackScene\) return false/);
    assert.match(play, /runtime.setSpectatorLocalTransportBlocked\(this, true\)/);
    assert(play.indexOf('debug.sceneEntered(m_fields->levelId)') < play.indexOf('PlayLayer::init(level, useReplay'));
    assert.match(play, /debugPlaybackScene && m_fields->localSafety.activate/);
    assert.match(play, /recordPose\(corum::ranked::captureSpectatorPose\(player, true, false, m_gameState.m_isDualMode\), true\)/);
    assert.match(play, /recordPose\(corum::ranked::captureSpectatorPose\(m_player1, false, true, m_gameState.m_isDualMode\), true\)/);
    for (const sink of ['localSafety.suppressDeath(', 'suppressLocalCompletion()', 'blocksLevelPersistence(', 'freezeAtCompletionBoundary(']) assert(play.includes(sink));
    assert.doesNotMatch(runtime, /reportAttempt|reportPractice|Achievement|updateRating|armCurrentLevelForGameplay/);
});
test('Official Debug OFF preprocessing removes UI and every debug API call; Debug CI opts in', () => {
    assert.match(read('CMakeLists.txt'), /option\(CORUM_RANKED_DEBUG_SPECTATE[^\n]* OFF\)/);
    assert.match(read('CMakeLists.txt'), /FILTER SOURCES EXCLUDE REGEX.*DebugSpectate/);
    assert.match(read('../.github/workflows/build-ranked-debug.yml'), /CORUM_RANKED_DEBUG_SPECTATE=ON/);
    assert.match(read('../.github/workflows/build-ranked-mod.yml'), /CORUM_RANKED_DEBUG_SPECTATE=OFF/);
    const dir = mkdtempSync(join(tmpdir(), 'corum-debug-off-'));
    try {
        const sources = ['src/debug/DebugSpectateRuntime.cpp', 'src/debug/DebugSpectatePopup.cpp', 'src/SpectatorLiveRuntime.cpp', 'src/PlayLayer.cpp', 'src/RankedPopup.cpp'];
        for (const path of sources) {
            const file = join(dir, 'off.cpp');writeFileSync(file, read(path).replace(/^#include.*$/gm, ''));
            const off = execFileSync('g++', ['-E', '-P', '-x', 'c++', file], { encoding: 'utf8' });
            assert(!off.includes('/api/ranked/debug/spectate/'), path);
            assert(!off.includes('RECORD SPECTATE') && !off.includes('TEST SPECTATE') && !off.includes('DEBUG SPECTATE'), path);
        }
    } finally { rmSync(dir, { recursive: true, force: true }); }
});
test('server persistence and Recorded Publisher are isolated from Ranked writes and retain production buffer/schema', () => {
    const service = server('debug-spectate/debug-spectate.service.ts');
    assert.match(service, /database.transaction/);
    assert.match(service, /jsonb_to_recordset/);
    assert.match(service, /WHERE completed AND level_id = \$1 AND format_version = \$2/);
    assert.match(service, /this.clock.now\(\).getTime\(\)/);
    assert.match(service, /relay.publishRecorded/);assert.match(service, /relay.subscribeRecorded/);
    assert.doesNotMatch(service, /(?:INSERT INTO|UPDATE|DELETE FROM) ranked_|\.emitDiscord|MatchService|QueueService|PlacementService|AchievementService/);
    const relay = server('match/spectator-live.service.ts');
    assert.match(relay, /this.appendPacket\(stream, packet, nowMs\)/);
    assert.match(relay, /this.readStream\(key, stream, 1, null/);
    assert.match(server('config/server-environment.ts'), /ENABLE_DEBUG_SPECTATE\?\.trim/);
});
