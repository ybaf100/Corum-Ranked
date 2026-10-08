import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const play = readFileSync(join(root, 'src/PlayLayer.cpp'), 'utf8');
const overlay = readFileSync(join(root, 'src/ui/RankedEntryOverlay.cpp'), 'utf8');
const visuals = readFileSync(join(root, 'src/ui/RankedVisuals.hpp'), 'utf8');
function method(source, signature) {
    const start = source.indexOf(signature);
    assert(start >= 0, signature);
    const open = source.indexOf('{', start);
    let depth = 1, end = open + 1;
    while (depth && end < source.length) {
        if (source[end] === '{') ++depth;
        else if (source[end] === '}') --depth;
        ++end;
    }
    assert.equal(depth, 0);
    return source.slice(start, end);
}

test('actual native entry gate owns input/audio and releases once after shared server start', () => {
    const callback = method(play, 'bool blockEntryGameplayUpdate()');
    const presentation = method(overlay, 'void RankedEntryOverlay::present(');
    const nativeInit = method(play, 'bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects)');
    const inputHold = method(nativeInit, 'if (m_fields->nativeEntryHeld)');
    const countAttempt = method(play, 'void updateAttempts()');
    const cpp = `
#include "${join(root, 'src/domain/RoundResourcePolicy.hpp')}"
#include "${join(root, 'src/domain/EntryCountdown.hpp')}"
#include <cassert>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "${join(root, 'src/ui/RankedInput.hpp')}"
using namespace cocos2d;
using geode::cast::typeinfo_cast;
#include "${join(root, 'src/domain/FontMetrics.hpp')}"
namespace geode::log {template<class... T>void info(T...) {}}
using namespace geode;
struct PlayLayer;
struct FakeLabel {
    std::string text;float scale=0;unsigned char opacity=0;
    void setString(char const* s){text=s;}void setScale(float s){scale=s;}
    void setOpacity(unsigned char o){opacity=o;}
    char const* getFntFile()const{return "CorumSansCountdown.fnt";}
};
namespace corum::ranked::ui {
using CCLabelBMFont=FakeLabel;
${method(visuals, 'inline float fontScale(')}
${method(visuals, 'inline void setLabelScale(')}
namespace i18n {bool korean=false;bool isKorean(){return korean;}}
struct RankedEntryOverlay {
    FakeLabel heading,digit;FakeLabel* m_heading=&heading;FakeLabel* m_digit=&digit;
    int removed=0,releases=0;
    static RankedEntryOverlay* create();
    void present(bool,std::optional<std::int64_t>);
    void dismiss(){++removed;}
    void releaseToGameplay(void*){++releases;}
    void synchronizeTouchPriority(PlayLayer*){} // dispatcher behavior covered in RankedPauseInputNativeTest
};
std::vector<std::unique_ptr<RankedEntryOverlay>> overlays;
RankedEntryOverlay* RankedEntryOverlay::create(){overlays.push_back(std::make_unique<RankedEntryOverlay>());return overlays.back().get();}
}
struct FakeButton:CCMenuItem {int writes=0;bool isEnabled()const{return enabled;}
void setEnabled(bool value){enabled=value;++writes;}};
struct UILayer:CCLayer {
    bool keyboard=true,keypad=true,held=true;int resets=0,menuEnables=0,menuDisables=0;
    FakeButton pause;FakeButton* m_pauseBtn=&pause;
    CCMenu pauseMenu;
    UILayer(){pause.parent=&pauseMenu;pauseMenu.item=&pause;onEnter();pauseMenu.onEnter();}
    bool isTouchEnabled()const{return touch;}bool isKeyboardEnabled()const{return keyboard;}
    bool isKeypadEnabled()const{return keypad;}
    void setKeyboardEnabled(bool value){keyboard=value;}
    void setKeypadEnabled(bool value){keypad=value;}void resetAllButtons(){held=false;++resets;}
    void enableMenu(){++menuEnables;m_pauseBtn->setEnabled(true);}
    void disableMenu(){++menuDisables;m_pauseBtn->setEnabled(false);}
};
struct PlayerObject {bool held=true;int releases=0;void releaseAllButtons(){held=false;++releases;}};
struct FMODAudioEngine {
    int pauses=0,resumes=0;bool paused=false;
    static FMODAudioEngine* sharedEngine(){static FMODAudioEngine engine;return &engine;}
    void pauseAllMusic(bool state){++pauses;paused=state;}
    void resumeAllMusic(){++resumes;paused=false;}
};
struct PlayLayer {
    static inline PlayLayer* current=nullptr;
    std::shared_ptr<int> life=std::make_shared<int>(0);
    bool m_isPaused=true,running=true,touch=false,keyboard=false;
    int musicStarts=0,quitCalls=0,touchWrites=0,keyboardWrites=0,attemptCounts=0;
    UILayer* m_uiLayer=nullptr;PlayerObject* m_player1=nullptr;PlayerObject* m_player2=nullptr;
    std::vector<int> m_queuedButtons;
    PlayLayer(){current=this;}
    virtual ~PlayLayer(){if(current==this)current=nullptr;}
    static PlayLayer* get(){return current;}
    bool isRunning()const{return running;}
    bool isTouchEnabled()const{return touch;}bool isKeyboardEnabled()const{return keyboard;}
    void setTouchEnabled(bool value){touch=value;++touchWrites;}
    void setKeyboardEnabled(bool value){keyboard=value;++keyboardWrites;}
    void startMusic(){++musicStarts;}
    void updateAttempts(){assert(m_queuedButtons.empty());if(m_uiLayer){assert(!m_uiLayer->held);}
        if(m_player1){assert(!m_player1->held);}if(m_player2){assert(!m_player2->held);}++attemptCounts;}
    void addChild(corum::ranked::ui::RankedEntryOverlay*,int){}
};
namespace corum::ranked::ui {${presentation}}
namespace geode {
template<class T>struct WeakRef {
    T* pointer;std::weak_ptr<int> life;
    explicit WeakRef(T* p):pointer(p),life(p->life){}
    struct Locked {T* pointer;explicit operator bool()const{return pointer!=nullptr;}T* data()const{return pointer;}};
    Locked lock()const{return {life.expired()?nullptr:pointer};}
};
}
std::vector<std::function<void()>> deferred;
void queueInMainThread(std::function<void()> callback){deferred.push_back(std::move(callback));}
void drain(){auto work=std::move(deferred);deferred.clear();for(auto& callback:work)callback();}
namespace corum::ranked {
struct RankedRuntime {
    struct Entry {bool enteredA=false,enteredB=false;};
    struct Match {std::string matchId="match",state="ROUND_ENTERING";int roundNumber=1,deathmatchSequence=0;Entry entry;};
    struct View {Match match;} data;
    int level=123,ticks=0,notes=0,nativePosts=0,arms=0,starts=0;
    bool canEnter=false,armAllowed=true,postQueued=false,startGranted=false;
    std::optional<std::int64_t> remaining;
    static RankedRuntime& get(){static RankedRuntime runtime;return runtime;}
    View const& view()const{return data;}void tick(){++ticks;}
    int currentLevelId()const{return level;}bool canEnterCurrentLevel()const{return canEnter;}
    void noteNativeRoundSceneEntered(int id){assert(id==level);++notes;if(!postQueued){postQueued=true;++nativePosts;}}
    std::optional<std::int64_t> nativeEntryCountdownMillis()const{return remaining;}
    bool nativeEntryStartGranted()const{return startGranted;}
    bool armCurrentLevelForGameplay(){++arms;return armAllowed;}
};
}
struct CorumRankedPlayLayer:PlayLayer {
    struct Fields {
        bool nativeEntryHeld=true,autoExitRequested=false,entryWasPaused=false;
        bool entryTouchEnabled=true,entryKeyboardEnabled=true;
        bool entryUIKeyboardEnabled=false,entryUIKeypadEnabled=false,entryUIPauseEnabled=false;
        bool entryAttemptCountDeferred=false;
        int levelId=123;
        struct Context {std::string matchId="match";int roundNumber=1,deathmatchSequence=0;} sceneContext;
        corum::ranked::ui::RankedEntryOverlay* entryOverlay=nullptr;
        std::chrono::steady_clock::time_point attemptJournalReadyAt{};
    } fields;
    Fields* m_fields=&fields;
    void onQuit(){++quitCalls;}
    bool blocksLocalEvidence(){return false;}
    void initializeEntryInputHold(){${inputHold}}
    ${countAttempt}
    ${callback}
};
int main(){
    using namespace corum::ranked;
    auto& runtime=RankedRuntime::get();auto& audio=*FMODAudioEngine::sharedEngine();
    CorumRankedPlayLayer scene;UILayer uiLayer;PlayerObject player1,player2;
    scene.m_uiLayer=&uiLayer;scene.m_player1=&player1;scene.m_player2=&player2;
    scene.m_isPaused=false;scene.touch=true;scene.keyboard=true;scene.m_queuedButtons={1,2};
    scene.updateAttempts();scene.updateAttempts();assert(scene.attemptCounts==0&&scene.fields.entryAttemptCountDeferred);
    scene.initializeEntryInputHold();auto& overlay=*scene.fields.entryOverlay;
    assert(uiLayer.touch&&!uiLayer.keyboard&&!uiLayer.keypad&&!uiLayer.pause.enabled&&!uiLayer.held);
    auto* inputHandler=CCDirector::sharedDirector()->dispatcher.findHandler(&uiLayer);
    assert(inputHandler&&uiLayer.touchWrites==0);
    assert(uiLayer.resets==1&&player1.releases==1&&player2.releases==1&&!player1.held&&!player2.held);
    scene.running=false;scene.m_queuedButtons={1,2};
    assert(scene.blockEntryGameplayUpdate());
    assert(runtime.nativePosts==0&&runtime.arms==0&&runtime.starts==0);
    assert(scene.m_queuedButtons.empty()&&audio.paused&&scene.m_isPaused&&!scene.touch&&!scene.keyboard);
    assert(overlay.heading.text=="WAITING FOR OPPONENT..."&&overlay.digit.text.empty());
    scene.running=true;runtime.remaining=0;
    assert(scene.blockEntryGameplayUpdate());assert(runtime.nativePosts==1&&runtime.arms==0);
    runtime.data.match.entry.enteredA=true;
    assert(scene.blockEntryGameplayUpdate());assert(runtime.nativePosts==1&&runtime.arms==0);
    runtime.data.match.entry.enteredB=true;runtime.remaining=3000;
    assert(scene.blockEntryGameplayUpdate());assert(overlay.heading.text=="STARTS IN..."&&overlay.digit.text=="3");
    assert(runtime.arms==0&&audio.resumes==0&&scene.musicStarts==0);
    runtime.remaining=930;assert(scene.blockEntryGameplayUpdate());assert(overlay.digit.text=="1");
    runtime.remaining=0;assert(scene.blockEntryGameplayUpdate());assert(runtime.arms==0);
    runtime.data.match.state="ROUND_PLAYING";runtime.canEnter=true;runtime.remaining.reset();
    assert(scene.blockEntryGameplayUpdate());assert(runtime.arms==0);
    runtime.remaining=0;runtime.armAllowed=false;
    scene.m_queuedButtons={1,2};uiLayer.held=true;player1.held=true;player2.held=true;
    assert(scene.blockEntryGameplayUpdate());assert(runtime.arms==1&&scene.fields.nativeEntryHeld);
    assert(scene.m_queuedButtons.empty()&&uiLayer.held&&player1.held&&player2.held&&scene.attemptCounts==0);
    runtime.armAllowed=true;assert(!scene.blockEntryGameplayUpdate());
    assert(runtime.arms==2&&!scene.fields.nativeEntryHeld&&!scene.m_isPaused&&scene.touch&&scene.keyboard);
    assert(scene.touchWrites==2&&scene.keyboardWrites==2&&overlay.removed==0&&overlay.releases==1&&scene.fields.entryOverlay==&overlay);
    assert(uiLayer.touch&&uiLayer.keyboard&&uiLayer.keypad&&uiLayer.pause.enabled&&uiLayer.resets==2);
    assert(uiLayer.touchWrites==0&&CCDirector::sharedDirector()->dispatcher.findHandler(&uiLayer)==inputHandler);
    auto* pauseHandler=CCDirector::sharedDirector()->dispatcher.findHandler(&uiLayer.pauseMenu);
    assert(pauseHandler&&pauseHandler->m_nPriority<inputHandler->m_nPriority);
    auto& dispatch=CCDirector::sharedDirector()->dispatcher;dispatch.began(1);dispatch.ended(1);
    assert(uiLayer.pause.activations==1); // enabled=true alone did not test this path before
    assert(player1.releases==2&&player2.releases==2&&!player1.held&&!player2.held);
    assert(scene.attemptCounts==1&&!scene.fields.entryAttemptCountDeferred);
    assert(audio.resumes==1&&!audio.paused&&scene.musicStarts==1);
    assert(scene.fields.attemptJournalReadyAt!=std::chrono::steady_clock::time_point{});
    int ticks=runtime.ticks,pauses=audio.pauses;
    for(int i=0;i<100;++i)assert(!scene.blockEntryGameplayUpdate());
    assert(runtime.ticks==ticks&&runtime.arms==2&&audio.pauses==pauses&&scene.musicStarts==1&&scene.attemptCounts==1);

    // Preserve the scene's pre-entry input/pause preferences rather than enable
    // every input unconditionally or resume music on an already-paused scene.
    runtime={};audio={};runtime.data.match.state="ROUND_PLAYING";runtime.canEnter=true;
    runtime.data.match.entry={true,true};runtime.remaining=0;
    CorumRankedPlayLayer paused;paused.fields.entryWasPaused=true;
    paused.fields.entryTouchEnabled=false;paused.fields.entryKeyboardEnabled=false;
    UILayer pausedUI;pausedUI.setTouchEnabled(false);pausedUI.keyboard=true;pausedUI.keypad=false;pausedUI.pause.enabled=false;
    paused.m_uiLayer=&pausedUI;paused.initializeEntryInputHold();
    assert(!paused.blockEntryGameplayUpdate());
    assert(paused.m_isPaused&&!paused.touch&&!paused.keyboard&&audio.paused&&audio.resumes==0&&paused.musicStarts==0);
    assert(!pausedUI.touch&&pausedUI.keyboard&&!pausedUI.keypad&&!pausedUI.pause.enabled&&!pausedUI.held);
    assert(pausedUI.menuEnables==0);

    // Regression: vanilla init has a temporarily disabled Pause button. Native
    // scene entry later enables it. The old GO code restored the init-time false
    // snapshot and permanently locked Pause. Our hold must stay locked, then
    // vanilla enableMenu must release it exactly once at the certified GO.
    runtime={};audio={};
    CorumRankedPlayLayer transient;UILayer transientUI;transient.m_uiLayer=&transientUI;
    transient.m_isPaused=false;transientUI.pause.enabled=false;
    transient.initializeEntryInputHold();assert(!transient.fields.entryUIPauseEnabled);
    transientUI.enableMenu();assert(transientUI.pause.enabled);
    assert(transient.blockEntryGameplayUpdate());assert(!transientUI.pause.enabled);
    runtime.data.match.entry={true,true};runtime.remaining=0;runtime.startGranted=true;
    assert(!transient.blockEntryGameplayUpdate());
    assert(!transient.m_isPaused&&transientUI.pause.enabled&&transientUI.menuEnables==2);
    for(int i=0;i<20;++i)assert(!transient.blockEntryGameplayUpdate());
    assert(transientUI.menuEnables==2);

    // An ordinary scene never acquires/releases a Ranked entry menu lock.
    {runtime={};CorumRankedPlayLayer ordinary;UILayer ordinaryUI;ordinary.m_uiLayer=&ordinaryUI;
    ordinary.fields.nativeEntryHeld=false;ordinaryUI.pause.enabled=false;
    assert(!ordinary.blockEntryGameplayUpdate());
    assert(!ordinaryUI.pause.enabled&&ordinaryUI.menuEnables==0&&ordinaryUI.menuDisables==0);}

    // An exact server startsAt certificate can release at GO while the latest
    // poll still says ENTERING. No extra polling RTT may extend the countdown.
    runtime={};audio={};runtime.data.match.entry={true,true};runtime.remaining=0;
    CorumRankedPlayLayer certified;certified.updateAttempts();
    assert(certified.blockEntryGameplayUpdate());assert(runtime.arms==0&&certified.attemptCounts==0);
    runtime.startGranted=true;runtime.armAllowed=false;
    assert(certified.blockEntryGameplayUpdate());assert(runtime.arms==1&&certified.attemptCounts==0);
    runtime.armAllowed=true;assert(!certified.blockEntryGameplayUpdate());
    assert(certified.attemptCounts==1&&!certified.fields.entryAttemptCountDeferred&&runtime.arms==2);
    assert(!certified.blockEntryGameplayUpdate());assert(certified.attemptCounts==1&&runtime.arms==2);

    // Four context dimensions, including a reused map in a new round/DM, must
    // defer teardown outside the native frame stack and never claim new entry.
    for(int dimension=0;dimension<5;++dimension){
        runtime={};audio={};CorumRankedPlayLayer stale;stale.updateAttempts();
        if(dimension==0)runtime.data.match.matchId="other";
        if(dimension==1)++runtime.level;
        if(dimension==2)++runtime.data.match.roundNumber;
        if(dimension==3)++runtime.data.match.deathmatchSequence;
        if(dimension==4)runtime.data.match.state="CANCELLED";
        assert(stale.blockEntryGameplayUpdate());assert(stale.blockEntryGameplayUpdate());
        assert(stale.fields.autoExitRequested&&stale.quitCalls==0&&deferred.size()==1);
        assert(runtime.notes==0&&runtime.arms==0);drain();assert(stale.quitCalls==1&&stale.attemptCounts==0);
    }
    runtime={};runtime.data.match.matchId="other";
    auto destroyed=std::make_unique<CorumRankedPlayLayer>();
    assert(destroyed->blockEntryGameplayUpdate());destroyed.reset();drain();
    CorumRankedPlayLayer departed;assert(departed.blockEntryGameplayUpdate());
    CorumRankedPlayLayer replacement;drain();assert(departed.quitCalls==0&&replacement.quitCalls==0);

    // Ordinary Practice never enters this gate or mutates spectator/gameplay
    // state, audio, queued inputs, or the process-wide native-entry request.
    runtime={};audio={};CorumRankedPlayLayer ordinary;ordinary.fields.nativeEntryHeld=false;
    ordinary.m_isPaused=false;ordinary.touch=true;ordinary.keyboard=true;ordinary.m_queuedButtons={1};
    UILayer ordinaryUI;PlayerObject ordinaryPlayer;
    ordinary.m_uiLayer=&ordinaryUI;ordinary.m_player1=&ordinaryPlayer;
    ordinary.initializeEntryInputHold();
    assert(!ordinary.blockEntryGameplayUpdate());
    assert(runtime.ticks==0&&runtime.notes==0&&runtime.arms==0&&audio.pauses==0);
    assert(!ordinary.m_isPaused&&ordinary.touch&&ordinary.keyboard&&ordinary.m_queuedButtons.size()==1);
    assert(ordinaryUI.touch&&ordinaryUI.keyboard&&ordinaryUI.keypad&&ordinaryUI.pause.enabled&&ordinaryUI.resets==0);
    assert(ordinaryPlayer.held&&ordinaryPlayer.releases==0&&ordinary.fields.entryOverlay==nullptr);

    // Exercise the real overlay's localization/presentation and real countdown
    // policy at 60/120/144/240Hz. Each observation is calculated from the same
    // remaining server time, independent of the number of prior rendered frames.
    ui::i18n::korean=true;ui::RankedEntryOverlay localized;
    localized.present(false,std::nullopt);assert(localized.heading.text=="상대 입장 대기 중...");
    localized.present(true,2500);assert(localized.heading.text=="시작까지..."&&localized.digit.text=="3");
    for(int hz:{60,120,144,240}){
        ui::RankedEntryOverlay animated;
        for(int frame=0;frame<=hz*3;++frame){
            auto const elapsed=static_cast<std::int64_t>(std::llround(frame*1000.0/hz));
            auto const remaining=std::max<std::int64_t>(0,3000-elapsed);
            animated.present(true,remaining);
            auto const expected=entryCountdownFrame(remaining);
            assert(animated.digit.text==(expected.digit?std::to_string(expected.digit):""));
            assert(animated.digit.scale==corumFontScale(1.7f*expected.scale,"CorumSansCountdown.fnt")&&animated.digit.opacity==expected.opacity);
        }
        animated.present(true,2500);assert(animated.digit.text=="3"&&animated.digit.opacity==255);
        animated.present(true,1500);assert(animated.digit.text=="2"&&animated.digit.opacity==255);
        animated.present(true,500);assert(animated.digit.text=="1"&&animated.digit.opacity==255);
        animated.present(true,0);assert(animated.digit.text.empty()&&animated.digit.opacity==0);
    }
    for(auto boundary:{3000,2000,1000}){
        auto initial=entryCountdownFrame(boundary),appear=entryCountdownFrame(boundary-60),hold=entryCountdownFrame(boundary-500),fade=entryCountdownFrame(boundary-950);
        assert(initial.scale>appear.scale&&appear.scale>hold.scale&&hold.scale==1);
        assert(initial.opacity==0&&appear.opacity>0&&appear.opacity<255&&hold.opacity==255&&fade.opacity<hold.opacity);
    }
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-entry-native-'));
    try {
        mkdirSync(join(dir, 'Geode'));
        writeFileSync(join(dir, 'Geode/Geode.hpp'), readFileSync(join(root, 'tests/helpers/RankedInputNativeDouble.hpp'), 'utf8'));
        writeFileSync(join(dir, 'entry.cpp'), cpp);
        for (const platform of ['GEODE_IS_WINDOWS', 'GEODE_IS_IOS']) {
            const binary = join(dir, platform);
            execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                `-D${platform}=1`, '-I', dir, join(dir, 'entry.cpp'), '-o', binary], { stdio: 'pipe' });
            execFileSync(binary, [], { stdio: 'pipe' });
        }
    } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('native entry guards the existing world/evidence owners without extra hot detours', () => {
    const post = method(play, 'void postUpdate(float dt)');
    assert(post.indexOf('blockEntryGameplayUpdate()') < post.indexOf('syncMatchGameplayPause'));
    assert(post.indexOf('blockEntryGameplayUpdate()') < post.indexOf('PlayLayer::postUpdate'));
    assert.match(method(play, 'bool scoringEnabled()'), /!m_fields->nativeEntryHeld/);
    const cameraHook = play.slice(play.indexOf('$modify(CorumRankedSpectatorCameraHook'));
    const world = method(cameraHook, 'void update(float dt)');
    assert(world.indexOf('blockEntryGameplayUpdate()') < world.indexOf('GJBaseGameLayer::update'));
    assert.match(overlay, /FontWeight::Black/);
    assert.doesNotMatch(method(play, 'bool blockEntryGameplayUpdate()'), /reportAttemptStart|reportAttemptProgress|reportAttemptEnd/);
});
