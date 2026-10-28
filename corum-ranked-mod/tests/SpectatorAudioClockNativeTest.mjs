import test from 'node:test';import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';import {join,resolve} from 'node:path';import {tmpdir} from 'node:os';import {execFileSync} from 'node:child_process';
const root=resolve(new URL('..',import.meta.url).pathname),source=readFileSync(join(root,'src/PlayLayer.cpp'),'utf8');
function method(name){let start=source.indexOf(name);assert(start>=0);let p=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[p]==='{')-(source[p]==='}');++p;}return source.slice(start,p);}
test('production spectator audio waits for source clock, seeks actual song position after restoration, holds terminal and never double-starts',()=>{
 const dir=mkdtempSync(join(tmpdir(),'spectator-audio-clock-'));try{
 const cpp=`
#include "${root}/src/domain/SpectatorRemotePolicy.hpp"
#include "${root}/src/domain/SpectatorAudioPolicy.hpp"
#include <cassert>
#include <chrono>
namespace corum::ranked {enum class SpectatorLiveStatus{Live,Syncing,PartialSync};struct SpectatorLiveRuntime{SpectatorLiveStatus state=SpectatorLiveStatus::Live;std::string reason;bool prepared=false,pending=false;void audioPrepared(bool b){prepared=b;}bool presentationPending()const{return pending;}SpectatorLiveStatus status(){return state;}void noteWorldPartial(std::string r){reason=r;}void worldCauseRecovered(std::string const&r){if(reason==r)reason.clear();}};}
namespace geode::log{template<class...T>void info(T const&...){}}
using namespace geode;
using namespace corum::ranked;
constexpr int FMOD_OK=0,FMOD_TIMEUNIT_MS=1;
struct FMODAudioEngine{unsigned time=0;std::string track="C:/GD/VOICE.mp3";bool paused=true;int seeks=0,resumes=0;
 struct Channel{FMODAudioEngine*owner;bool ready=true;int setPosition(unsigned value,int){if(!ready)return 1;owner->time=value;++owner->seeks;return FMOD_OK;}int getPosition(unsigned*out,int){*out=owner->time;return FMOD_OK;}}channel{this};Channel*getActiveMusicChannel(int){return &channel;}
 static FMODAudioEngine*sharedEngine(){static FMODAudioEngine a;return &a;}std::string getActiveMusic(int){return track;}unsigned getMusicTimeMS(int){return time;}
 void pauseMusic(int){paused=true;}void resumeMusic(int){paused=false;++resumes;}void setMusicTimeMS(unsigned t,bool,int){time=t;++seeks;}};
struct PlayLayer{int starts=0;void startMusic(){++starts;FMODAudioEngine::sharedEngine()->time=0;}};
struct Owner:PlayLayer{struct Fields{SpectatorAudioClockPolicy spectatorAudioPolicy;bool spectatorMode=true,survivalLiveLevel=false,debugPlaybackScene=true,spectatorAudioStarted=false,spectatorAudioPaused=true;std::string spectatorAudioContext;double spectatorAudioElapsed=-1;std::chrono::steady_clock::time_point spectatorAudioChangedAt{};std::optional<SpectatorRemoteFrame> spectatorRenderedFrame;}fields;Fields*m_fields=&fields;bool m_isPaused=false,authorized=true;SpectatorLiveRuntime transport;
 bool spectatorCameraActive(){return authorized;}SpectatorLiveRuntime&spectatorTransport(){return transport;}
 ${method('void startMusic()')}
 ${method('void syncSpectatorAudio()')}
};
int main(){
 SpectatorAudioClockPolicy policy;assert(!policy.needsSeek("track",0,0,0,0));
 for(int n=0;n<500;++n){assert(!policy.needsSeek("track",0,0,0,n*8.));}assert(policy.seeks==0);
 for(int n=1;n<=10800;++n){assert(!policy.needsSeek("track",n*16.,n*16.+(n%20),n*16.,n*16.));}assert(policy.seeks==0);
 double clock=200000;assert(!policy.needsSeek("track",clock,clock+400,clock,clock));
 assert(!policy.needsSeek("track",clock+500,clock+900,clock+500,clock+500));
 assert(policy.needsSeek("track",clock+800,clock+1200,clock+800,clock+800));policy.sought(clock+800,true);
 assert(!policy.needsSeek("track",clock+900,clock+1300,clock+900,clock+900));assert(policy.seeks==1);
 Owner o;auto* audio=FMODAudioEngine::sharedEngine();o.startMusic();assert(o.starts==0);o.syncSpectatorAudio();assert(audio->paused&&o.starts==0);
 SpectatorGhostPose p;p.audio=SpectatorAudioSample{spectatorTrackHash("/mobile/GD/VOICE.mp3"),62150};
 o.fields.spectatorRenderedFrame=SpectatorRemoteFrame{{"m",1,0,123,"a",1,1},0,p,60000,0};o.transport.state=SpectatorLiveStatus::Syncing;o.syncSpectatorAudio();assert(o.starts==1&&audio->paused);
 o.transport.state=SpectatorLiveStatus::Live;o.syncSpectatorAudio();assert(o.starts==1&&audio->time==62150&&!audio->paused&&audio->seeks==1);
 o.syncSpectatorAudio();assert(o.starts==1&&audio->seeks==1);
 o.transport.state=SpectatorLiveStatus::Syncing;o.syncSpectatorAudio();assert(audio->paused);o.transport.state=SpectatorLiveStatus::Live;o.syncSpectatorAudio();assert(!audio->paused&&audio->seeks==1);
 o.fields.spectatorRenderedFrame->pose->dead=true;o.syncSpectatorAudio();assert(audio->paused);
 o.fields.spectatorRenderedFrame->context.attemptId="next";o.fields.spectatorRenderedFrame->context.generation=2;o.fields.spectatorRenderedFrame->publisherElapsedMs=0;o.fields.spectatorRenderedFrame->pose->dead=false;o.fields.spectatorRenderedFrame->pose->audio->positionMs=150;o.syncSpectatorAudio();assert(audio->time==150&&!audio->paused&&o.starts==1);
 audio->channel.ready=false;o.fields.spectatorAudioPolicy.reset();o.fields.spectatorRenderedFrame->pose->audio->positionMs=900;o.syncSpectatorAudio();assert(audio->paused&&!o.transport.prepared);
 audio->channel.ready=true;o.fields.spectatorAudioPolicy.lastSeek=-1e12;o.transport.pending=true;o.syncSpectatorAudio();assert(audio->paused&&o.transport.prepared);o.transport.pending=false;o.syncSpectatorAudio();assert(!audio->paused);
 audio->track="another-song.mp3";o.syncSpectatorAudio();assert(audio->paused&&o.transport.reason=="AUDIO_TRACK_UNAVAILABLE");
 o.fields.spectatorRenderedFrame->pose->audio.reset();o.syncSpectatorAudio();assert(audio->paused); // legacy data never guesses percent -> song time
}
`;
 writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
