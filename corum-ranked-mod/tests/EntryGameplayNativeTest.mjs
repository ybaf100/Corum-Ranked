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
const preparation = readFileSync(join(root, 'src/RankedRoundPreparation.cpp'), 'utf8');
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
    const entryExit = method(play, 'void queueHeldEntryExit()');
    const nativeReturn = method(preparation, 'bool RankedRoundPreparation::returnFromNativeQuit(');
    const presentation = method(overlay, 'void RankedEntryOverlay::present(');
    const nativeInit = method(play, 'bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects)');
    const inputHold = method(nativeInit, 'if (m_fields->nativeEntryHeld)');
    const countAttempt = method(play, 'void updateAttempts()');
    const cpp = `
#include "${join(root, 'src/domain/RoundResourcePolicy.hpp')}"
#include "${join(root, 'src/domain/EntryCountdown.hpp')}"
#include "${join(root, 'src/domain/ServerClock.hpp')}"
#include <cassert>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include "${join(root, 'src/ui/RankedInput.hpp')}"
using namespace cocos2d;
using geode::cast::typeinfo_cast;
#include "${join(root, 'src/domain/FontMetrics.hpp')}"
namespace geode::log {template<class... T>void info(T...) {} template<class... T>void warn(T...) {}}
using namespace geode;
struct AppDelegate {bool m_gamePaused=false;static AppDelegate* get(){static AppDelegate a;return &a;}};
struct GJGameLevel {int refs=1;void retain(){++refs;}void release(){--refs;}};
struct SceneParent:CCNode {bool running=true;std::shared_ptr<int> life=std::make_shared<int>(0);bool isRunning()const{return running;}};
struct PlayLayer;
struct FakeLabel {
    std::string text;float scale=0;unsigned char opacity=0;int writes=0;
    void setString(char const* s){text=s;++writes;}void setScale(float s){scale=s;}
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
    EntryCountdownPresentation m_countdown;int m_headingState=-1,m_presentedDigit=-1;
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
    bool m_isPaused=true,running=true,touch=false,keyboard=false,m_hasCompletedLevel=false;
    SceneParent parent;bool attached=true,alive=true,refuseCompletion=false;
    int refs=1,completions=0,onEnters=0,nativePauseOpens=0;
    GJGameLevel level;
    std::function<void()> completionHook;
    int musicStarts=0,quitCalls=0,touchWrites=0,keyboardWrites=0,attemptCounts=0;
    UILayer* m_uiLayer=nullptr;PlayerObject* m_player1=nullptr;PlayerObject* m_player2=nullptr;
    std::vector<int> m_queuedButtons;
    PlayLayer(){current=this;CCDirector::sharedDirector()->runningScene=&parent;}
    virtual ~PlayLayer(){if(current==this)current=nullptr;}
    static PlayLayer* get(){return current;}
    void retain(){assert(alive);++refs;}void release(){assert(alive&&refs>0);if(--refs==0){alive=false;life.reset();}}
    SceneParent* getParent(){return attached?&parent:nullptr;}
    bool isRunning()const{return running;}
    bool isTouchEnabled()const{return touch;}bool isKeyboardEnabled()const{return keyboard;}
    void setTouchEnabled(bool value){touch=value;++touchWrites;}
    void setKeyboardEnabled(bool value){keyboard=value;++keyboardWrites;}
    void startMusic(){++musicStarts;}
    void onQuit(){++quitCalls;}
    void onEnter(){++onEnters;assert(false&&"entry recovery must not re-register a running scene");}
    void resume(){onEnter();}
    void onEnterTransitionDidFinish(){assert(alive&&refs>=2);++completions;
        if(!refuseCompletion)AppDelegate::get()->m_gamePaused=false;
        if(completionHook)completionHook();}
    void pauseGame(){if(!AppDelegate::get()->m_gamePaused&&!m_isPaused){++nativePauseOpens;m_isPaused=true;AppDelegate::get()->m_gamePaused=true;}}
    void updateAttempts(){assert(m_queuedButtons.empty());if(m_uiLayer){assert(!m_uiLayer->held);}
        if(m_player1){assert(!m_player1->held);}if(m_player2){assert(!m_player2->held);}++attemptCounts;}
    void addChild(corum::ranked::ui::RankedEntryOverlay*,int){}
};
namespace corum::ranked::ui {${presentation}}
namespace geode {
template<class T>struct Ref {
    T* pointer=nullptr;Ref()=default;explicit Ref(T* p):pointer(p){if(pointer)pointer->retain();}
    Ref(Ref const& other):Ref(other.pointer){}Ref(Ref&& other):pointer(std::exchange(other.pointer,nullptr)){}
    ~Ref(){if(pointer)pointer->release();}Ref& operator=(std::nullptr_t){if(pointer)pointer->release();pointer=nullptr;return *this;}
    T* data()const{return pointer;}explicit operator bool()const{return pointer;}
};
template<class T>struct WeakRef {
    T* pointer=nullptr;std::weak_ptr<int> life;
    WeakRef()=default;WeakRef(WeakRef const&)=default;WeakRef&operator=(WeakRef const&)=default;WeakRef&operator=(WeakRef&&)=default;WeakRef(WeakRef&& other):pointer(std::exchange(other.pointer,nullptr)),life(std::move(other.life)){}
    explicit WeakRef(T* p):pointer(p),life(p->life){}
    struct Locked {T* pointer;explicit operator bool()const{return pointer!=nullptr;}T* data()const{return pointer;}T* operator->()const{return pointer;}};
    Locked lock()const{return {life.expired()?nullptr:pointer};}
};
}
std::vector<std::function<void()>> deferred;
void queueInMainThread(std::function<void()> callback){deferred.push_back(std::move(callback));}
void drain(){auto work=std::move(deferred);deferred.clear();for(auto& callback:work)callback();}
// Compile Corum's actual deferred return, then explicitly inject process pause
// changes at the native pop boundary. This models the observed state gap, not
// the unidentified GD 2.2081 iOS writer, its guard or disassembly.
namespace teardown_double {
using CCScene=SceneParent;
struct CCDirector {int pops=0;CCScene* current=nullptr;CCScene* next=nullptr;CCScene* getRunningScene(){return current;}CCScene* getNextScene(){return next;}std::function<void()> retire;
    static CCDirector* sharedDirector(){static CCDirector d;return &d;}
    void popScene(){++pops;if(retire)retire();}};
struct RankedRoundPreparation {
    geode::WeakRef<PlayLayer> m_play;geode::WeakRef<CCScene> m_origin,m_returnScene;std::optional<geode::WeakRef<CCScene>> m_nativePauseRestoreOrigin;
    geode::Ref<GJGameLevel> m_launchedLevel;
    bool m_quitting=true,m_returnQueued=false,m_returnPopped=false;std::uint64_t m_launchGeneration=1;
    RankedRoundPreparation(PlayLayer* p,CCScene* origin):m_play(p),m_origin(origin),m_launchedLevel(&p->level){CCDirector::sharedDirector()->current=&p->parent;}
    bool returnFromNativeQuit(GJGameLevel*);
};
${nativeReturn}
}
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
namespace corum::ranked {inline bool quitThroughSurvivalCompletionOverlay(PlayLayer*){return false;}}
struct CorumRankedPlayLayer:PlayLayer {
    struct Fields {
        bool survivalLevel=false,survivalNativeClearFinished=false;bool nativeEntryHeld=true,autoExitRequested=false,entryWasPaused=false,entryNativeCompletionInProgress=false;
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
    bool blockSurvivalEntry(){return false;}
    ${method(play, '    void releaseEntryControls()')}
    void initializeEntryInputHold(){${inputHold}}
    ${entryExit}
    ${countAttempt}
    ${callback}
};
int main(){
    using namespace corum::ranked;
    {
        CorumRankedPlayLayer clearing;clearing.fields.survivalLevel=true;clearing.m_hasCompletedLevel=true;
        clearing.queueHeldEntryExit();assert(!clearing.fields.autoExitRequested&&deferred.empty());
        clearing.fields.survivalNativeClearFinished=true;clearing.queueHeldEntryExit();drain();assert(clearing.quitCalls==1);
    }
    // A retained callback must never pop another scene or a transition that
    // took ownership while native Quit was unwinding.
    for(int changed=0;changed<4;++changed){
        CorumRankedPlayLayer play;SceneParent origin,other;
        teardown_double::RankedRoundPreparation handoff(&play,&origin);
        auto* d=teardown_double::CCDirector::sharedDirector();d->next=nullptr;d->retire={};auto before=d->pops;
        assert(handoff.returnFromNativeQuit(&play.level));
        if(changed==0)d->current=&other;
        if(changed==1)d->next=&other;
        if(changed==2)d->current=&origin;
        if(changed==3)play.parent.life.reset();
        drain();assert(d->pops==before&&handoff.m_returnQueued&&handoff.m_play.pointer);d->next=nullptr;
    }
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

    // A successful native concession Quit can return with both states false,
    // yet a later native boundary leaves the next entry's process state true.
    // Execute the actual Corum queued return, init hold and GO, including R2
    // and a new match R1. Native flag writes here are explicit boundary doubles.
    for(bool nextMatch:{false,true})for(bool changedDuringPop:{false,true}){
        runtime={};audio={};AppDelegate::get()->m_gamePaused=false;
        CorumRankedPlayLayer retired;retired.m_isPaused=false;SceneParent origin;
        teardown_double::RankedRoundPreparation handoff(&retired,&origin);
        auto* director=teardown_double::CCDirector::sharedDirector();
        director->retire=[&]{retired.running=false;retired.attached=false;
            if(changedDuringPop)AppDelegate::get()->m_gamePaused=true;};
        assert(handoff.returnFromNativeQuit(&retired.level));
        assert(handoff.returnFromNativeQuit(&retired.level)&&deferred.size()==1);
        assert(!AppDelegate::get()->m_gamePaused); // pre-pop Quit log is not the final boundary
        drain();assert(handoff.m_returnQueued&&handoff.m_play.pointer);
        director->retire={};
        CorumRankedPlayLayer next;UILayer nextUI;next.m_uiLayer=&nextUI;
        runtime.data.match.matchId=nextMatch?"next-match":"match";
        runtime.data.match.roundNumber=nextMatch?1:2;
        next.fields.sceneContext.matchId=runtime.data.match.matchId;
        next.fields.sceneContext.roundNumber=runtime.data.match.roundNumber;
        next.m_isPaused=false;next.touch=true;next.keyboard=true;
        next.updateAttempts();next.initializeEntryInputHold();
        if(!changedDuringPop)AppDelegate::get()->m_gamePaused=true;
        assert(next.blockEntryGameplayUpdate());
        assert(next.completions==0&&next.musicStarts==0&&next.attemptCounts==0);
        auto* uiHandler=CCDirector::sharedDirector()->dispatcher.findHandler(&nextUI);
        assert(uiHandler);
        runtime.data.match.entry={true,true};runtime.remaining=0;runtime.startGranted=true;
        assert(!next.blockEntryGameplayUpdate());
        assert(next.completions==1&&next.onEnters==0&&!next.m_isPaused&&!AppDelegate::get()->m_gamePaused);
        assert(next.refs==1&&next.attemptCounts==1&&next.musicStarts==1&&audio.resumes==1);
        assert(CCDirector::sharedDirector()->dispatcher.findHandler(&nextUI)==uiHandler&&nextUI.touchWrites==0);
        for(int i=0;i<20;++i)assert(!next.blockEntryGameplayUpdate());
        assert(next.completions==1&&next.musicStarts==1&&next.attemptCounts==1);
        next.pauseGame();assert(next.nativePauseOpens==1&&AppDelegate::get()->m_gamePaused);
    }
    // A clean initial R1 needs no extra completion. Recovery is conditional on
    // the observed state and never opens/resumes a synthetic Pause window.
    {runtime={};audio={};AppDelegate::get()->m_gamePaused=false;
     runtime.data.match.entry={true,true};runtime.remaining=0;runtime.startGranted=true;
     CorumRankedPlayLayer clean;clean.m_isPaused=false;clean.initializeEntryInputHold();
     assert(!clean.blockEntryGameplayUpdate()&&clean.completions==0&&clean.onEnters==0);}

    // Refusal must keep the entry held, schedule exactly one native Quit and
    // never publish the deferred attempt or resume gameplay audio/input.
    {runtime={};audio={};AppDelegate::get()->m_gamePaused=true;
     runtime.data.match.entry={true,true};runtime.remaining=0;runtime.startGranted=true;
     CorumRankedPlayLayer refused;UILayer refusedUI;refused.m_uiLayer=&refusedUI;
     refused.m_isPaused=false;refused.initializeEntryInputHold();refused.updateAttempts();refused.refuseCompletion=true;
     assert(refused.blockEntryGameplayUpdate());
     assert(refused.fields.nativeEntryHeld&&refused.fields.autoExitRequested&&refused.m_isPaused);
     assert(refused.completions==1&&refused.onEnters==0&&!refusedUI.pause.enabled&&!refused.keyboard);
     assert(refused.musicStarts==0&&audio.resumes==0&&refused.attemptCounts==0&&audio.paused&&deferred.size()==1);
     for(int i=0;i<20;++i)assert(refused.blockEntryGameplayUpdate());
     assert(refused.completions==1&&deferred.size()==1);drain();assert(refused.quitCalls==1);}

    // Other hooks may replace/depart the scene or revoke the exact context or
    // start certificate. Recheck after the native callback, not only before it.
    for(int dimension=0;dimension<12;++dimension){
        runtime={};audio={};AppDelegate::get()->m_gamePaused=true;
        runtime.data.match.entry={true,true};runtime.remaining=0;runtime.startGranted=true;
        CorumRankedPlayLayer other,changed;changed.m_isPaused=false;changed.initializeEntryInputHold();changed.updateAttempts();
        changed.completionHook=[&]{
            if(dimension==0)PlayLayer::current=&other;
            if(dimension==1)changed.attached=false;
            if(dimension==2)changed.running=false;
            if(dimension==3)runtime.data.match.matchId="other";
            if(dimension==4)++runtime.data.match.roundNumber;
            if(dimension==5)++runtime.data.match.deathmatchSequence;
            if(dimension==6)++runtime.level;
            if(dimension==7)runtime.data.match.state="CANCELLED";
            if(dimension==8)runtime.data.match.entry.enteredB=false;
            if(dimension==9)runtime.startGranted=false;
            if(dimension==10)runtime.remaining=500;
            if(dimension==11)CCDirector::sharedDirector()->runningScene=&other.parent;
        };
        assert(changed.blockEntryGameplayUpdate());
        assert(changed.fields.nativeEntryHeld&&changed.fields.autoExitRequested&&changed.completions==1);
        assert(changed.attemptCounts==0&&changed.musicStarts==0&&audio.resumes==0&&other.quitCalls==0);
        drain();assert(other.quitCalls==0);
        assert(changed.quitCalls==((dimension==0||dimension==1||dimension==11)?0:1));
    }
    // Native completion re-entry must not release twice. A strong Ref must
    // also protect all post-call field access if the callback drops scene ownership.
    {runtime={};audio={};AppDelegate::get()->m_gamePaused=true;
     runtime.data.match.entry={true,true};runtime.remaining=0;runtime.startGranted=true;
     CorumRankedPlayLayer reentrant;reentrant.m_isPaused=false;reentrant.initializeEntryInputHold();
     reentrant.completionHook=[&]{assert(reentrant.fields.nativeEntryHeld&&reentrant.fields.entryNativeCompletionInProgress);
         assert(reentrant.blockEntryGameplayUpdate());};
     assert(!reentrant.blockEntryGameplayUpdate());
     assert(reentrant.completions==1&&runtime.arms==1&&reentrant.musicStarts==1);}
    {runtime={};audio={};AppDelegate::get()->m_gamePaused=true;
     runtime.data.match.entry={true,true};runtime.remaining=0;runtime.startGranted=true;
     CorumRankedPlayLayer removed;removed.m_isPaused=false;removed.initializeEntryInputHold();
     removed.completionHook=[&]{removed.attached=false;PlayLayer::current=nullptr;removed.release();assert(removed.alive&&removed.refs==1);};
     assert(removed.blockEntryGameplayUpdate());assert(!removed.alive&&removed.refs==0);
     assert(removed.attemptCounts==0&&removed.musicStarts==0);drain();assert(removed.quitCalls==0);}
    // Preserve a genuinely paused pre-entry scene and all ordinary GD state.
    {runtime={};audio={};AppDelegate::get()->m_gamePaused=true;
     runtime.data.match.entry={true,true};runtime.remaining=0;runtime.startGranted=true;
     CorumRankedPlayLayer alreadyPaused;alreadyPaused.initializeEntryInputHold();
     assert(!alreadyPaused.blockEntryGameplayUpdate());
     assert(alreadyPaused.m_isPaused&&AppDelegate::get()->m_gamePaused&&alreadyPaused.completions==0&&audio.resumes==0);}
    AppDelegate::get()->m_gamePaused=false;

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
    AppDelegate::get()->m_gamePaused=true;
    assert(!ordinary.blockEntryGameplayUpdate());assert(AppDelegate::get()->m_gamePaused&&ordinary.completions==0);

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
        assert(animated.digit.writes==4&&animated.heading.writes==1);
        animated.present(true,2500);assert(animated.digit.text.empty()&&animated.digit.opacity==0);
        ui::RankedEntryOverlay nextRound;nextRound.present(true,2500);assert(nextRound.digit.text=="3");
        nextRound.present(true,1500);assert(nextRound.digit.text=="2");
        nextRound.present(true,500);assert(nextRound.digit.text=="1");
        nextRound.present(true,0);assert(nextRound.digit.text.empty());
    }
    // Reproduce the actual ServerClock correction around the 3 -> 2 boundary.
    ServerClock serverClock;ui::RankedEntryOverlay corrected;
    serverClock.observe("2026-10-08T00:00:00.900Z",1000);
    corrected.present(true,serverClock.remainingMillis("2026-10-08T00:00:03.000Z",1000));
    assert(corrected.digit.text=="3");
    serverClock.observe("2026-10-08T00:00:01.010Z",1020);
    corrected.present(true,serverClock.remainingMillis("2026-10-08T00:00:03.000Z",1020));
    assert(corrected.digit.text=="2");auto const writes=corrected.digit.writes;
    for(int i=0;i<100;++i){
        serverClock.observe("2026-10-08T00:00:00.980Z",1040+i);
        auto remaining=serverClock.remainingMillis("2026-10-08T00:00:03.000Z",1040+i);
        assert(remaining&&*remaining==2020);
        corrected.present(true,remaining);assert(corrected.digit.text=="2");
    }
    assert(corrected.digit.writes==writes);
    corrected.present(true,std::nullopt);assert(corrected.digit.text=="2");
    corrected.present(true,500);assert(corrected.digit.text=="1");
    corrected.present(true,0);corrected.present(true,500);assert(corrected.digit.text.empty());
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
                `-D${platform}=1`, '-I', dir, join(dir, 'entry.cpp'), join(root, 'src/domain/ServerClock.cpp'), '-o', binary], { stdio: 'pipe' });
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
    const go=method(play,'bool blockEntryGameplayUpdate()');
    assert.doesNotMatch(go,/m_gamePaused\s*=|->resume\(|PlayLayer::resume\(|onEnter\(\)/);
    assert.match(go,/static_cast<PlayLayer\*>\(this\)->onEnterTransitionDidFinish\(\)/);
    assert.match(method(play,'void queueHeldEntryExit()'),/layer->onQuit\(\)/);
});
