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

test('FMOD NOTREADY keeps one pending candidate and retries seek later', () => {
    const start = body(
        'void RankedAudioManager::startDesiredAudio(',
        'void RankedAudioManager::updatePendingAudioStart()',
    );
    const seek = start.slice(start.indexOf('if (m_pendingStartMs > 0)'));
    assert.match(seek, /seekResult == FMOD_ERR_NOTREADY/);
    const notReady = seek.slice(
        seek.indexOf('if (seekResult == FMOD_ERR_NOTREADY)'),
        seek.indexOf('if (seekResult != FMOD_OK)'),
    );
    assert.doesNotMatch(notReady, /cancelPendingAudioStart\(\)/);
    assert.match(notReady, /audio-seek-deferred/);
    assert.match(notReady, /audioStartTimedOut\(\)/);

    const update = body(
        'void RankedAudioManager::updatePendingAudioStart()',
        'void RankedAudioManager::cancelPendingAudioStart()',
    );
    assert.match(update, /m_pendingStartMs > 0 && !m_pendingSeekAccepted/);
    assert.match(update, /m_pendingChannel->setPosition\(m_pendingStartMs, FMOD_TIMEUNIT_MS\)/);
    assert.match(update, /seekResult == FMOD_ERR_NOTREADY/);
    assert.match(update, /failDesiredAudio\("seek-notready-timeout"/);
    assert.match(update, /m_pendingSeekAccepted = true/);
});

test('stalled pending transitions time out and fall back without killing the old BGM', () => {
    assert.match(source, /kPendingAudioStartTimeout\s*=\s*std::chrono::seconds\(3\)/);
    assert.match(source, /RankedAudioMode pendingTimeoutFallback\(RankedAudioMode mode\)/);

    const update = body(
        'void RankedAudioManager::updatePendingAudioStart()',
        'void RankedAudioManager::cancelPendingAudioStart()',
    );
    const pending = update.slice(
        update.indexOf('if (state == PreparedStreamState::Pending)'),
        update.indexOf('if (state == PreparedStreamState::Error)'),
    );
    assert.match(pending, /audioStartTimedOut\(\)/);
    assert.match(pending, /audio-pending-timeout/);
    assert.match(pending, /failDesiredAudio\("buffer-seek-timeout"/);
    assert.doesNotMatch(pending, /stopOwnedAudio\(\)/);
    const failure = body('void RankedAudioManager::failDesiredAudio(', 'void RankedAudioManager::retireOwnedChannel(');
    assert.match(failure, /m_failedPlaybackKeys\.insert\(resource->key\)/);
    assert.match(failure, /m_desiredMode = availableMode\(m_requestedMode\)/);
    assert.doesNotMatch(failure, /sound->release\(\)|stopOwnedAudio\(\)/);

    const fallback = body(
        'RankedAudioMode pendingTimeoutFallback(',
        '} // namespace',
    );
    assert.match(fallback, /Round3DeathmatchWait/);
    assert.match(fallback, /return RankedAudioMode::Round12/);
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
    assert.match(finish, /state == PreparedStreamState::Pending/);
    assert.match(finish, /audioStartTimedOut\(\)/);
    assert(finish.indexOf('channel->setPaused(false)') < finish.indexOf('retireOwnedChannel('));
    assert.match(finish, /channel->addFadePoint\(clock, 0\)/);
    assert.match(finish, /channel->addFadePoint\(clock \+.*fadeIn \* rate\), 1\)/);
    assert.match(finish, /audio-seek-ready:/);
});

test('competitive switches reuse validated paths and never force an FMOD update', () => {
    assert.match(source, /m_readySongPaths\.find\(songId\)/);
    assert.match(source, /m_readySongPaths\.emplace\(songId,/);
    assert.doesNotMatch(source, /m_system->update\(\)/);
});
