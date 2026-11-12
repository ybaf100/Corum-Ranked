import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join, resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = path => readFileSync(join(root, path), 'utf8');

test('desktop replay keys mirror the on-screen controls and clamp speed steps', () => {
    const dir = mkdtempSync(join(tmpdir(), 'corum-replay-keys-'));
    try {
        writeFileSync(join(dir, 'test.cpp'), `
#include "${join(root, 'src/replay/SurvivalReplayShortcuts.hpp')}"
#include <cassert>
using namespace corum::ranked::replay;
static_assert(replayShortcut(0x20) == ReplayShortcut::TogglePause && replayShortcut(0x4B) == ReplayShortcut::TogglePause);
static_assert(replayShortcut(0x25) == ReplayShortcut::Back10 && replayShortcut(0x27) == ReplayShortcut::Forward10);
static_assert(replayShortcut(0x52) == ReplayShortcut::Restart && replayShortcut(0x24) == ReplayShortcut::Restart);
static_assert(replayShortcut(0x26) == ReplayShortcut::Faster && replayShortcut(0x28) == ReplayShortcut::Slower);
// W, A, D, Escape and P stay with GD (jump, platformer, pause).
static_assert(replayShortcut(0x57) == ReplayShortcut::None && replayShortcut(0x41) == ReplayShortcut::None);
static_assert(replayShortcut(0x44) == ReplayShortcut::None && replayShortcut(0x1B) == ReplayShortcut::None);
static_assert(replayShortcut(0x50) == ReplayShortcut::None);
int main() {
    assert(replaySpeedStep(1, 1) == 2 && replaySpeedStep(2, 1) == 4 && replaySpeedStep(4, 1) == 4);
    assert(replaySpeedStep(1, -1) == .5 && replaySpeedStep(.5, -1) == .5 && replaySpeedStep(4, -1) == 2);
    assert(replaySpeedStep(3, 1) == 2); // an unknown speed restarts from 1x
}
`);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', join(dir, 'test.cpp'), '-o', join(dir, 'test')], {stdio: 'pipe'});
        execFileSync(join(dir, 'test'), [], {stdio: 'pipe'});
    } finally { rmSync(dir, {recursive: true, force: true}); }
});

test('replay keys are consumed only by an owned replay scene and otherwise reach GD unchanged', () => {
    const source = read('src/replay/SurvivalReplayNative.cpp');
    const hook = source.slice(source.indexOf('class $modify(CorumSurvivalReplayKeys,UILayer)'));
    assert.match(hook, /void keyDown\(cocos2d::enumKeyCodes key,double timestamp\)\{if\(!replayKey\(key,true\)\)UILayer::keyDown\(key,timestamp\);\}/);
    assert.match(hook, /void keyUp\(cocos2d::enumKeyCodes key,double timestamp\)\{if\(!replayKey\(key,false\)\)UILayer::keyUp\(key,timestamp\);\}/);
    assert.match(hook, /static_cast<GJBaseGameLayer\*>\(play\)!=m_gameLayer\)return false/);
    const method = source.slice(source.indexOf('bool replayShortcut(int key,bool down){'), source.indexOf('void onReplaySeek('));
    // Unmapped keys never touch scene fields; live spectating and Pause keep GD's keys.
    assert.match(method, /action==ReplayShortcut::None\|\|!m_fields->replayOwned\|\|!m_fields->ready\|\|m_isPaused/);
    assert.match(method, /if\(!held\.insert\(key\)\.second\)return true;/);
    assert.match(source, /if\(m_fields->replayOwned&&m_isPaused\)\{m_fields->replayHeldKeys\.clear\(\);/);
});
