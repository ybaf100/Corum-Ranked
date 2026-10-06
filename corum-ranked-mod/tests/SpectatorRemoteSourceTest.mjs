import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
const read = path => readFileSync(new URL(`../${path}`, import.meta.url), 'utf8');
const play = read('src/PlayLayer.cpp');
const remote = read('src/spectator/CorumRemotePlayer.cpp');
const live = read('src/SpectatorLiveRuntime.cpp');

test('dedicated visible renderer stays separate from scoped hidden world driver', () => {
    assert.match(remote, /PlayerObject::create\(1, 1, layer, root, true\)/);
    assert.match(remote, /player == layer->m_player1 \|\| player == layer->m_player2/);
    assert.match(remote, /m_player->setPosition\(\{pose.x, pose.y\}\)/);
    assert.match(remote, /m_player->setRotation\(pose.rotation\)/);
    assert.match(play, /spectatorWorldDriver.apply\(this, spectatorCameraActive\(\), rendered\)/);
    const driver = read('src/spectator/SpectatorWorldDriver.cpp');
    assert.match(driver, /ScopedSpectatorWorldDriverWrite scope\(m_player\)/);
    assert.match(driver, /m_player = layer->m_player1/);
    assert.match(driver, /m_player->setPosition\(\{pose.x, pose.y\}\)/);
    assert.doesNotMatch(driver, /reportAttempt|savePercentage|saveNewScore|levelComplete|spiderTestJump|PlayerObject::update\(/);
    assert.match(play, /auto spectatorAuthority = runtime.isSpectating\(\)/);
    assert.match(play, /remotePlayer.update\(this, true, m_fields->spectatorRenderedFrame\)/);
    assert.match(play, /SpectatorLiveRuntime::get\(\).presentationFrame\(\)/);
});
test('remote renderer cannot invoke scoring, physics, death, completion or camera', () => {
    assert.doesNotMatch(remote, /(?:reportAttempt|levelComplete\(|destroyPlayer\(|resetLevel\(|onQuit\(|savePercentage\(|saveNewScore\(|updateCamera\(|PlayerObject::update\()/);
    assert.match(remote, /unscheduleAllSelectors\(\)/);
    assert.match(remote, /isolateNativeCompanions\(player, effectsRoot\)/);
    assert.match(remote, /discardNativeCompanions\(m_player.data\(\)\)/);
    assert.match(remote, /if \(!frame \|\| !frame->pose\) \{\s*m_poseCurrent = false;\s*setVisible\(false\)/);
});
test('level and spectator exit clean up before vanilla teardown', () => {
    assert.match(play, /void onQuit\(\) \{\s*m_fields->spectatorPresentationRetired = true;\s*unschedule\([\s\S]*?cleanupSpectatorPresentation\(\)/);
    const exit = play.slice(play.indexOf('void onExit()'));
    assert(exit.indexOf('cleanupSpectatorPresentation()') < exit.indexOf('PlayLayer::onExit()'));
    assert.match(play, /cleanupSpectatorPresentation\(\);\s*live.reset\(\);\s*if \(!m_fields->autoExitRequested\)/);
    assert.match(read('src/spectator/CorumRemotePlayer.hpp'), /~CorumRemotePlayer\(\)/);
});
test('typed live frame preserves server identity and a local reset epoch', () => {
    const frame = live.slice(live.indexOf('SpectatorLiveRuntime::presentationFrame()'));
    for (const field of ['matchId', 'roundNumber', 'deathmatchSequence', 'levelId', 'attemptId', 'generation', 'epoch']) {
        assert.match(frame, new RegExp(`\\.${field} =`));
    }
    assert.match(frame, /ranked.isPlacement\(\)/);
    assert.match(frame, /!ranked.isSpectating\(\)/);
    assert.match(frame, /m_viewContextKey != viewerContextKey\(match\)/);
    assert.match(live, /if \(streamChanged \|\| resetStream\) \{\s*\+\+m_viewPresentationEpoch/);
    assert.match(live, /m_sampleBuffer.render\(nowMs\)/);
    assert.doesNotMatch(live, /m_previousPose|m_poseReceivedAt|elapsed\) \/ 50.0f/);
});
test('MIT attribution is packaged and new renderer participates in native compile', () => {
    assert.match(read('CMakeLists.txt'), /GLOB_RECURSE[\s\S]*?src\/\*\.cpp/);
    assert(JSON.parse(read('mod.json')).resources.files.includes('resources/GLOBED-LICENSE.txt'));
    assert.match(read('resources/GLOBED-LICENSE.txt'), /Copyright \(c\) 2023 dank_meme/);
});
