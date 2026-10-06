import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
const read = p => readFileSync(new URL(`../${p}`, import.meta.url), 'utf8');
const play = read('src/PlayLayer.cpp');
const camera = read('src/spectator/CorumSpectatorCamera.cpp');
const end = read('src/EndLevelLayer.cpp');

test('camera final adapter moves render transforms; target override stays scoped and separate', () => {
    assert.match(camera, /cameraMoveX\(target.x, 0.0f/);
    assert.match(camera, /cameraMoveY\(target.y, 0.0f/);
    assert.match(play, /class \$modify\(CorumRankedSpectatorCameraHook, GJBaseGameLayer\)/);
    assert.match(play, /GJBaseGameLayer::updateCamera\(dt\)/);
    assert.match(play, /remotePlayer.cameraFrame\(\)/);
    assert.doesNotMatch(camera.replace(/\/\/[^\n]*/g, ''), /m_player[12]|reportAttempt|destroyPlayer\(|levelComplete\(/);
    assert.doesNotMatch(play, /m_player1->set(?:Position|Rotation)\(|m_player1\s*=/);
});
test('single camera owner applies native before final correction with no viewer duplicate', () => {
    const hook=play.slice(play.indexOf('class $modify(CorumRankedSpectatorCameraHook'));
    assert(hook.indexOf('GJBaseGameLayer::updateCamera(dt);\n        }') < hook.indexOf('owner->driveSpectatorCamera()'));
    assert.match(hook, /ScopedCameraTargetOverride/);
    const view=play.slice(play.indexOf('void updateSpectatorView('),play.indexOf('void spectatorFrame('));
    assert.doesNotMatch(view, /updateCamera\(|driveSpectatorCamera|remotePlayer.update/);
    assert.match(play, /getTotalFrames\(\)/);
    assert.match(camera, /nodeToParentTransform\(\)/);assert.match(camera, /node->setPosition/);
    assert.match(camera, /updateCameraBGArt/);
    const scoped=read('src/spectator/ScopedCameraTargetOverride.hpp');
    assert.match(scoped, /~ScopedCameraTargetOverride/);assert.match(scoped, /m_positionX = m_x/);
    assert.match(scoped, /cocos2d::CCNode::setPosition/);
});
test('same-scene viewer heartbeat owns entry/exit independently of stopped vanilla gameplay', () => {
    assert.match(play, /schedule\(schedule_selector\(CorumRankedPlayLayer::spectatorFrame\)\)/);
    assert.doesNotMatch(play, /bool watchableSpectatorScene\(\) const/);
    assert.match(play, /if \(!prepareSpectatorScene\(\)\) return/);
    assert.match(play, /auto spectatorAuthority = runtime.isSpectating\(\)/);
    assert.match(play, /if \(!spectatorAuthority \|\| !watchableSpectatorScene\(\)\)/);
    assert.match(play, /m_fields->spectatorScenePrepared\) return/);
    assert.match(play, /frozen \|\| \(m_fields->rankedInitialized && m_fields->localSafety.active\(\)\)/);
    assert.match(end, /Ref<EndLevelLayer> retained\(this\);\s*if \(!corum::ranked::prepareSpectatorScene\(m_playLayer\)\) return;/);
    assert.match(end, /WeakRef<EndLevelLayer> rankedCompletionOverlay/);
    assert.match(end, /overlay->m_playLayer != layer/);
    assert.match(play, /if \(!corum::ranked::quitThroughRankedCompletionOverlay\(this\)\) onQuit\(\)/);
    assert.match(play, /WAITING\.\.\./);
    assert.match(play, /POLLING\.\.\./);
    assert.doesNotMatch(play, /m_isPracticeMode\s*=\s*true/);
    const cleanup = play.slice(play.indexOf('void cleanupSpectatorPresentation()'), play.indexOf('void updateSpectatorView('));
    assert(cleanup.indexOf('spectatorCamera.cleanup(this)') < cleanup.indexOf('remotePlayer.cleanup()'));
});
