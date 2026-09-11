// Source invariants for the Round 3 FMOD transition hotfix. Native builds
// remain the ABI gate; affected-device traces remain the runtime proof.
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';
import test from 'node:test';

const source = readFileSync(
    fileURLToPath(new URL('../src/RankedAudioManager.cpp', import.meta.url)),
    'utf8',
);

const body = (from, to) => {
    const start = source.indexOf(from);
    const end = source.indexOf(to, start + from.length);
    assert(start >= 0 && end > start, `Missing source boundary: ${from} -> ${to}`);
    return source.slice(start, end);
};

test('Ranked streams open and seek through FMOD non-blocking state', () => {
    const prepared = body(
        'FMOD::Sound* RankedAudioManager::preparedSound(',
        'void RankedAudioManager::prewarmReadySounds()',
    );
    assert.match(prepared, /FMOD_CREATESTREAM\s*\|\s*FMOD_2D\s*\|\s*FMOD_NONBLOCKING/);
    assert.match(source, /FMOD_OPENSTATE_SETPOSITION|PreparedStreamState::Pending/);

    const start = body(
        'void RankedAudioManager::startDesiredAudio(',
        'void RankedAudioManager::updatePendingAudioStart()',
    );
    assert.match(start, /channel->setPosition\(m_pendingStartMs, FMOD_TIMEUNIT_MS\)/);
    assert(start.indexOf('m_pendingChannel = channel') < start.indexOf('channel->setPosition'));
    assert.doesNotMatch(start, /channel->setPaused\(false\)/);
    assert.doesNotMatch(start, /m_system->update\(\)/);
});

test('the old BGM survives until the asynchronous candidate is ready', () => {
    const request = body(
        'void RankedAudioManager::requestAudioSwitch()',
        'void RankedAudioManager::startDesiredAudio(',
    );
    const desired = request.slice(request.indexOf('if (desired) {'), request.indexOf('cancelPendingAudioStart();\n    if (m_rankedChannel)'));
    assert.match(desired, /m_switchPending = true;\s*startDesiredAudio\(\);/);
    assert.doesNotMatch(desired, /stopOwnedAudio\(\)/);

    const finish = body(
        'void RankedAudioManager::updatePendingAudioStart()',
        'void RankedAudioManager::cancelPendingAudioStart()',
    );
    assert.match(finish, /if \(state == PreparedStreamState::Pending\) return;/);
    assert(finish.indexOf('stopOwnedAudio()') < finish.indexOf('channel->setPaused(false)'));
    assert.match(finish, /audio-seek-ready:/);
});

test('competitive switches reuse validated paths and never force an FMOD update', () => {
    assert.match(source, /m_readySongPaths\.find\(songId\)/);
    assert.match(source, /m_readySongPaths\.emplace\(songId,/);
    assert.doesNotMatch(source, /m_system->update\(\)/);
});
