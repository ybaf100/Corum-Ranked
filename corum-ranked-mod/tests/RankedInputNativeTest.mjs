import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, mkdirSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join, resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = path => readFileSync(join(root, path), 'utf8');
function body(source, signature) {
    const start = source.indexOf(signature); assert(start >= 0, signature);
    const open = source.indexOf('{', start); let end = open + 1, depth = 1;
    while (depth) { if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end; }
    return source.slice(start, end);
}
function execute(cpp) {
    const dir = mkdtempSync(join(tmpdir(), 'corum-input-native-'));
    try {
        mkdirSync(join(dir, 'Geode'));
        writeFileSync(join(dir, 'Geode/Geode.hpp'), read('tests/helpers/RankedInputNativeDouble.hpp'));
        writeFileSync(join(dir, 'test.cpp'), cpp);
        for (const debug of [false, true]) {
            const binary = join(dir, debug ? 'debug' : 'official');
            execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I', dir,
                ...(debug ? ['-DCORUM_RANKED_DEBUG_SPECTATE=1'] : []), join(dir, 'test.cpp'), '-o', binary], {stdio:'pipe'});
            execFileSync(binary, [], {stdio:'pipe'});
        }
    } finally { rmSync(dir, {recursive:true, force:true}); }
}

test('actual scoped cancellation clears TrackingTouch and old claims without re-registering delegates', () => {
    execute(`
#include "${join(root, 'src/ui/RankedInput.hpp')}"
using namespace cocos2d;
using namespace corum::ranked::ui;
int main() {
    auto& d = CCDirector::sharedDirector()->dispatcher;
    CCMenu menu; CCMenuItem oldItem, newItem; menu.item = &oldItem; oldItem.parent = &menu;
    menu.onEnter(); d.began(1); assert(menu.m_eState == kCCMenuStateTrackingTouch);
    // Existing pointer-only cleanup plus input suspension is not cancellation.
    menu.m_pSelectedItem = nullptr; menu.setTouchEnabled(false); menu.setTouchEnabled(true);
    menu.item = &newItem; newItem.parent = &menu; d.began(2); d.ended(2);
    assert(newItem.activations == 0 && menu.m_eState == kCCMenuStateTrackingTouch);
    cancelMenuTouch(&menu); d.began(3); d.ended(3); assert(newItem.activations == 1);
    d.began(4); int adds = d.adds, removes = d.removes;
    auto* handler = d.findHandler(&menu); assert(handler);
    cancelMenuTouch(&menu);
    assert(menu.m_pSelectedItem == nullptr && menu.m_eState == kCCMenuStateWaiting && !newItem.selected);
    assert(d.adds == adds && d.removes == removes && d.findHandler(&menu) == handler);
    // A finger from the previous UI must neither activate nor assert on the new UI.
    menu.item = &oldItem; d.ended(4); assert(oldItem.activations == 0);
    d.began(5); d.ended(5); assert(oldItem.activations == 1);
    cancelMenuTouch(nullptr); menu.onExit(); cancelMenuTouch(&menu);
    assert(d.handlers.empty());
}
`);
});

test('actual Pause menu repair routes touch before a swallowing input delegate and preserves modals', () => {
    execute(`
#include "${join(root, 'src/ui/RankedInput.hpp')}"
using namespace cocos2d;
using namespace corum::ranked::ui;
struct Gameplay : CCLayer { int hits=0; bool ccTouchBegan(CCTouch*, CCEvent*) override { ++hits; return true; } };
int main() {
    auto& d = CCDirector::sharedDirector()->dispatcher;
    Gameplay input, modal; CCMenu pauseMenu; CCMenuItem pauseButton;
    pauseMenu.item = &pauseButton; pauseButton.parent = &pauseMenu;
    input.onEnter(); pauseMenu.onEnter(); d.began(1); d.ended(1); assert(pauseButton.activations == 1);
    // Cocos gives equal-priority delegates newest-first. Re-registering the
    // swallowing input after its child is enough to defeat an enabled Pause.
    input.setTouchEnabled(false); input.setTouchEnabled(true);
    d.began(2); d.ended(2); assert(input.hits == 1 && pauseButton.isEnabled() && pauseButton.activations == 1);
    int adds = d.adds, removes = d.removes;
    restorePauseMenuInput(&pauseMenu, &input);
    assert(d.adds == adds && d.removes == removes);
    assert(d.findHandler(&pauseMenu)->m_nPriority < d.findHandler(&input)->m_nPriority);
    d.began(3); d.ended(3); assert(pauseButton.activations == 2 && input.hits == 1);
    int changes = d.priorityWrites; restorePauseMenuInput(&pauseMenu, &input);
    assert(d.priorityWrites == changes); // never lower an already-correct menu
    d.addTargetedDelegate(&modal, -500, true); restorePauseMenuInput(&pauseMenu, &input);
    d.began(4); d.ended(4); assert(modal.hits == 1 && pauseButton.activations == 2);
    d.removeDelegate(&modal);
    pauseMenu.onExit(); input.onExit(); restorePauseMenuInput(nullptr, &input);
    assert(d.handlers.empty());
}
`);
});

test('actual retained Ranked callbacks restore menu registration before onEnter, across rounds and results', () => {
    const popup = read('src/RankedPopup.cpp');
    execute(`
#include "${join(root, 'src/ui/RankedInput.hpp')}"
#include <string>
#include <optional>
using namespace cocos2d;
namespace geode::log { template<class... T> void info(T...) {} }
using namespace geode;
std::string operator""_spr(char const*s,std::size_t n){return {s,n};}
namespace corum::ranked {
struct RankedVolumeControl {int begins=0;static RankedVolumeControl& get(){static RankedVolumeControl c;return c;}unsigned long beginSession(){++begins;return 1;}};
struct Runtime {void cancelHistoryReads(){}struct View{struct{std::string state="ROUND_PLAYING";}match;}data;auto const& view(){return data;}};
}
struct RankedRuntime { static corum::ranked::Runtime& get(){static corum::ranked::Runtime r;return r;} };
#define schedule_selector(x) 0
struct CCLayerColor {
    CCMenu* nativeChild=nullptr; bool running=false,expectNativeTouch=true;
    virtual ~CCLayerColor()=default;
    virtual void onEnter(){ assert(nativeChild && nativeChild->isTouchEnabled()==expectNativeTouch); nativeChild->onEnter(); running=true; }
    virtual void onExit(){nativeChild->onExit();running=false;}
};
struct Root { CCMenu* menu; void removeAllChildrenWithCleanup(bool){assert(menu->m_eState==kCCMenuStateWaiting);menu->item=nullptr;} };
struct Menu : CCMenu {void removeAllChildrenWithCleanup(bool){assert(m_eState==kCCMenuStateWaiting && !m_pSelectedItem);item=nullptr;}};
struct CorumRankedLayer : CCLayerColor {
    struct Hover{void detach(){}};Hover*m_lobbyHover=nullptr;void*m_lobbyContents=nullptr;
    void* m_lobbySurvival=nullptr;void* m_lobbySolo=nullptr;void* m_lobbyPanel=nullptr;void* m_lobbyToolbar=nullptr;
    void setLobbyNodeInput(void*,bool){}
    Menu* m_menu=nullptr; Root* m_root=nullptr; void* m_rankTransitionLayer=nullptr;
    void* m_resultScoreLayer=nullptr;bool m_resultScorePending=false;
    struct Reconnect {bool attached=true;int cancellations=0;void cancel(){++cancellations;attached=false;}};
    struct ReconnectWeak{Reconnect* p;Reconnect* lock(){return p;}};std::optional<ReconnectWeak>m_historyReconnect;
    CCMenu* m_settingsMenu=nullptr;unsigned long m_volumeSession=0;void closeSettings(){}
    bool m_rankedChildScenePushed=false,m_enteringSongGate=false,m_enteringLevel=false,keypad=true;
    int refreshed=0,scheduled=0;void suspendHistoryThumbnail(){}void resetHistoryUi(){}void closeHistoryFilter(){}
    struct Channel {void dismiss(){}};struct ChannelWeak{Channel*p;Channel*lock(){return p;}};std::optional<ChannelWeak>m_updateChannelPopup;
    void setKeypadEnabled(bool v){keypad=v;}
    void schedule(int,float){++scheduled;} void unschedule(int){--scheduled;}
    void refresh(float){++refreshed;}void resetDynamicUiPointers(){}
    ${body(popup, 'void onEnter() override')}
    ${body(popup, 'void onExit() override')}
    ${body(popup, 'void clearUi()')}
};
int main(){
    auto& d=CCDirector::sharedDirector()->dispatcher; Menu menu;Root root{&menu};CorumRankedLayer lobby;
    lobby.m_menu=&menu;lobby.nativeChild=&menu;lobby.m_root=&root;
    CCMenuItem button;button.parent=&menu;menu.item=&button;lobby.onEnter();
    for(int round=1;round<=3;++round){
        d.began(10+round); assert(menu.m_eState==kCCMenuStateTrackingTouch);
        lobby.clearUi(); menu.item=&button; d.ended(10+round); assert(button.activations==round-1);
        d.began(20+round);d.ended(20+round);assert(button.activations==round);
        // Cover with a native child during a held finger, then return to the
        // retained round/result scene. The old claim must not follow the scene.
        d.began(30+round);lobby.m_rankedChildScenePushed=true;lobby.m_enteringLevel=true;
        lobby.onExit();assert(d.handlers.empty());lobby.onEnter();d.ended(30+round);
        assert(!lobby.m_rankedChildScenePushed && !lobby.m_enteringLevel && menu.m_eState==kCCMenuStateWaiting);
    }
    assert(lobby.refreshed==4 && lobby.scheduled==2 && lobby.keypad);
    RankedRuntime::get().data.match.state="MATCH_RESULT";
    lobby.clearUi();menu.item=&button;d.began(99);d.ended(99);assert(button.activations==4);
    // Covering a scene must not orphan a still-owned rank reveal layer.
    lobby.m_rankTransitionLayer=&button;lobby.onExit();assert(lobby.m_rankTransitionLayer==&button);
    assert(d.handlers.empty());
    lobby.expectNativeTouch=false;lobby.m_rankTransitionLayer=nullptr;lobby.m_resultScoreLayer=&button;lobby.onEnter();
    assert(!lobby.keypad&&!menu.isTouchEnabled());lobby.onExit();assert(lobby.m_resultScoreLayer==&button);
    lobby.m_resultScoreLayer=nullptr;lobby.m_resultScorePending=true;lobby.onEnter();
    assert(!lobby.keypad&&!menu.isTouchEnabled());lobby.onExit();
}
`);
});

test('countdown blocker explicitly releases touch/keypad on GO and canceled entry without new detours', () => {
    const overlay = read('src/ui/RankedEntryOverlay.cpp'), play = read('src/PlayLayer.cpp');
    execute(`
#include <Geode/Geode.hpp>
using namespace cocos2d;
struct RankedEntryOverlay : CCLayer {
    void cancelPauseTouch(){}
    void* m_play=nullptr;
    bool keypad=true,removed=false; int gameplayTouches=0;
    void setKeypadEnabled(bool v){keypad=v;}
    void removeFromParentAndCleanup(bool){assert(!touch&&!keypad);removed=true;}
    bool ccTouchBegan(CCTouch*,CCEvent*)override{++gameplayTouches;return true;}
    ${body(overlay,'void RankedEntryOverlay::registerWithTouchDispatcher()').replace('RankedEntryOverlay::','')}
    ${body(overlay,'void RankedEntryOverlay::dismiss()').replace('RankedEntryOverlay::','')}
};
int main(){
    auto& d=CCDirector::sharedDirector()->dispatcher;CCMenu menu;CCMenuItem button;menu.item=&button;menu.onEnter();
    for(int path=0;path<2;++path){RankedEntryOverlay blocker;blocker.onEnter();d.began(path+1);d.ended(path+1);
        assert(button.activations==path&&blocker.gameplayTouches==1);
        blocker.dismiss();assert(!d.findHandler(&blocker)&&blocker.removed);
        d.began(path+10);d.ended(path+10);assert(button.activations==path+1);blocker.onExit();}
    menu.onExit();assert(d.handlers.empty());
}
`);
    assert.match(body(play, 'bool blockEntryGameplayUpdate()')+body(play,'void releaseEntryControls()'), /entryOverlay->releaseToGameplay\(this\)/);
    assert.match(body(play, 'void onExit()'), /entryOverlay->dismiss\(\)/);
    assert.doesNotMatch(read('src/ui/RankedInput.hpp')+overlay, /\$modify|incrementForcePrio|registerForcePrio/);
});
