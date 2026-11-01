import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const read = (path) => readFileSync(new URL(`../${path}`, import.meta.url), 'utf8');

test('replays stay playable across Corum Ranked client updates (no mod-version gate)', () => {
    const runtime = read('src/replay/SurvivalReplayRuntime.cpp');
    const identity = runtime.slice(runtime.indexOf('for(auto key:{"matchId"'), runtime.indexOf('throw std::runtime_error("Replay does not match'));
    assert.match(identity, /"attemptId"/);
    assert.doesNotMatch(identity, /modVersion|geodeVersion|gameVersion/);
    const compatibility = read('src/replay/SurvivalReplayCompatibility.hpp');
    assert.doesNotMatch(compatibility, /modVersion|hwanhee1\.corum_ranked/);
    const quality = read('src/replay/SurvivalReplayQuality.hpp');
    const classify = quality.slice(quality.indexOf('inline ReplayQualityView classifyReplayQuality'));
    assert.doesNotMatch(classify, /modVersion/);
    // The native boundary hash is CBF's binary, never this mod's own binary.
    const cbf = read('src/replay/SurvivalReplayCbfNative.cpp');
    assert.match(cbf, /binaryPath=mod->getBinaryPath\(\)/);
    assert.doesNotMatch(cbf, /binaryPath=Mod::get\(\)->getBinaryPath\(\)/);
});
