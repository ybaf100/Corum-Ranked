// Integration invariants complement the compiled policy tests. They do NOT
// substitute for native Geode builds or reproduction on Android / Windows.
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';
import test from 'node:test';

const read = (file) => readFileSync(fileURLToPath(new URL(file, import.meta.url)), 'utf8');
const layer = read('../src/LevelInfoLayer.cpp');
const popup = read('../src/RankedPopup.cpp');
const resolver = read('../src/RankedLevelPreparation.cpp');
const policy = read('../src/domain/LevelPreparationPolicy.hpp');
const play = read('../src/PlayLayer.cpp');
const end = read('../src/EndLevelLayer.cpp');
const spectator = read('../src/SpectatorLiveRuntime.cpp');
const runtime = read('../src/RankedRuntime.cpp');
const runtimeHeader = read('../src/RankedRuntime.hpp');
const body = (source, from, to) => {
    const start = source.indexOf(from);
    const end = source.indexOf(to, start + from.length);
    assert(start >= 0 && end > start, `Missing source boundary: ${from} -> ${to}`);
    return source.slice(start, end);
};

test('both scene entry paths use the shared metadata authority, no raw cache fallback', () => {
    assert.match(body(popup, 'GJGameLevel* findPlayableMap()', 'bool exactLevelLookupStateAllowed()'), /findLevelInfoMap\(\)/);
    assert.match(body(popup, 'GJGameLevel* findLevelInfoMap()', 'float preparationProgressTarget()'), /RankedLevelPreparation::get\(\)\.level\(\)/);
    assert(!/->getSavedLevel\(|GJGameLevel::create\(/.test(popup), 'Raw saved/synthetic scene input returned');
    assert.match(body(layer, 'bool showRankedSongDownloadGate(', '} // namespace corum::ranked'), /authorizes\(level, context\)/);
    const init = body(layer, 'bool init(GJGameLevel* level', 'bool isCurrentRankedScene()');
    assert(init.indexOf('authorizes(level, context)') < init.indexOf('LevelInfoLayer::init(level, challenge)'));
    assert.match(init, /authorizes\(m_level, context\)/);
});

test('native response lifetime, immutable context and no same-key timeout overlap', () => {
    assert.match(resolver, /m_ticket\.finish\(currentPreparationContext\(\)\)/);
    assert.match(resolver, /m_proof\.attest\(currentPreparationContext\(\), metadata\)/);
    assert.match(resolver, /m_proof\.matches\(context, levelMetadata\(level\)\)/);
    assert.match(layer, /\.adopt\(m_level, m_fields->context\)/);
    assert.match(resolver, /responseCompleted\(tag\.c_str\(\), generation\)/);
    const busy = body(resolver, 'if (m_ticket.busy()) {', 'if (now < m_nextAttempt)');
    assert.doesNotMatch(busy, /getOnlineLevels/);
    assert.match(resolver, /currentStageIndex/);
    assert.match(resolver, /currentMapIndex/);
    assert.match(policy, /m_discarded/);
});

test('exact lookup coexists with a foreign singleton delegate without waiting forever', () => {
    const poll = body(
        resolver,
        'void RankedLevelPreparation::poll()',
        'std::uint64_t RankedLevelPreparation::requestGeneration',
    );
    assert.match(poll, /manager->getOnlineLevels\(search\)/);
    assert.doesNotMatch(poll, /WAITING FOR GD LEVEL LOOKUP/);
    assert.doesNotMatch(poll, /m_levelManagerDelegate\s*=\s*this/);

    const hook = resolver.slice(resolver.indexOf('class $modify(CorumRankedMetadataResponse'));
    assert.match(hook, /auto\* previousDelegate = m_levelManagerDelegate/);
    assert.match(hook, /m_levelManagerDelegate = &preparation/);
    assert.match(hook, /m_levelManagerDelegate = previousDelegate/);
    assert(hook.indexOf('requestGeneration(tag.c_str())') < hook.indexOf('m_levelManagerDelegate = &preparation'));
    const routed = hook.slice(hook.lastIndexOf('auto* previousDelegate = m_levelManagerDelegate'));
    assert(routed.indexOf('m_levelManagerDelegate = &preparation') < routed.indexOf('GameLevelManager::onGetOnlineLevelsCompleted(response, tag)'));
});

test('metadata gates geometry, RobTop readiness and post-download/update live objects', () => {
    const songs = body(layer, 'bool songsReady(', 'bool isCurrentRankedLevel(');
    assert(songs.indexOf('authorizes(') < songs.indexOf('m_isRobtopSong'));
    assert.match(songs, /if \(songWidget && songWidget->m_isRobtopSong\) return true;/);
    assert.match(layer, /metadataReady\(\) && mapReady\(m_level\) && !LevelInfoLayer::shouldDownloadLevel\(\)/);
    for (const event of ['init-before', 'init-after', 'download-callback', 'download-live-after', 'update-callback', 'update-live-after', 'play-activate']) {
        assert(layer.includes(`"${event}"`));
    }
    assert.match(layer, /recoverMetadata\("download-metadata-invalid"\)/);
    assert.match(layer, /recoverMetadata\("update-metadata-invalid"\)/);
});

test('only original settings button is exempt, validated by owner and native callback', () => {
    assert.doesNotMatch(layer, /ranked-prepare-settings|rankedSettingsMenu|onRankedLevelOptions/);
    const options = body(layer, 'CCMenuItem* vanillaOptionsButton(', 'CCLabelBMFont* gateLabel(');
    for (const token of ['"settings-menu"', '"settings-button"', 'm_pListener', 'm_pfnSelector', '&LevelInfoLayer::onLevelOptions']) assert(options.includes(token));
    assert.doesNotMatch(options, /CCMenuItemSpriteExtra::create|setPosition|setTarget|->activate/);
    assert.match(body(layer, 'void setMenuItemsEnabled(', 'void enableSongAcquisitionControls('), /typeinfo_cast<FLAlertLayer\*>\(root\)\) return/);
});

test('modal checked at scheduling AND activation; arm only at final activation', () => {
    const launch = body(layer, 'bool launchRankedGameplay()', 'void activateRankedPlayButton(');
    const activate = body(layer, 'void activateRankedPlayButton(', 'void verifyGameplayLaunch(');
    assert.match(launch, /modalBlocksPlay\(\)/);
    assert.doesNotMatch(launch, /armCurrentLevelForGameplay/);
    assert.match(activate, /preparationAllowsLaunch/);
    assert(activate.indexOf('modalBlocksPlay()') < activate.indexOf('armCurrentLevelForGameplay()'));
    assert(activate.indexOf('armCurrentLevelForGameplay()') < activate.indexOf('nativePlayButton->activate()'));
    assert.match(activate, /runtime\.tick\(\)/);
    assert.match(activate, /scheduleAutoPlay\(0\.25f\)/);
    assert.doesNotMatch(activate, /setTarget|PlayLayer::scene/);
});

test('Jukebox effective path exists on Android too; no hardcoded Stereo Madness workaround', () => {
    const path = body(layer, 'bool songFileExists(', 'bool songIdReady(');
    assert.match(path, /manager->pathForSong\(id\)/);
    assert.doesNotMatch(path, /GEODE_IS_WINDOWS/);
    assert.match(layer, /isModLoaded\("fleym.nongd"\)/);
    assert.match(layer, /if \(jukeboxLoaded\(\)\) setMenuItemsEnabled\(songWidget, true\)/);
    assert.doesNotMatch(layer, /==\s*"Stereo Madness"|find\("Stereo Madness"/);
});

test('diagnostics log values and pointer identity, never full geometry', () => {
    for (const token of ['geometryBytes=', 'notDownloaded=', 'type=', 'song=', 'track=', 'songs=', 'widgetSong=', 'robtop=', 'static_cast<void*>(level)']) assert(resolver.includes(token));
    assert.doesNotMatch(resolver, /m_levelString\.c_str\(\)/);
});

test('metadata recovery cancels competing return/launch callbacks and waits for modals', () => {
    const recover = body(layer, 'void recoverMetadata(', 'void returnForMetadata(');
    for (const callback of ['activateRankedPlayButton', 'verifyGameplayLaunch', 'autoPlayRanked', 'returnToRanked']) {
        assert(recover.includes(`unschedule(schedule_selector(CorumRankedLevelInfoLayer::${callback}))`));
    }
    const back = body(layer, 'void returnForMetadata(', 'void restoreVanillaOptionsMenu(');
    assert(back.indexOf('modalBlocksPlay()') < back.indexOf('popScene()'));
    assert.match(back, /if \(m_fields->returningToRanked\) return/);
});

test('live spectator path is read-only and keeps polling fallback', () => {
    const spectatorBranch = body(play, 'void updateSpectatorView(float dt)', 'void spectatorFrame(float dt)');
    assert.match(play, /schedule\(schedule_selector\(CorumRankedPlayLayer::spectatorFrame\)\)/);
    assert.match(spectatorBranch, /m_player1->setVisible\(false\)/);
    assert.doesNotMatch(spectatorBranch, /m_player1->setPosition\(|m_player1->setRotation\(|m_player1->setVisible\(true\)/);
    assert.doesNotMatch(spectatorBranch, /remotePlayer\.update|driveSpectatorCamera|updateCamera\(/);
    assert.match(play, /bool beginSpectatorPresentationFrame\(\)[\s\S]*?remotePlayer\.update/);
    for (const forbidden of ['reportAttemptStart(', 'reportAttemptProgress(', 'reportAttemptEnd(', 'levelComplete()']) {
        assert(!spectatorBranch.includes(forbidden), `Spectator branch must not invoke ${forbidden}`);
    }
    assert.match(play, /m_fields->spectatorPending/);
    assert.match(end, /Closing Ranked completion overlay for live spectator handoff/);
    assert.match(play, /spectatorCurrentProgress = match\.spectatorCurrentProgress/);
});

test('spectator reconnect reuses attested native round entry without arming an Attempt', () => {
    const popupEntry = body(popup, 'void maybeEnterLevel()', 'void onDownloadAllResources(');
    const levelLaunch = body(layer, 'bool launchRankedGameplay()', 'void activateRankedPlayButton(');
    const activate = body(layer, 'void activateRankedPlayButton(', 'void verifyGameplayLaunch(');
    const playInit = body(play, 'bool init(GJGameLevel* level', 'void postUpdate(float dt)');
    const spectatorAuthority = body(runtime, 'bool RankedRuntime::canEnterCurrentLevelAsSpectator()', 'int RankedRuntime::localDeathmatchVisualAttemptsUsed()');

    assert.match(runtimeHeader, /canEnterCurrentLevelAsSpectator\(\) const/);
    assert.match(popupEntry, /canEnterCurrentLevelAsSpectator\(\)/);
    assert.match(popupEntry, /findPlayableMap\(\)/);
    assert.match(popupEntry, /RankedRoundPreparation::get\(\)\.launch/);
    assert.doesNotMatch(popupEntry, /LevelInfoLayer::scene/);
    const round = read('../src/RankedRoundPreparation.cpp');
    assert.match(round, /preparation.authorizes\(level, m_context\)/);
    assert.match(round, /if \(!survival && !spectator && !entering && !runtime.armCurrentLevelForGameplay\(\)\)/);
    assert.match(round, /PlayLayer::scene\(level, false, false\)/);
    assert.match(levelLaunch, /rankedLevelEntryAllowed\(runtime\)/);
    assert.match(activate, /auto const spectatorLaunch = runtime\.canEnterCurrentLevelAsSpectator\(\)/);
    assert.match(activate, /if \(!spectatorLaunch && !runtime\.armCurrentLevelForGameplay\(\)\)/);
    assert(activate.indexOf('canEnterCurrentLevelAsSpectator()') < activate.indexOf('armCurrentLevelForGameplay()'));
    assert.match(playInit, /syncSpectatorLocalSafety\(\)/);
    assert.match(playInit, /m_player1->setVisible\(false\)/);
    assert.doesNotMatch(playInit, /reportAttemptStart\(|reportAttemptProgress\(|reportAttemptEnd\(/);
    assert.match(spectatorAuthority, /spectatorLevelEntryAllowed/);
    assert.doesNotMatch(spectatorAuthority, /MAP_PLAYING|GJGameLevel::create|PlayLayer::scene|getSavedLevel/);
});

test('spectator scoring transport is suppressed before the first gameplay frame', () => {
    const scoring = body(play, 'bool scoringEnabled()', 'bool init(GJGameLevel* level');
    assert.match(scoring, /!m_fields->spectatorMode/);
    assert.match(scoring, /!m_fields->spectatorPending/);
    assert.match(scoring, /!corum::ranked::RankedRuntime::get\(\)\.isSpectating\(\)/);
    assert.match(scoring, /syncSpectatorLocalSafety\(\)/);
});

test('practice never publishes live gameplay and relay validates stream identity', () => {
    assert.match(play, /tickPublisher\(\{\}, true\)/);
    assert.match(spectator, /!practice/);
    assert.match(spectator, /m_publishAttemptId != attemptId/);
    assert.match(spectator, /PACKET_ATTEMPT_MISMATCH/);
    assert.match(spectator, /STALE_GENERATION/);
    assert.match(spectator, /SEQUENCE_GAP/);
    assert.match(spectator, /UNSUPPORTED_PROTOCOL_VERSION/);
    assert.match(spectator, /STREAM_TIMEOUT/);
    assert.match(spectator, /viewerContextKey/);
    assert.match(spectator, /streamRound != expectedRound/);
    assert.match(spectator, /streamDeathmatch != expectedDeathmatch/);
    assert.match(spectator, /PACKET_CONTEXT_MISMATCH/);
    assert.match(spectator, /publisherElapsedMs=.*rttMs=.*maxCorrection=/);
    assert.match(spectator, /correction rejected/);
});

test('remote ghost uses pose keyframes without bypassing Ranked Level Prepare', () => {
    assert.match(spectator, /encodeSpectatorPose\(pose\)/);
    const codec = read('../src/spectator/SpectatorPoseCodec.hpp');
    for (const field of ['x', 'y', 'rotation', 'mode', 'mini', 'upsideDown']) assert(codec.includes(`value["${field}"]`));
    assert.doesNotMatch(spectator, /GJGameLevel::create|PlayLayer::scene|getSavedLevel/);
    assert.doesNotMatch(play, /GJGameLevel::create|PlayLayer::scene|getSavedLevel/);
});

