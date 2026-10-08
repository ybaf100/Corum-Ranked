import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';

const read = path => readFileSync(new URL('../' + path, import.meta.url), 'utf8');
const control = read('src/RankedVolumeControl.cpp');
const header = read('src/RankedVolumeControl.hpp').replace('#include <Geode/Geode.hpp>', '').replace('private:', 'public:');
const popup = read('src/ui/RankedSettingsPopup.hpp');
const screen = read('src/RankedPopup.cpp');
function body(source, name) {
    const at = source.indexOf(name); assert(at >= 0, name);
    let end = source.indexOf('{', at) + 1, depth = 1;
    while (depth) { depth += (source[end] === '{') - (source[end] === '}'); ++end; }
    return source.slice(at, end);
}
function run(cpp) {
    const dir = mkdtempSync(join(tmpdir(), 'corum-volume-'));
    try {
        writeFileSync(join(dir, 'test.cpp'), cpp);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', join(dir, 'test.cpp'), '-o', join(dir, 'test')], {stdio: 'pipe'});
        execFileSync(join(dir, 'test'), [], {stdio: 'pipe'});
    } finally { rmSync(dir, {recursive: true, force: true}); }
}

test('actual Ranked output DSP scales music, SFX and both fades, persists volume and releases only its own gain', () => {
    run(`
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <limits>
#include <set>
#include <string>
#include <vector>
namespace cocos2d { struct CCNode {virtual ~CCNode()=default; virtual void update(float) {}}; }
struct Mod {
    float saved=1;int writes=0;
    static Mod* get(){static Mod m;return &m;}
    template<class T>T getSavedValue(char const* key,T){assert(std::string(key)=="ranked-volume");return static_cast<T>(saved);}
    template<class T>void setSavedValue(char const* key,T v){assert(std::string(key)=="ranked-volume");saved=v;++writes;}
};
struct CCScheduler {
    std::set<cocos2d::CCNode*> targets;
    static CCScheduler* get(){static CCScheduler s;return &s;}
    void scheduleUpdateForTarget(cocos2d::CCNode* n,int,bool){targets.insert(n);}
    void unscheduleUpdateForTarget(cocos2d::CCNode* n){targets.erase(n);}
};
namespace geode::prelude {using ::Mod;using ::CCScheduler;namespace log {template<class...T>void warn(T...) {}}}
enum FMOD_RESULT {FMOD_OK,FMOD_ERR_NOTREADY};
constexpr int FMOD_DSP_TYPE_FADER=1,FMOD_CHANNELCONTROL_DSP_TAIL=-3;
namespace FMOD {
struct DSP {
    float gain=1;int writes=0,releases=0;bool attached=false;
    FMOD_RESULT setWetDryMix(float pre,float post,float dry){assert(pre==1&&dry==0);gain=post;++writes;return FMOD_OK;}
    FMOD_RESULT release(){assert(!attached);++releases;return FMOD_OK;}
};
struct ChannelGroup {
    std::vector<DSP*> dsps;bool failAttach=false,failRemove=false;float ownGain=.7f;
    FMOD_RESULT addDSP(int position,DSP* p){assert(position==FMOD_CHANNELCONTROL_DSP_TAIL);if(failAttach)return FMOD_ERR_NOTREADY;dsps.push_back(p);p->attached=true;return FMOD_OK;}
    FMOD_RESULT removeDSP(DSP* p){if(failRemove)return FMOD_ERR_NOTREADY;auto it=std::find(dsps.begin(),dsps.end(),p);assert(it!=dsps.end());dsps.erase(it);p->attached=false;return FMOD_OK;}
    float output(float native,float envelope=1){for(auto* d:dsps)native*=d->gain;return native*envelope*ownGain;}
};
struct System {
    ChannelGroup output;std::vector<DSP*> created;bool failCreate=false;
    ~System(){for(auto* d:created)delete d;}
    FMOD_RESULT getMasterChannelGroup(ChannelGroup** p){*p=&output;return FMOD_OK;}
    FMOD_RESULT createDSPByType(int type,DSP** p){assert(type==FMOD_DSP_TYPE_FADER);if(failCreate)return FMOD_ERR_NOTREADY;*p=new DSP;created.push_back(*p);return FMOD_OK;}
};
}
struct FMODAudioEngine {
    FMOD::System* m_system=nullptr;float m_musicVolume=.65f,m_sfxVolume=.25f;
    static FMODAudioEngine* sharedEngine(){static FMODAudioEngine e;return &e;}
};
${header.replace('#pragma once', '')}
${control.replace(/^#include .*$/gm, '')}
int main(){using namespace corum::ranked;
    Mod::get()->saved=.4f;RankedVolumeControl c;assert(c.volume()==.4f);
    FMOD::System system;auto* engine=FMODAudioEngine::sharedEngine();engine->m_system=&system;
    auto& out=system.output;FMOD::DSP external;external.gain=.8f;out.dsps.push_back(&external);
    auto session=c.beginSession();assert(out.dsps.size()==2&&CCScheduler::get()->targets.contains(&c));
    auto* gain=out.dsps.back();assert(gain!=&external&&gain->gain==.4f);
    auto const close=[](float a,float b){assert(std::abs(a-b)<.000001f);};
    close(out.output(engine->m_musicVolume,.3f),.65f*.3f*.7f*.8f*.4f);
    close(out.output(engine->m_musicVolume,.7f),.65f*.7f*.7f*.8f*.4f);
    close(out.output(engine->m_sfxVolume),.25f*.7f*.8f*.4f);
    c.setVolume(0);assert(out.output(.9f)==0&&gain==out.dsps.back());
    c.setVolume(.25f);assert(Mod::get()->saved==.25f&&gain->gain==.25f);
    assert(engine->m_musicVolume==.65f&&engine->m_sfxVolume==.25f&&out.ownGain==.7f&&external.gain==.8f);
    auto const writes=gain->writes;for(int frame=0;frame<100;++frame)c.update(0);assert(gain->writes==writes);
    auto next=c.beginSession();assert(next!=session&&out.dsps.size()==2&&gain->releases==1);
    c.endSession(session);assert(out.dsps.size()==2); // stale owner cannot end a newer session
    gain=out.dsps.back();out.failRemove=true;c.endSession(next);
    assert(gain->gain==1&&gain->releases==0&&!CCScheduler::get()->targets.contains(&c));
    out.failRemove=false;next=c.beginSession();assert(out.dsps.size()==2&&gain->gain==.25f);
    c.endSession(next);assert(out.dsps.size()==1&&out.dsps.front()==&external&&gain->releases==1);
    close(out.output(.65f),.65f*.7f*.8f); // ordinary GD restored, external mixer unchanged
    RankedVolumeControl reloaded;assert(reloaded.volume()==.25f);
    c.setVolume(-10);assert(c.volume()==0);c.setVolume(10);assert(c.volume()==1);
    c.setVolume(std::numeric_limits<float>::quiet_NaN());assert(c.volume()==1);
    Mod::get()->saved=std::numeric_limits<float>::infinity();RankedVolumeControl corrupt;assert(corrupt.volume()==1);
    out.failAttach=true;next=c.beginSession();assert(out.dsps.size()==1&&system.created.back()->releases==1);
    auto const attempts=system.created.size();for(int frame=0;frame<100;++frame)c.update(0);assert(system.created.size()==attempts);
    out.failAttach=false;c.m_retryAt={};c.update(0);assert(out.dsps.size()==2);c.endSession(next);
    engine->m_system=nullptr;next=c.beginSession();assert(!c.m_gain);engine->m_system=&system;c.update(0);assert(c.m_gain);c.endSession(next);
}
`);
});

test('Settings uses a localized bottom-right Corum modal and keeps the volume session across native gameplay', () => {
    assert.match(screen, /settingsButton->setPosition\(\{winSize.width - 60, 22\}\)/);
    assert.match(screen, /tr\("settings", "Settings"\)/);
    assert.match(popup, /FontWeight::Black/);
    assert.match(popup, /RankedVolumeControl::get\(\).setVolume\(m_volume->getValue\(\)\)/);
    const exit=body(screen, 'void onExit() override');
    assert.match(exit, /closeSettings\(\)/);
    assert.doesNotMatch(exit, /endSession/);
    assert.match(body(screen, 'void onCloseLayer('), /endSession\(m_volumeSession\)/);
    assert.match(body(screen, '~CorumRankedLayer()'), /endSession\(m_volumeSession\)/);
    assert.doesNotMatch(control, /setBackgroundMusicVolume|setEffectsVolume|->setVolume\(|setMode\(|playSound|setPosition\(|stopAllMusic/);
});

test('closed retained Settings releases its own modal priority and saves once on scene exit', () => {
    run(`
#include <cassert>
#include <set>
struct CCObject {virtual ~CCObject()=default;};
struct Dispatcher {std::set<CCObject*> owners;static Dispatcher* get(){static Dispatcher d;return &d;}void unregisterForcePrio(CCObject* p){owners.erase(p);}};
using CCTouchDispatcher=Dispatcher;
struct Mod {int saves=0;static Mod* get(){static Mod m;return &m;}void saveData(){++saves;}};
struct Popup:CCObject {int exits=0;void onExit(){++exits;}};
struct RankedSettingsPopup:Popup {bool m_closed=false,m_exited=false,m_dirty=true;
    ${body(popup, 'void onExit() override').replace(' override', '')}
};
int main(){Popup external;RankedSettingsPopup popup;auto* d=Dispatcher::get();d->owners={&external,&popup};
    popup.onExit();assert(popup.m_closed&&d->owners.contains(&external)&&!d->owners.contains(&popup)&&Mod::get()->saves==1);
    popup.onExit();assert(Mod::get()->saves==1&&d->owners.size()==1);
}
`);
});
