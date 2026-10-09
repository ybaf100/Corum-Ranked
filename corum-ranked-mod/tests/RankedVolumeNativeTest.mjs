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

test('personal menu volume persists, clamps corrupt values and refreshes only the owned BGM gain', () => {
    run(`
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <string>
struct Mod {
    float saved=1;int writes=0;
    static Mod* get(){static Mod m;return &m;}
    template<class T>T getSavedValue(char const* key,T){assert(std::string(key)=="ranked-volume");return static_cast<T>(saved);}
    template<class T>void setSavedValue(char const* key,T v){assert(std::string(key)=="ranked-volume");saved=v;++writes;}
};
namespace geode::prelude {using ::Mod;}
namespace corum::ranked {struct RankedAudioManager {int refreshes=0;static RankedAudioManager& get(){static RankedAudioManager a;return a;}void refreshVolume(){++refreshes;}};}
${header.replace('#pragma once', '')}
${control.replace(/^#include .*$/gm, '')}
int main(){using namespace corum::ranked;
    Mod::get()->saved=.4f;RankedVolumeControl c;assert(c.volume()==.4f);
    auto& audio=RankedAudioManager::get();c.setVolume(.4f);assert(audio.refreshes==0&&Mod::get()->writes==0);
    c.setVolume(0);assert(c.volume()==0&&audio.refreshes==1&&Mod::get()->saved==0);
    c.setVolume(.25f);assert(c.volume()==.25f&&audio.refreshes==2&&Mod::get()->saved==.25f);
    RankedVolumeControl reloaded;assert(reloaded.volume()==.25f);
    c.setVolume(-10);assert(c.volume()==0);c.setVolume(10);assert(c.volume()==1);
    c.setVolume(std::numeric_limits<float>::quiet_NaN());assert(c.volume()==1);
    Mod::get()->saved=std::numeric_limits<float>::infinity();RankedVolumeControl corrupt;assert(corrupt.volume()==1);
}
`);
});

test('Settings uses a localized bottom-right Corum modal and limits gain to menu BGM', () => {
    assert.match(screen, /settingsButton->setPosition\(\{winSize.width - 70, 30\}\)/);
    assert.match(screen, /tr\("settings", "Settings"\)/);
    assert.match(popup, /FontWeight::Black/);
    assert.match(popup, /RankedVolumeControl::get\(\).setVolume\(m_volume->getValue\(\)\)/);
    const exit=body(screen, 'void onExit() override');
    assert.match(exit, /closeSettings\(\)/);
    assert.doesNotMatch(exit, /endSession/);
    assert.doesNotMatch(screen, /m_volumeSession|beginSession|endSession/);
    assert.match(popup, /Lobby, matchmaking and results BGM/);
    assert.match(read('src/RankedAudioManager.cpp'), /playSound\(sound, group, true/);
    assert.doesNotMatch(control, /getMasterChannelGroup|createDSPByType|CCScheduler|setBackgroundMusicVolume|setEffectsVolume|->setVolume\(|setMode\(|playSound|setPosition\(|stopAllMusic/);
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
    void unschedule(int){}
#define schedule_selector(x) 0
    ${body(popup, 'void onExit() override').replace(' override', '')}
};
int main(){Popup external;RankedSettingsPopup popup;auto* d=Dispatcher::get();d->owners={&external,&popup};
    popup.onExit();assert(popup.m_closed&&d->owners.contains(&external)&&!d->owners.contains(&popup)&&Mod::get()->saves==1);
    popup.onExit();assert(Mod::get()->saves==1&&d->owners.size()==1);
}
`);
});
