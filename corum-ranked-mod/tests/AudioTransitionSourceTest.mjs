import {readFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import assert from 'node:assert/strict';
import test from 'node:test';
const source=readFileSync(fileURLToPath(new URL('../src/RankedAudioManager.cpp',import.meta.url)),'utf8');
const popup=readFileSync(fileURLToPath(new URL('../src/RankedPopup.cpp',import.meta.url)),'utf8');
function body(from,to){const a=source.indexOf(from),b=source.indexOf(to,a+from.length);assert(a>=0&&b>a);return source.slice(a,b);}
test('single-use streams prepare silent offset channels ahead of transitions',()=>{
 const prepared=body('FMOD::Sound* RankedAudioManager::preparedSound(','void RankedAudioManager::synchronizeSongCaches()');
 assert.match(prepared,/FMOD_CREATESOUNDEXINFO info \{\}/);
 assert.match(prepared,/info\.cbsize = sizeof\(info\)/);
 assert.match(prepared,/info\.initialseekposition/);assert.match(prepared,/info\.initialseekpostype = FMOD_TIMEUNIT_MS/);
 assert.match(prepared,/FMOD_CREATESTREAM \| FMOD_2D \| FMOD_NONBLOCKING \| FMOD_ACCURATETIME/);
 assert.match(prepared,/m_preparedSounds\[resource\.key\]/);
 assert.match(prepared,/m_consumedPreparedKeys\.contains\(resource\.key\)/);
 assert.doesNotMatch(body('void RankedAudioManager::startDesiredAudio(', 'void RankedAudioManager::updatePendingAudioStart()'),/->setPosition\(|playSound\(/);
 assert.match(source,/prewarmReadyChannels\(\);/);
 const warm=body('FMOD::Channel* RankedAudioManager::preparedChannel(', 'void RankedAudioManager::prewarmReadyChannels()');
 assert.match(warm,/if \(!prepared\.seekSubmitted\)/);
 assert.match(warm,/prepared\.channel->setPosition\(startMs, FMOD_TIMEUNIT_MS\)/);
 assert.match(warm,/prepared\.seekSubmitted = true/);
 assert.match(warm,/prepared\.channel->setMute\(true\)/);
 assert.match(source,/prewarmReadySounds\(\);\n}\n\nvoid RankedAudioManager::onApplicationPause/);
});
test('initial offset mismatch is repaired while muted; transferred offset still validated',()=>{
 const start=body('void RankedAudioManager::startDesiredAudio(','void RankedAudioManager::updatePendingAudioStart()');
 assert.match(source,/playSound\(sound, nullptr, true, &prepared\.channel\)/);
 assert.match(start,/preparedChannel\(\*resource, sound, prepareResult\)/);
 assert.match(start,/m_consumedPreparedKeys\.insert\(resource->key\)/);
 assert.match(start,/channel->setMute\(true\)/);assert.doesNotMatch(start,/setPaused\(false\)/);
 const finish=body('void RankedAudioManager::updatePendingAudioStart()','void RankedAudioManager::cancelPendingAudioStart()');
 assert(finish.indexOf('getPosition(&position')<finish.indexOf('setPaused(false)'));
 assert.match(finish,/failDesiredAudio\("prepared-position"/);
});
test('stream starvation mute is preserved during normal refresh and application resume',()=>{
 const health=body('void RankedAudioManager::updateStreamHealth()','void RankedAudioManager::synchronizeSongCaches()');
 assert.match(health,/getOpenState\(&state, &buffered, &starving, &diskBusy\)/);
 assert.match(health,/channel->setMute\(mustMute\)/);
 assert.match(health,/observe\(m_retiringSound, m_retiringChannel/);
 assert.match(health,/failDesiredAudio\("active-buffer-timeout"/);
 assert.match(source,/m_rankedChannel->setMute\(m_ownedStreamMuted\)/);
 assert.doesNotMatch(source,/m_rankedChannel->setMute\(false\)/);
 assert.match(popup,/schedule\(schedule_selector\(CorumRankedLayer::audioFrame\), 0\.02f\)/);
 assert.match(popup,/void audioFrame\(float\) \{ RankedAudioManager::get\(\)\.tick\(\); \}/);
 assert.equal((popup.match(/unschedule\(schedule_selector\(CorumRankedLayer::audioFrame\)\)/g)||[]).length,2);
});
test('bounded preparation failures keep the audible predecessor and latch a fallback',()=>{
 assert.match(source,/kPendingAudioStartTimeout = std::chrono::seconds\(3\)/);
 const fail=body('void RankedAudioManager::failDesiredAudio(','void RankedAudioManager::retireOwnedChannel(');
 assert.match(fail,/m_failedPlaybackKeys\.insert\(resource->key\)/);
 assert.match(fail,/availableMode\(m_requestedMode\)/);
 assert.doesNotMatch(fail,/sound->release\(\)|stopOwnedAudio\(\)/);
 const finish=body('void RankedAudioManager::updatePendingAudioStart()','void RankedAudioManager::cancelPendingAudioStart()');
 assert(finish.indexOf('channel->setPaused(false)')<finish.indexOf('retireOwnedChannel('));
 assert.match(finish,/channel->addFadePoint\(clock, 0\)/);assert.match(finish,/audio-prebuffer-ready:/);
});
test('paths stay cached and discarded loading/active Sounds are not synchronously released in switches',()=>{
 assert.match(source,/m_readySongPaths\.find\(songId\)/);
 const prepared=body('FMOD::Sound* RankedAudioManager::preparedSound(','void RankedAudioManager::updateStreamHealth()');
 assert.match(prepared,/m_retainedSounds\.push_back\(found->second\)/);
 assert.match(prepared,/sound == m_rankedSound \|\| sound == m_retiringSound \|\| sound == m_pendingSound/);
 assert.match(prepared,/preparedStreamState\(sound\) == PreparedStreamState::Pending/);
 assert.doesNotMatch(source,/m_system->update\(\)/);
});
