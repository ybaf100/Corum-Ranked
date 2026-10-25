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
        'std::string RankedAudioManager::configSignature(',
        'void RankedAudioManager::configure(',
        'bool RankedAudioManager::enabled()',
        'RankedAudioResourceView const* RankedAudioManager::findResource(',
        'RankedAudioResourceView const* RankedAudioManager::resourceForMode(',
        'void RankedAudioManager::setMode(',
        'bool RankedAudioManager::ownedChannelPlaying()',
        'float RankedAudioManager::targetRankedVolume()',
        'float RankedAudioManager::targetRankedVolume(std::string const& key)',
        'void RankedAudioManager::refreshVolume()',
        'FMOD::ChannelGroup* RankedAudioManager::ownedMusicGroup()',
        'void RankedAudioManager::releaseMusicGroup()',
        'void RankedAudioManager::ensureFmodOutputActive()',
        'void RankedAudioManager::stopOwnedAudio()',
        'void RankedAudioManager::releasePreparedSounds()',
        'FMOD::Sound* RankedAudioManager::preparedSound(',
        'void RankedAudioManager::prewarmReadySounds()',
        'FMOD::Channel* RankedAudioManager::preparedChannel(',
        'void RankedAudioManager::prewarmReadyChannels()',
        'void RankedAudioManager::stopPreparedChannels()',
        'void RankedAudioManager::reapRetainedSounds()',
        'void RankedAudioManager::updateStreamHealth()',
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
#include <sstream>
#include <cassert>
#include <chrono>
#include <cmath>
#include <initializer_list>
#include <functional>
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
constexpr int FMOD_TIMEUNIT_MS=1,FMOD_CREATESTREAM=2,FMOD_2D=4,FMOD_NONBLOCKING=8,FMOD_LOOP_NORMAL=16,FMOD_LOOP_OFF=32,FMOD_ACCURATETIME=64;
struct FMOD_CREATESOUNDEXINFO {int cbsize=0;unsigned int initialseekposition=0;int initialseekpostype=0;};
namespace FMOD {
struct ChannelGroup {
    float volume=1;int writes=0,releases=0;ChannelGroup* parent=nullptr;
    FMOD_RESULT setPaused(bool){return FMOD_OK;}FMOD_RESULT setMute(bool){return FMOD_OK;}
    FMOD_RESULT setVolume(float value){volume=value;++writes;return FMOD_OK;}
    FMOD_RESULT addGroup(ChannelGroup* group){group->parent=this;return FMOD_OK;}
    FMOD_RESULT release(){++releases;parent=nullptr;return FMOD_OK;}
    float gain()const{return volume*(parent?parent->gain():1);}
};
struct Sound {
    FMOD_OPENSTATE state=FMOD_OPENSTATE_READY;bool starving=false;int releases=0,seekCalls=0,plays=0;unsigned int initial=0;
    enum Seek {Immediate,Async,NotReadyForever} seek=Immediate;
    FMOD_RESULT openResult=FMOD_OK;
    FMOD_RESULT getOpenState(FMOD_OPENSTATE* s,unsigned int*,bool* hungry,bool*){*s=state;if(hungry)*hungry=starving;return openResult;}
    FMOD_RESULT setMode(int){return FMOD_OK;}FMOD_RESULT setLoopCount(int){return FMOD_OK;}
    FMOD_RESULT release(){++releases;return FMOD_OK;}
};
struct Channel {
    Sound* sound=nullptr;ChannelGroup* group=nullptr;bool playing=true,paused=true,muted=false,failUnpause=false,failSecondFade=false;
    int stops=0,unpauses=0,volumeWrites=0;float volume=1;unsigned int position=0,pendingPosition=0;
    unsigned long long clock=4800000,stopAt=0;bool dspAvailable=true;
    std::vector<std::pair<unsigned long long,float>> fadePoints;
    FMOD_RESULT stop(){++stops;playing=false;return FMOD_OK;}
    FMOD_RESULT isPlaying(bool* value){*value=playing;return FMOD_OK;}
    FMOD_RESULT setPaused(bool value){if(!value&&failUnpause)return FMOD_ERR_INVALID_HANDLE;paused=value;if(!value)++unpauses;return FMOD_OK;}
    FMOD_RESULT getPaused(bool* value){*value=paused;return FMOD_OK;}
    FMOD_RESULT setMute(bool value){muted=value;return FMOD_OK;}FMOD_RESULT getMute(bool* value){*value=muted;return FMOD_OK;}
    FMOD_RESULT setVolume(float value){volume=value;++volumeWrites;return FMOD_OK;}FMOD_RESULT getVolume(float* value){*value=volume;return FMOD_OK;}
    FMOD_RESULT setPosition(unsigned int value,int){++sound->seekCalls;if(sound->seek==Sound::NotReadyForever)return FMOD_ERR_NOTREADY;
        if(sound->seek==Sound::Async){pendingPosition=value;sound->state=FMOD_OPENSTATE_SETPOSITION;}else position=value;return FMOD_OK;}
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
    float audible()const{return playing&&!paused&&!muted?volume*gain()*(group?group->gain():1):0;}
};
struct System {
    std::unordered_map<std::string,Sound*> sounds;std::vector<std::unique_ptr<Sound>> soundOwners;
    std::vector<std::unique_ptr<Channel>> channels;std::vector<std::unique_ptr<ChannelGroup>> groups;int creates=0,plays=0;FMOD_RESULT playResult=FMOD_OK;ChannelGroup master;std::function<void(Channel*)> configureChannel;
    FMOD_RESULT createSound(char const* path,int flags,FMOD_CREATESOUNDEXINFO* info,Sound** value){
        assert((flags&FMOD_NONBLOCKING)&&(flags&FMOD_CREATESTREAM)&&(flags&FMOD_ACCURATETIME));
        assert(info&&info->cbsize==sizeof(FMOD_CREATESOUNDEXINFO)&&info->initialseekpostype==FMOD_TIMEUNIT_MS);++creates;
        soundOwners.push_back(std::make_unique<Sound>());*value=soundOwners.back().get();sounds[path]=*value;(*value)->initial=info->initialseekposition;return FMOD_OK;}
    FMOD_RESULT playSound(Sound* sound,ChannelGroup* group,bool paused,Channel** value){++plays;if(playResult!=FMOD_OK)return playResult;
        channels.push_back(std::make_unique<Channel>());*value=channels.back().get();(*value)->sound=sound;(*value)->group=group;(*value)->paused=paused;(*value)->position=sound->plays++?0:sound->initial;if(configureChannel)configureChannel(*value);return FMOD_OK;}
    FMOD_RESULT getSoftwareFormat(int* rate,void*,void*){*rate=48000;return FMOD_OK;}
    FMOD_RESULT createChannelGroup(char const*,ChannelGroup** value){groups.push_back(std::make_unique<ChannelGroup>());*value=groups.back().get();return FMOD_OK;}
    FMOD_RESULT getMasterChannelGroup(ChannelGroup** value){*value=&master;return FMOD_OK;}
    void advance(int ms){AudioClock::current+=std::chrono::milliseconds(ms);for(auto& c:channels){
        c->clock+=static_cast<unsigned long long>(ms)*48;
        if(c->pendingPosition&&c->sound->state==FMOD_OPENSTATE_READY){c->position=c->pendingPosition;c->pendingPosition=0;}
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
struct RankedVolumeControl{float value=1;static RankedVolumeControl& get(){static RankedVolumeControl c;return c;}float volume()const{return value;}};
struct TransitionTrace {static TransitionTrace& get(){static TransitionTrace t;return t;}template<class T>void mark(T const&){} };
constexpr auto kPendingAudioStartTimeout=std::chrono::seconds(3),kDownloadGuardTimeout=std::chrono::seconds(105);
constexpr auto kPlaybackVerifyDelay=std::chrono::milliseconds(1000);
constexpr int kMusicLibraryFirstSongId=10000000;
constexpr int kMaxPlaybackRetries=2;constexpr float kRankedRelativeVolume=0.8f;
${helpers}
${methods.map(n => method(source, n)).join('\n').replaceAll('std::chrono::steady_clock::now()', 'AudioClock::now()')}
bool RankedAudioManager::resourcesReady()const{return true;}
bool RankedAudioManager::songReady(int)const{return true;}
std::string RankedAudioManager::readySongPath(int song)const{return std::to_string(song);}
std::vector<int> RankedAudioManager::uniqueSongIds()const{return {224958,786863,99477};}
void RankedAudioManager::startNextDownload(){}
void RankedAudioManager::cancelActiveDownload(){}
void RankedAudioManager::synchronizeSongCaches(){}
// The actual download state machine is exercised by RankedAudioDownloadNativeTest.
void RankedAudioManager::updateLibraryDownload(){}
void RankedAudioManager::finishDownloadFailure(int,std::string const&){}
}
using namespace corum::ranked;
bool near(float a,float b){return std::abs(a-b)<0.001f;}
struct Fixture {
    FMOD::System system;RankedAudioManager audio;
    Fixture(){RankedVolumeControl::get().value=1;FMODAudioEngine::sharedEngine()->m_system=&system;audio.m_downloadState=RankedResourceDownloadState::Ready;
        audio.m_config.audio.enabled=true;audio.m_config.audio.fadeInSeconds=1.8;audio.m_config.audio.fadeOutSeconds=1.8;
        audio.m_config.audio.resources={{"menu","",224958,2,true},{"match","",786863,26,true},{"round3_deathmatch_wait","",99477,182,true}};
        audio.m_configSignature=audio.configSignature(audio.m_config);}
    ~Fixture(){audio.fadeOutForGameplay();}
    FMOD::Sound* sound(int id){for(auto const& r:audio.m_config.audio.resources)if(r.songId==id)return audio.preparedSound(r);return nullptr;}
    void step(int ms){system.advance(ms);audio.tick();}
    FMOD::Channel* start(RankedAudioMode mode){audio.setMode(mode);auto* c=audio.m_rankedChannel;assert(c);step(2000);assert(near(c->audible(),0.8f));return c;}
};
int main(){
    // Common gain multiplies GD and personal gain without resetting media or DSP ramps.
    {Fixture f;f.audio.m_config.audio.resources[0].volume=.5;f.audio.m_config.audio.resources[1].volume=.25;
        f.audio.setMode(RankedAudioMode::Lobby);auto* old=f.audio.m_rankedChannel;f.step(2000);assert(old&&near(old->volume,.4f));
        f.audio.setMode(RankedAudioMode::Round12);auto* current=f.audio.m_rankedChannel;
        assert(current&&near(current->volume,.2f)&&f.audio.m_retiringKey=="menu");
        auto before=f.audio.m_config;auto const signature=f.audio.configSignature(before);
        auto const plays=f.system.plays;auto const oldFades=old->fadePoints,newFades=current->fadePoints;
        auto const oldSeeks=old->sound->seekCalls,newSeeks=current->sound->seekCalls;
        auto changed=before;changed.audio.resources[0].volume=0;changed.audio.resources[1].volume=.75;
        assert(f.audio.configSignature(changed)==signature);f.audio.configure(changed);f.step(1);
        assert(f.audio.m_rankedChannel==current&&f.audio.m_retiringChannel==old&&old->stops==0&&current->stops==0);
        assert(near(old->volume,0)&&near(current->volume,.6f));
        assert(old->fadePoints==oldFades&&current->fadePoints==newFades);
        assert(f.system.plays==plays&&old->sound->seekCalls==oldSeeks&&current->sound->seekCalls==newSeeks);
        RankedVolumeControl::get().value=.4f;FMODAudioEngine::sharedEngine()->m_musicVolume=.5f;f.step(1);
        assert(near(current->volume,.3f)&&near(current->group->volume,.4f));
        changed.audio.resources[1].volume=std::numeric_limits<double>::quiet_NaN();f.audio.configure(changed);
        assert(near(f.audio.targetRankedVolume(),.4f));FMODAudioEngine::sharedEngine()->m_musicVolume=1;
    }
    // Only Corum-owned menu BGM joins this group. Both sides of a crossfade
    // change immediately; GD/native music and SFX remain outside its gain.
    {Fixture f;auto* old=f.start(RankedAudioMode::Lobby);
        FMOD::Sound nativeMusic,nativeSfx;FMOD::Channel* music=nullptr;FMOD::Channel* sfx=nullptr;
        f.system.playSound(&nativeMusic,&f.system.master,false,&music);music->setVolume(.65f);
        f.system.playSound(&nativeSfx,&f.system.master,false,&sfx);sfx->setVolume(.25f);
        f.audio.setMode(RankedAudioMode::Round12);auto* current=f.audio.m_rankedChannel;
        auto* group=f.audio.m_musicGroup;assert(group&&old->group==group&&current->group==group);
        f.system.advance(900);auto oldBefore=old->audible(),newBefore=current->audible();
        auto const plays=f.system.plays,oldSeeks=old->sound->seekCalls,newSeeks=current->sound->seekCalls;
        RankedVolumeControl::get().value=.4f;f.audio.refreshVolume();
        assert(near(old->audible(),oldBefore*.4f)&&near(current->audible(),newBefore*.4f));
        assert(near(music->audible(),.65f)&&near(sfx->audible(),.25f)&&f.system.master.volume==1);
        auto const writes=group->writes;for(int i=0;i<100;++i)f.audio.refreshVolume();assert(group->writes==writes);
        RankedVolumeControl::get().value=0;f.audio.refreshVolume();assert(old->audible()==0&&current->audible()==0);
        RankedVolumeControl::get().value=.25f;f.audio.refreshVolume();assert(near(current->audible(),newBefore*.25f));
        assert(f.system.plays==plays&&old->sound->seekCalls==oldSeeks&&current->sound->seekCalls==newSeeks);
        assert(old->stops==0&&current->stops==0);
        f.audio.fadeOutForGameplay();assert(!f.audio.m_musicGroup&&group->releases==1&&old->stops==1&&current->stops==1);
        assert(near(music->audible(),.65f)&&near(sfx->audible(),.25f)&&f.system.master.releases==0);
        f.audio.setMode(RankedAudioMode::ResultWin); // no configured result: still no unrelated gain
        assert(near(music->audible(),.65f));
    }
    // History's native LevelInfo handoff must stop the lobby, its retiring
    // crossfade and prepared channels immediately, without touching GD audio.
    // Background resume/ticks cannot resurrect Ranked BGM until the lobby owns
    // its scene again and explicitly requests the current mode.
    {Fixture f;auto* old=f.start(RankedAudioMode::Lobby);
        f.audio.setMode(RankedAudioMode::Round12);auto* current=f.audio.m_rankedChannel;
        FMOD::Sound nativeSound;FMOD::Channel* nativeMusic=nullptr;
        f.system.playSound(&nativeSound,&f.system.master,false,&nativeMusic);nativeMusic->setVolume(.65f);
        int engineStops=FMODAudioEngine::sharedEngine()->stops,menus=GameManager::sharedState()->menus;
        f.audio.fadeOutForGameplay();
        assert(old->stops==1&&current->stops==1&&!f.audio.m_rankedChannel&&!f.audio.m_retiringChannel);
        assert(f.audio.m_desiredMode==RankedAudioMode::Silent&&f.audio.m_requestedMode==RankedAudioMode::Silent);
        f.audio.onApplicationResume();for(int i=0;i<20;++i)f.step(100);
        assert(!f.audio.m_rankedChannel&&!f.audio.m_retiringChannel&&f.audio.m_preparedChannels.empty());
        assert(nativeMusic->stops==0&&near(nativeMusic->audible(),.65f));
        assert(FMODAudioEngine::sharedEngine()->stops==engineStops&&GameManager::sharedState()->menus==menus);
        f.audio.setMode(RankedAudioMode::Lobby);auto* returned=f.audio.m_rankedChannel;f.step(2000);
        assert(returned&&returned!=old&&near(returned->audible(),.8f));}
    // All configured offsets, especially 182s, are decoded during prewarming.
    // The transition itself performs zero seeks and does not restart the old
    // stream. first-play semantics deliberately reset a reused Sound to zero.
    {Fixture f;f.audio.prewarmReadySounds();auto* prepared=f.sound(786863);auto* r3=f.sound(99477);
        assert(prepared->initial==26000&&r3->initial==182000);
        auto* old=f.start(RankedAudioMode::Lobby);int creates=f.system.creates;
        f.audio.setMode(RankedAudioMode::Round12);auto* candidate=f.audio.m_rankedChannel;
        assert(candidate&&candidate->position==26000&&prepared->seekCalls==0);
        assert(f.system.creates==creates&&f.audio.m_retiringChannel==old&&old->stops==0);
        f.system.advance(900);assert(near(old->audible(),0.4f)&&near(candidate->audible(),0.4f));
        f.step(1000);assert(!f.audio.m_retiringChannel&&old->stops==1);
        f.audio.setMode(RankedAudioMode::Round3DeathmatchWait);
        assert(f.audio.m_rankedChannel->position==182000&&r3->seekCalls==0);}
    // A non-ready prebuffer must keep the old BGM; one initial open and a single
    // bounded deadline replace the old transition-time setPosition loop.
    for(int path=0;path<3;++path){Fixture f;auto* old=f.start(RankedAudioMode::Lobby);auto* match=f.sound(786863);
        if(path==0)match->state=FMOD_OPENSTATE_LOADING;
        if(path==1)match->starving=true;
        if(path==2)f.system.playResult=FMOD_ERR_NOTREADY;
        f.audio.setMode(RankedAudioMode::Round12);
        for(int i=0;i<20;++i){f.audio.setMode(RankedAudioMode::Round12);f.step(200);}
        assert(f.audio.m_failedPlaybackKeys.contains("match")&&f.audio.m_desiredMode==RankedAudioMode::Lobby);
        assert(f.audio.m_rankedChannel==old&&old->stops==0&&match->seekCalls==0);
        auto creates=f.system.creates,plays=f.system.plays;
        for(int i=0;i<20;++i){f.audio.setMode(RankedAudioMode::Round12);f.step(200);}
        assert(f.system.creates==creates&&f.system.plays==plays);}
    // R3 preparation failure remains latched through 300 identical requests.
    {Fixture f;auto* match=f.start(RankedAudioMode::Round12);auto* r3=f.sound(99477);r3->state=FMOD_OPENSTATE_LOADING;
        f.audio.setMode(RankedAudioMode::Round3DeathmatchWait);for(int i=0;i<16;++i)f.step(200);
        assert(f.audio.m_desiredMode==RankedAudioMode::Round12&&match->stops==0&&r3->releases==0);
        auto creates=f.system.creates,plays=f.system.plays;
        for(int i=0;i<300;++i){f.audio.setMode(RankedAudioMode::Round3DeathmatchWait);f.step(200);}
        assert(f.system.creates==creates&&f.system.plays==plays);
        r3->state=FMOD_OPENSTATE_READY;f.audio.setMode(RankedAudioMode::Round12);
        f.audio.setMode(RankedAudioMode::Round3DeathmatchWait);assert(f.audio.m_rankedChannel->position==182000);}
    // Actual buffer starvation must mute stale data, never reset the stream on
    // every refresh. Recovery unmutes the same channel at its existing clock.
    {Fixture f;auto* current=f.start(RankedAudioMode::Lobby);auto* sound=current->sound;
        auto plays=f.system.plays;sound->starving=true;f.step(20);
        assert(current->muted&&near(current->audible(),0)&&current->stops==0);
        for(int i=0;i<25;++i){f.audio.setMode(RankedAudioMode::Lobby);f.step(20);}
        assert(f.system.plays==plays&&current->stops==0);
        sound->starving=false;f.step(20);assert(!current->muted&&near(current->audible(),0.8f));
        assert(f.audio.m_rankedChannel==current&&f.system.plays==plays);}
    // Both active and retiring channels are protected; steady/resume volume
    // writes may not undo the starvation mute during a pending replacement.
    {Fixture f;auto* old=f.start(RankedAudioMode::Lobby);f.audio.setMode(RankedAudioMode::Round12);
        auto* current=f.audio.m_rankedChannel;old->sound->starving=true;current->sound->starving=true;
        f.audio.onApplicationResume();f.step(20);assert(old->muted&&current->muted);
        old->sound->starving=false;current->sound->starving=false;f.step(20);assert(!old->muted&&!current->muted);}
    // Sustained starvation is bounded and falls back instead of looping new
    // channels or leaving an indefinitely audible repeated decode block.
    {Fixture f;auto* current=f.start(RankedAudioMode::Round12);current->sound->starving=true;
        for(int i=0;i<160;++i){f.audio.setMode(RankedAudioMode::Round12);f.step(20);}
        assert(f.audio.m_failedPlaybackKeys.contains("match")&&f.audio.m_desiredMode==RankedAudioMode::Lobby);
        assert(current->muted||current->stops>0);}
    // Offset buffers are single use. Three rounds re-arm during gameplay and
    // never seek/restart/release a Sound used by a currently fading channel.
    {Fixture f;for(int round=0;round<3;++round){auto* c=f.start(RankedAudioMode::Round12);
        assert(c->position>=26000&&c->sound->plays==1&&c->sound->seekCalls==0);
        auto* used=c->sound;f.audio.fadeOutForGameplay();assert(c->stops==1);
        auto* next=f.sound(786863);assert(next!=used&&next->initial==26000);f.audio.tick();
        assert(used->releases==1&&next->releases==0);}}
    // Shared song IDs with different modes/offsets require independent buffers.
    {Fixture f;f.audio.m_config.audio.resources.push_back({"result_win","",786863,29,false});
        auto* old=f.start(RankedAudioMode::Round12);f.audio.prewarmReadySounds();
        assert(old->sound->releases==0);f.audio.setMode(RankedAudioMode::ResultWin);
        auto* next=f.audio.m_rankedChannel;assert(next&&next->sound!=old->sound&&next->position==29000);
        assert(f.audio.m_retiringChannel==old&&old->stops==0&&old->sound->releases==0);f.step(2000);}
    // A READY Sound followed by a successful paused playSound may still report
    // zero. beta38 immediately latched menu -> Silent here (device result=0).
    // Cover every configured offset, including zero, with the ACTUAL class.
    for(unsigned int offset:{0u,2000u,26000u,29000u,182000u}) {Fixture f;
        f.audio.m_config.audio.resources={{"menu","",224958,double(offset)/1000,true}};
        f.system.configureChannel=[](auto* c){c->position=0;};
        f.audio.setMode(RankedAudioMode::Lobby);
        if(offset){assert(!f.audio.m_rankedChannel&&!f.audio.m_failedPlaybackKeys.contains("menu"));
            auto* candidate=f.system.channels.back().get();assert(candidate->paused&&candidate->muted);
            assert(candidate->sound->seekCalls==1);f.step(20);}
        auto* c=f.audio.m_rankedChannel;assert(c&&c->position==offset&&!c->paused&&!c->muted);
        f.step(2000);assert(near(c->audible(),0.8f));
        assert(c->sound->seekCalls==(offset?1:0));}
    // Async seek leaves getPosition stale until the buffer is ready. The old
    // channel continues; the candidate never unpauses at the wrong position.
    {Fixture f;auto* old=f.start(RankedAudioMode::Lobby);auto* s=f.sound(786863);
        s->seek=FMOD::Sound::Async;
        f.system.configureChannel=[](auto* c){c->position=0;c->sound->seek=FMOD::Sound::Async;};
        f.audio.setMode(RankedAudioMode::Round12);auto* pending=f.system.channels.back().get();
        assert(f.audio.m_rankedChannel==old&&pending->paused&&pending->muted&&s->seekCalls==1);
        assert(pending->position==0&&s->state==FMOD_OPENSTATE_SETPOSITION);
        for(int i=0;i<40;++i) {f.audio.setMode(RankedAudioMode::Round12);f.step(20);}
        assert(s->seekCalls==1&&old->stops==0&&!f.audio.m_failedPlaybackKeys.contains("match"));
        s->state=FMOD_OPENSTATE_READY;f.step(20);
        assert(f.audio.m_rankedChannel==pending&&pending->position==26000&&f.audio.m_retiringChannel==old);}
    // Warm the paused R3 candidate during the lobby. Mode changes must transfer
    // that exact prepared channel without a new play/seek or 182s flush.
    {Fixture f;f.audio.prewarmReadySounds();
        f.system.configureChannel=[](auto* c){c->position=0;};f.audio.setMode(RankedAudioMode::Lobby);
        f.step(20);f.step(20);auto* r3=f.sound(99477);
        auto* standby=f.audio.m_preparedChannels.at("round3_deathmatch_wait").channel;
        assert(standby&&standby->paused&&standby->muted&&standby->position==182000&&r3->seekCalls==1);
        auto const plays=f.system.plays;f.audio.setMode(RankedAudioMode::Round3DeathmatchWait);
        assert(f.audio.m_rankedChannel==standby&&f.system.plays==plays&&r3->seekCalls==1);
        f.step(2000);assert(near(standby->audible(),0.8f));}
    // An accepted but permanently stale seek stays silent and times out once.
    {Fixture f;auto* old=f.start(RankedAudioMode::Lobby);auto* s=f.sound(786863);
        f.system.configureChannel=[](auto* c){c->position=0;c->sound->seek=FMOD::Sound::Async;};
        f.audio.setMode(RankedAudioMode::Round12);
        for(int i=0;i<170;++i){f.audio.setMode(RankedAudioMode::Round12);f.step(20);}
        assert(s->seekCalls==1&&f.audio.m_failedPlaybackKeys.contains("match"));
        assert(f.audio.m_rankedChannel==old&&old->stops==0);
        const int plays=f.system.plays;for(int i=0;i<100;++i){f.audio.setMode(RankedAudioMode::Round12);f.step(20);}
        assert(f.system.plays==plays);}
    // All muted standby channels are stopped on gameplay/close/config teardown.
    for(int path=0;path<3;++path){Fixture f;f.audio.prewarmReadySounds();f.start(RankedAudioMode::Lobby);
        auto* standby=f.audio.m_preparedChannels.at("match").channel;assert(standby&&standby->paused);
        if(path==0){f.audio.fadeOutForGameplay();}if(path==1){f.audio.restoreMenuMusic();}if(path==2){f.audio.releasePreparedSounds();}
        assert(standby->stops==1&&f.audio.m_preparedChannels.empty());}

    // Interrupted fades preserve instantaneous DSP gain, including Silent.
    for(bool silent:{false,true}){Fixture f;f.audio.setMode(RankedAudioMode::Lobby);
        auto* old=f.audio.m_rankedChannel;f.step(900);assert(near(old->audible(),0.4f));
        f.audio.setMode(silent?RankedAudioMode::Silent:RankedAudioMode::Round12);
        assert(near(old->audible(),0.4f)&&old->fadePoints.back().second==0);
        f.system.advance(900);assert(near(old->audible(),0.2f));f.step(1000);assert(old->stops==1);}
    // Partial/unsupported DSP fades must clear their zero multiplier.
    for(int path=0;path<2;++path){Fixture f;
        f.system.configureChannel=[path](auto* c){if(path==0)c->dspAvailable=false;else c->failSecondFade=true;};
        f.audio.setMode(RankedAudioMode::Round12);auto* c=f.audio.m_rankedChannel;
        assert(c&&!f.audio.m_ownedDspFadeIn&&c->fadePoints.empty());f.step(2000);assert(near(c->audible(),0.8f));}
    // No-progress verification remains bounded; one-use fresh streams preserve
    // configured offsets on retry rather than incorrectly playing from zero.
    {Fixture f;f.audio.setMode(RankedAudioMode::Round12);
        for(int i=0;i<30;++i){f.audio.setMode(RankedAudioMode::Round12);f.system.advance(1100);
            if(f.audio.m_rankedChannel)f.audio.m_rankedChannel->position=f.audio.m_playbackVerifyStartPositionMs;
            f.audio.tick();}
        assert(f.audio.m_failedPlaybackKeys.contains("match")&&f.audio.m_failedPlaybackKeys.contains("menu"));
        assert(f.audio.m_desiredMode==RankedAudioMode::Silent);}
    // Application/close/gameplay/config teardown stops every independent owner.
    for(int path=0;path<3;++path){Fixture f;auto* old=f.start(RankedAudioMode::Lobby);
        f.audio.setMode(RankedAudioMode::Round12);auto* current=f.audio.m_rankedChannel;
        f.audio.onApplicationPause();assert(old->paused&&current->paused);
        if(path==0){f.audio.fadeOutForGameplay();}if(path==1){f.audio.restoreMenuMusic();}if(path==2){f.audio.releasePreparedSounds();}
        assert(old->stops==1&&current->stops==1&&!f.audio.m_rankedChannel&&!f.audio.m_retiringChannel);}

}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-audio-transition-'));
    try {
        writeFileSync(join(dir, 'AudioConfigDouble.hpp'), `#pragma once\n#include "${join(root, 'src/domain/RankAssetPolicy.hpp')}"\n#include <cstdint>\n#include <string>\n#include <vector>\nnamespace geode {namespace utils::web {struct WebResponse {};} namespace async {template<class T> struct TaskHolder {void cancel(){}};}}\nnamespace corum::ranked {${config}}\n`);
        writeFileSync(join(dir, 'AudioManagerUnderTest.hpp'), header);
        writeFileSync(join(dir, 'test.cpp'), cpp);
        const binary = join(dir, 'test');
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I', dir,
            ...(debug ? ['-DCORUM_RANKED_DEBUG_SPECTATE=1'] : []), join(dir, 'test.cpp'), '-o', binary], {stdio:'pipe'});
        execFileSync(binary, [], {stdio:'pipe'});
    } finally {rmSync(dir, {recursive:true, force:true});}
});

