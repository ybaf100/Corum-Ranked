import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join, resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = path => readFileSync(join(root, path), 'utf8');
function method(source, signature) {
    const start = source.indexOf(signature); assert(start >= 0, signature);
    const open = source.indexOf('{', start); let end = open + 1, depth = 1;
    while (depth) { if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end; }
    return source.slice(start, end);
}

for (const debug of [false, true]) test(`production audio transitions, bounded failures and DSP crossfade (Debug ${debug ? 'ON' : 'OFF'})`, () => {
    const source = read('src/RankedAudioManager.cpp');
    const methods = [
        'bool RankedAudioManager::enabled()',
        'RankedAudioResourceView const* RankedAudioManager::findResource(',
        'RankedAudioResourceView const* RankedAudioManager::resourceForMode(',
        'void RankedAudioManager::setMode(',
        'bool RankedAudioManager::ownedChannelPlaying()',
        'float RankedAudioManager::targetRankedVolume()',
        'void RankedAudioManager::ensureFmodOutputActive()',
        'void RankedAudioManager::stopOwnedAudio()',
        'void RankedAudioManager::releasePreparedSounds()',
        'FMOD::Sound* RankedAudioManager::preparedSound(',
        'void RankedAudioManager::prewarmReadySounds()',
        'void RankedAudioManager::beginOwnedFadeOut(',
        'void RankedAudioManager::requestAudioSwitch()',
        'RankedAudioMode RankedAudioManager::availableMode(',
        'bool RankedAudioManager::audioStartTimedOut()',
        'void RankedAudioManager::failDesiredAudio(',
        'void RankedAudioManager::retireOwnedChannel(',
        'void RankedAudioManager::startDesiredAudio(',
        'void RankedAudioManager::updatePendingAudioStart()',
        'void RankedAudioManager::cancelPendingAudioStart()',
        'void RankedAudioManager::verifyDesiredAudio()',
        'void RankedAudioManager::updateOwnedFades(',
        'void RankedAudioManager::fadeOutForGameplay()',
        'void RankedAudioManager::restoreMenuMusic()',
        'void RankedAudioManager::onApplicationPause()',
        'void RankedAudioManager::onApplicationResume()',
        'void RankedAudioManager::tick()',
    ];
    const configSource = read('src/RankedRuntime.hpp');
    const config = configSource.slice(configSource.indexOf('struct RankedAudioResourceView {'),
        configSource.indexOf('struct EnvironmentModIssueView {'));
    const helpers = source.slice(source.indexOf('enum class PreparedStreamState {'), source.indexOf('} // namespace'));
    // Compile the production class and callback bodies. Substitute only the
    // steady clock; FMOD/file/download boundaries are public-API doubles.
    const header = read('src/RankedAudioManager.hpp').replace('#include "RankedRuntime.hpp"', '#include "AudioConfigDouble.hpp"').replace('private:', 'public:');
    const cpp = `
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
struct AudioClock {
    static inline auto current = std::chrono::steady_clock::time_point(std::chrono::seconds(100));
    static auto now(){return current;}
};
namespace geode::log {template<class... T> void info(T...){} template<class... T> void warn(T...){}
 template<class... T> void error(T...){} template<class... T> void debug(T...){} }
using namespace geode;
namespace fmt {template<class... T> std::string format(T...){return "trace";}}
enum FMOD_RESULT {FMOD_OK, FMOD_ERR_NOTREADY, FMOD_ERR_INVALID_HANDLE};
enum FMOD_OPENSTATE {FMOD_OPENSTATE_READY,FMOD_OPENSTATE_PLAYING,FMOD_OPENSTATE_SETPOSITION,FMOD_OPENSTATE_LOADING,FMOD_OPENSTATE_ERROR};
constexpr int FMOD_TIMEUNIT_MS=1,FMOD_CREATESTREAM=2,FMOD_2D=4,FMOD_NONBLOCKING=8,FMOD_LOOP_NORMAL=16,FMOD_LOOP_OFF=32;
namespace FMOD {
struct Sound {
    FMOD_OPENSTATE state=FMOD_OPENSTATE_READY;bool starving=false;int releases=0,seekCalls=0;
    enum Seek {Immediate,Async,NotReadyForever} seek=Immediate;
    FMOD_RESULT openResult=FMOD_OK;
    FMOD_RESULT getOpenState(FMOD_OPENSTATE* s,unsigned int*,bool* hungry,bool*){*s=state;if(hungry)*hungry=starving;return openResult;}
    FMOD_RESULT setMode(int){return FMOD_OK;}FMOD_RESULT setLoopCount(int){return FMOD_OK;}
    FMOD_RESULT release(){++releases;return FMOD_OK;}
};
struct Channel {
    Sound* sound=nullptr;bool playing=true,paused=true,muted=false,failUnpause=false,failSecondFade=false;
    int stops=0,unpauses=0,volumeWrites=0;float volume=1;unsigned int position=0;
    unsigned long long clock=4800000,stopAt=0;bool dspAvailable=true;
    std::vector<std::pair<unsigned long long,float>> fadePoints;
    FMOD_RESULT stop(){++stops;playing=false;return FMOD_OK;}
    FMOD_RESULT isPlaying(bool* value){*value=playing;return FMOD_OK;}
    FMOD_RESULT setPaused(bool value){if(!value&&failUnpause)return FMOD_ERR_INVALID_HANDLE;paused=value;if(!value)++unpauses;return FMOD_OK;}
    FMOD_RESULT getPaused(bool* value){*value=paused;return FMOD_OK;}
    FMOD_RESULT setMute(bool value){muted=value;return FMOD_OK;}FMOD_RESULT getMute(bool* value){*value=muted;return FMOD_OK;}
    FMOD_RESULT setVolume(float value){volume=value;++volumeWrites;return FMOD_OK;}FMOD_RESULT getVolume(float* value){*value=volume;return FMOD_OK;}
    FMOD_RESULT setPosition(unsigned int value,int){++sound->seekCalls;if(sound->seek==Sound::NotReadyForever)return FMOD_ERR_NOTREADY;
        position=value;if(sound->seek==Sound::Async)sound->state=FMOD_OPENSTATE_SETPOSITION;return FMOD_OK;}
    FMOD_RESULT getPosition(unsigned int* value,int){*value=position;return FMOD_OK;}
    FMOD_RESULT getDSPClock(unsigned long long*,unsigned long long* parent){if(!dspAvailable)return FMOD_ERR_INVALID_HANDLE;*parent=clock;return FMOD_OK;}
    FMOD_RESULT addFadePoint(unsigned long long at,float value){
        if(failSecondFade&&fadePoints.size()==1)return FMOD_ERR_INVALID_HANDLE;
        fadePoints.emplace_back(at,value);std::sort(fadePoints.begin(),fadePoints.end());return FMOD_OK;}
    FMOD_RESULT removeFadePoints(unsigned long long begin,unsigned long long end){
        std::erase_if(fadePoints,[=](auto p){return p.first>=begin&&p.first<=end;});return FMOD_OK;}
    FMOD_RESULT setDelay(unsigned long long,unsigned long long end,bool){stopAt=end;return FMOD_OK;}
    float gain()const{if(fadePoints.empty())return 1;if(clock<=fadePoints.front().first)return fadePoints.front().second;
        for(std::size_t i=1;i<fadePoints.size();++i)if(clock<fadePoints[i].first){auto a=fadePoints[i-1],b=fadePoints[i];
            return a.second+(b.second-a.second)*float(double(clock-a.first)/double(b.first-a.first));}
        return fadePoints.back().second;}
    float audible()const{return playing&&!paused&&!muted?volume*gain():0;}
};
struct ChannelGroup {FMOD_RESULT setPaused(bool){return FMOD_OK;}FMOD_RESULT setMute(bool){return FMOD_OK;}};
struct System {
    std::unordered_map<std::string,Sound*> sounds;std::vector<std::unique_ptr<Sound>> soundOwners;
    std::vector<std::unique_ptr<Channel>> channels;int creates=0,plays=0;FMOD_RESULT playResult=FMOD_OK;ChannelGroup master;
    FMOD_RESULT createSound(char const* path,int flags,void*,Sound** value){
        assert((flags&FMOD_NONBLOCKING)&&(flags&FMOD_CREATESTREAM));++creates;
        soundOwners.push_back(std::make_unique<Sound>());*value=soundOwners.back().get();sounds[path]=*value;return FMOD_OK;}
    FMOD_RESULT playSound(Sound* sound,void*,bool paused,Channel** value){++plays;if(playResult!=FMOD_OK)return playResult;
        channels.push_back(std::make_unique<Channel>());*value=channels.back().get();(*value)->sound=sound;(*value)->paused=paused;return FMOD_OK;}
    FMOD_RESULT getSoftwareFormat(int* rate,void*,void*){*rate=48000;return FMOD_OK;}
    FMOD_RESULT getMasterChannelGroup(ChannelGroup** value){*value=&master;return FMOD_OK;}
    void advance(int ms){AudioClock::current+=std::chrono::milliseconds(ms);for(auto& c:channels){
        c->clock+=static_cast<unsigned long long>(ms)*48;
        if(c->playing&&!c->paused&&c->sound->state==FMOD_OPENSTATE_READY&&!c->sound->starving)c->position+=ms;
        if(c->stopAt&&c->clock>=c->stopAt)c->playing=false;}}
};
}
struct FMODAudioEngine {
    FMOD::System* m_system=nullptr;float m_musicVolume=1;int stops=0;
    static FMODAudioEngine* sharedEngine(){static FMODAudioEngine e;return &e;}
    void stopAllMusic(bool){++stops;}
};
struct GameManager {static GameManager* sharedState(){static GameManager g;return &g;}int menus=0;void playMenuMusic(){++menus;}};
#include "AudioManagerUnderTest.hpp"
namespace corum::ranked {
struct TransitionTrace {static TransitionTrace& get(){static TransitionTrace t;return t;}template<class T>void mark(T const&){} };
constexpr auto kPendingAudioStartTimeout=std::chrono::seconds(3),kDownloadGuardTimeout=std::chrono::seconds(105);
constexpr auto kPlaybackVerifyDelay=std::chrono::milliseconds(1000);
constexpr int kMaxPlaybackRetries=2;constexpr float kRankedRelativeVolume=0.8f;
${helpers}
${methods.map(n => method(source, n)).join('\n').replaceAll('std::chrono::steady_clock::now()', 'AudioClock::now()')}
bool RankedAudioManager::resourcesReady()const{return true;}
bool RankedAudioManager::songReady(int)const{return true;}
std::string RankedAudioManager::readySongPath(int song)const{return std::to_string(song);}
std::vector<int> RankedAudioManager::uniqueSongIds()const{return {224958,786863,99477};}
void RankedAudioManager::startNextDownload(){}
void RankedAudioManager::finishDownloadFailure(int,std::string const&){}
}
using namespace corum::ranked;
bool near(float a,float b){return std::abs(a-b)<0.001f;}
struct Fixture {
    FMOD::System system;RankedAudioManager audio;
    Fixture(){FMODAudioEngine::sharedEngine()->m_system=&system;audio.m_downloadState=RankedResourceDownloadState::Ready;
        audio.m_config.audio.enabled=true;audio.m_config.audio.fadeInSeconds=1.8;audio.m_config.audio.fadeOutSeconds=1.8;
        audio.m_config.audio.resources={{"menu","",224958,2,true},{"match","",786863,26,true},{"round3_deathmatch_wait","",99477,182,true}};}
    ~Fixture(){audio.fadeOutForGameplay();}
    FMOD::Sound* sound(int id){return audio.preparedSound(id);}
    void step(int ms){system.advance(ms);audio.tick();}
    FMOD::Channel* start(RankedAudioMode mode){audio.setMode(mode);auto* c=audio.m_rankedChannel;assert(c);step(2000);assert(near(c->audible(),0.8f));return c;}
};
int main(){
    // Slow 26s compressed seek: no old-channel stop, no repeated candidate.
    {Fixture f;auto* old=f.start(RankedAudioMode::Lobby);auto* match=f.sound(786863);match->seek=FMOD::Sound::Async;
        int plays=f.system.plays,stops=FMODAudioEngine::sharedEngine()->stops;f.audio.setMode(RankedAudioMode::Round12);
        auto* candidate=f.audio.m_pendingChannel;assert(candidate&&candidate->paused&&candidate->muted&&candidate->position==26000);
        for(int i=0;i<8;++i){f.audio.setMode(RankedAudioMode::Round12);f.step(200);assert(old->stops==0&&old->audible()>0);}
        assert(f.system.plays==plays+1&&candidate->unpauses==0);
        match->state=FMOD_OPENSTATE_READY;f.audio.tick();assert(f.audio.m_rankedChannel==candidate&&f.audio.m_retiringChannel==old);
        assert(candidate->unpauses==1&&near(candidate->volume,0.8f)&&near(candidate->audible(),0)&&old->stops==0);
        // DSP clock ramps between 200ms UI ticks; base volume stays nonzero.
        f.system.advance(900);assert(near(old->audible(),0.4f)&&near(candidate->audible(),0.4f));
        assert(FMODAudioEngine::sharedEngine()->stops==stops);f.step(1000);
        assert(!f.audio.m_retiringChannel&&near(candidate->audible(),0.8f)&&old->stops==1);}
    // A pending 182s R3 seek times out once; UI must keep the match fallback.
    {Fixture f;auto* match=f.start(RankedAudioMode::Round12);auto* r3=f.sound(99477);r3->seek=FMOD::Sound::Async;
        f.audio.setMode(RankedAudioMode::Round3DeathmatchWait);auto* candidate=f.audio.m_pendingChannel;assert(candidate->position==182000);
        for(int i=0;i<16;++i){f.audio.setMode(RankedAudioMode::Round3DeathmatchWait);f.step(200);}
        assert(candidate->stops==1&&r3->releases==0&&match->stops==0);
        assert(f.audio.m_desiredMode==RankedAudioMode::Round12&&f.audio.m_failedPlaybackKeys.contains("round3_deathmatch_wait"));
        auto plays=f.system.plays,creates=f.system.creates;
        for(int i=0;i<300;++i){f.audio.setMode(RankedAudioMode::Round3DeathmatchWait);f.step(200);}
        assert(f.system.plays==plays&&f.system.creates==creates&&f.audio.m_rankedChannel==match);
        // A genuinely new requested mode can retry a previously failed track.
        r3->seek=FMOD::Sound::Immediate;r3->state=FMOD_OPENSTATE_READY;
        f.audio.setMode(RankedAudioMode::Round12);f.audio.setMode(RankedAudioMode::Round3DeathmatchWait);
        assert(f.audio.m_playingKey=="round3_deathmatch_wait"&&!f.audio.m_failedPlaybackKeys.contains("round3_deathmatch_wait"));}
    // Initial async open (no pending Channel) and repeated READY/NOTREADY seek
    // failures share the same bounded deadline, not a reset on every retry.
    for(int path=0;path<3;++path){Fixture f;auto* old=f.start(RankedAudioMode::Lobby);auto* match=f.sound(786863);
        if(path==0)match->state=FMOD_OPENSTATE_LOADING;
        if(path==1)match->seek=FMOD::Sound::NotReadyForever;
        if(path==2)f.system.playResult=FMOD_ERR_NOTREADY;
        f.audio.setMode(RankedAudioMode::Round12);if(path!=1)assert(!f.audio.m_pendingChannel);
        for(int i=0;i<20;++i){f.audio.setMode(RankedAudioMode::Round12);f.step(200);}
        assert(f.audio.m_desiredMode==RankedAudioMode::Lobby&&f.audio.m_failedPlaybackKeys.contains("match"));
        assert(f.audio.m_rankedChannel==old&&old->stops==0&&match->releases==0);auto plays=f.system.plays;
        for(int i=0;i<20;++i){f.audio.setMode(RankedAudioMode::Round12);f.step(200);}assert(f.system.plays==plays);}
    // READY but starving must not unpause/swap; recovering buffer can finish.
    {Fixture f;auto* old=f.start(RankedAudioMode::Lobby);auto* match=f.sound(786863);match->starving=true;
        f.audio.setMode(RankedAudioMode::Round12);f.step(1000);assert(f.audio.m_rankedChannel==old&&!f.audio.m_pendingChannel);
        match->starving=false;f.audio.tick();assert(f.audio.m_playingKey=="match");}
    // Start failure must keep the original channel, never replace it with mute.
    {Fixture f;auto* old=f.start(RankedAudioMode::Lobby);auto* match=f.sound(786863);match->seek=FMOD::Sound::Async;
        f.audio.setMode(RankedAudioMode::Round12);auto* candidate=f.audio.m_pendingChannel;candidate->failUnpause=true;
        match->state=FMOD_OPENSTATE_READY;f.audio.tick();assert(candidate->stops==1&&old->stops==0&&old->audible()>0);
        assert(f.audio.m_rankedChannel==old&&f.audio.m_desiredMode==RankedAudioMode::Lobby);}
    // Interrupting an incomplete DSP fade must not jump to full old volume or
    // retain a future fade-in endpoint; its starting gain is the current gain.
    {Fixture f;f.audio.setMode(RankedAudioMode::Lobby);auto* old=f.audio.m_rankedChannel;f.step(900);assert(near(old->audible(),0.4f));
        f.audio.setMode(RankedAudioMode::Round12);assert(near(old->audible(),0.4f)&&old->fadePoints.back().second==0);
        f.system.advance(900);assert(near(old->audible(),0.2f)&&near(f.audio.m_rankedChannel->audible(),0.4f));}
    // Silent also retires at the current DSP gain, without multiplying a second
    // tick-driven envelope or keeping the ended stream as an active owner.
    {Fixture f;f.audio.setMode(RankedAudioMode::Lobby);auto* old=f.audio.m_rankedChannel;f.step(900);
        f.audio.setMode(RankedAudioMode::Silent);assert(!f.audio.m_rankedChannel&&f.audio.m_retiringChannel==old);
        assert(near(old->audible(),0.4f));f.system.advance(900);assert(near(old->audible(),0.2f));
        f.step(1000);assert(!f.audio.m_retiringChannel&&old->stops==1);}
    // If DSP scheduling is unavailable/partial, the bounded tick fade remains
    // usable and no stray zero envelope silences the candidate permanently.
    for(int path=0;path<2;++path){Fixture f;auto* match=f.sound(786863);match->seek=FMOD::Sound::Async;
        f.audio.setMode(RankedAudioMode::Round12);auto* c=f.audio.m_pendingChannel;
        if(path==0)c->dspAvailable=false;else c->failSecondFade=true;
        match->state=FMOD_OPENSTATE_READY;f.audio.tick();assert(!f.audio.m_ownedDspFadeIn&&c->fadePoints.empty());
        f.step(2000);assert(near(c->audible(),0.8f));}
    // No-progress verification gets two retries. Repeated UI calls cannot
    // reset that budget; failures across the chain eventually become Silent.
    {Fixture f;f.audio.setMode(RankedAudioMode::Round12);
        for(int i=0;i<30;++i){f.audio.setMode(RankedAudioMode::Round12);f.system.advance(1100);
            if(f.audio.m_rankedChannel)f.audio.m_rankedChannel->position=f.audio.m_playbackVerifyStartPositionMs;
            f.audio.tick();}
        assert(f.audio.m_failedPlaybackKeys.contains("match")&&f.audio.m_failedPlaybackKeys.contains("menu"));
        assert(f.audio.m_desiredMode==RankedAudioMode::Silent);auto plays=f.system.plays;
        for(int i=0;i<20;++i){f.audio.setMode(RankedAudioMode::Round12);f.step(200);}assert(f.system.plays==plays);}
    // Don't loop recovery/create while a slow replacement is already pending.
    {Fixture f;auto* old=f.start(RankedAudioMode::Lobby);auto* match=f.sound(786863);match->seek=FMOD::Sound::Async;
        f.audio.setMode(RankedAudioMode::Round12);old->playing=false;auto plays=f.system.plays;
        for(int i=0;i<5;++i){f.step(200);}assert(f.system.plays==plays&&f.audio.m_pendingChannel);}
    // Gameplay handoff/close/config teardown stop all independently owned
    // channels, including the fading predecessor and the muted seek candidate.
    for(int path=0;path<3;++path){Fixture f;auto* old=f.start(RankedAudioMode::Lobby);f.audio.setMode(RankedAudioMode::Round12);
        auto* current=f.audio.m_rankedChannel;assert(f.audio.m_retiringChannel==old);auto* r3=f.sound(99477);r3->seek=FMOD::Sound::Async;
        f.audio.setMode(RankedAudioMode::Round3DeathmatchWait);auto* pending=f.audio.m_pendingChannel;
        f.audio.onApplicationPause();assert(old->paused&&current->paused&&pending->paused);
        if(path==0){f.audio.fadeOutForGameplay();}if(path==1){f.audio.restoreMenuMusic();}if(path==2){f.audio.releasePreparedSounds();}
        assert(old->stops==1&&current->stops==1&&pending->stops==1);
        assert(!f.audio.m_rankedChannel&&!f.audio.m_pendingChannel&&!f.audio.m_retiringChannel);}
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-audio-transition-'));
    try {
        writeFileSync(join(dir, 'AudioConfigDouble.hpp'), `#pragma once\n#include <cstdint>\n#include <string>\n#include <vector>\nnamespace geode {namespace utils::web {struct WebResponse {};} namespace async {template<class T> struct TaskHolder {void cancel(){}};}}\nnamespace corum::ranked {${config}}\n`);
        writeFileSync(join(dir, 'AudioManagerUnderTest.hpp'), header);
        writeFileSync(join(dir, 'test.cpp'), cpp);
        const binary = join(dir, 'test');
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I', dir,
            ...(debug ? ['-DCORUM_RANKED_DEBUG_SPECTATE=1'] : []), join(dir, 'test.cpp'), '-o', binary], {stdio:'pipe'});
        execFileSync(binary, [], {stdio:'pipe'});
    } finally {rmSync(dir, {recursive:true, force:true});}
});
