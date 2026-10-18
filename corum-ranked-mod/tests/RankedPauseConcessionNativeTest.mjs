import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';

const pause=readFileSync(new URL('../src/RankedPauseLayer.cpp',import.meta.url),'utf8');
const play=readFileSync(new URL('../src/PlayLayer.cpp',import.meta.url),'utf8');
function body(source,name){const at=source.indexOf(name);assert(at>=0,name);let end=source.indexOf('{',at)+1,depth=1;
    while(depth){depth+=(source[end]==='{')-(source[end]==='}');++end;}return source.slice(at,end);}

test('authenticated concession closes native Pause and permits its Quit with intermediate paused flags; refusal remains available to fallback',()=>{
    // The Corum callbacks below are extracted verbatim. Native Quit/Resume and
    // Cocos retain/release are boundary doubles, not an iOS implementation or
    // disassembly of GD iOS. The latest log proves entry into the Play quit
    // boundary while paused, so native Quit is NOT modeled as Resume first.
    const cpp=`
#include <cassert>
#include <optional>
#include <string>
#include <vector>
#include <utility>
struct CCObject {int refs=1;bool alive=true;void retain(){assert(alive);++refs;}void release(){assert(alive&&refs>0);if(--refs==0)alive=false;}};
struct Level {int m_levelID=123;};
struct PlayLayer:CCObject {inline static PlayLayer* current=nullptr;Level level;Level* m_level=&level;bool m_isPracticeMode=false,m_isTestMode=false;bool m_isPaused=true,refuseResume=false;PlayLayer* replacement=nullptr;int quits=0,resumes=0;static PlayLayer* get(){return current;}void onQuit();void resume();};
struct SurvivalRuntime {struct View{std::string id="survival";}v;bool pending=false;static SurvivalRuntime& get(){static SurvivalRuntime r;return r;}bool surrenderPending()const{return pending;}auto const&view()const{return v;}};
struct AppDelegate {bool m_gamePaused=true;static AppDelegate* get(){static AppDelegate a;return &a;}};
template<class T>struct Ref {T* p=nullptr;Ref()=default;explicit Ref(T* v):p(v){if(p)p->retain();}Ref(Ref const& v):Ref(v.p){}Ref(Ref&& v):p(std::exchange(v.p,nullptr)){}~Ref(){if(p)p->release();}T* data()const{return p;}T* operator->()const{assert(p&&p->alive);return p;}explicit operator bool()const{return p;}};
template<class T>struct WeakRef {T* p;explicit WeakRef(T* v):p(v){}Ref<T> lock()const{return Ref<T>(p&&p->alive?p:nullptr);}};
namespace geode::log {template<class...T>void info(T...) {}}
std::vector<std::string> events;
struct PauseLayer:CCObject {bool parent=true,dispatchingQuit=false,dispatchingResume=false,refuseResume=false,refuseQuit=false,closeViaResume=false;int nativeQuits=0,resumes=0;void* getParent(){return parent?this:nullptr;}void onQuit(CCObject*);void onResume(CCObject*);void nativeQuit();void nativeResume();};
struct Popup:CCObject {int closes=0;void cancel(){dismiss();}void dismiss(){++closes;events.push_back("dialog-closed");}};
namespace corum::ranked {
enum class RuntimeStage{Idle,Matched};
struct RankedRuntime {
    struct Match{std::string matchId="m";int roundNumber=1,deathmatchSequence=0;};
    struct Placement{std::string runId="m",state="MAP_PREPARE";int currentStageIndex=1,currentMapIndex=1;};struct View{Placement placement;RuntimeStage stage=RuntimeStage::Matched;Match match;}v;
    bool conceded=false,placement=false,spectator=false;int ends=0;
    static RankedRuntime& get(){static RankedRuntime r;return r;}
    auto const& view()const{return v;}
    bool isPlacement()const{return placement;}bool placementMapSkipped(std::string const& id,int stage,int map)const{return skipped&&id==skippedRunId&&stage==skippedStage&&map==skippedMap;}bool skipped=false;std::string skippedRunId="m";int skippedStage=1,skippedMap=1;bool isSpectating()const{return spectator;}bool isGameplayLevel(int id)const{return id==123;}
    bool gameplayConceded(int round,int death)const{return conceded&&round==v.match.roundNumber&&death==v.match.deathmatchSequence;}
    void endMatchGameplayPause(std::string const& lease){if(!lease.empty()){++ends;events.push_back("lease-ended");}}
};
}
using corum::ranked::RankedRuntime;
std::optional<WeakRef<PauseLayer>> activeRankedPause;
#define schedule_selector(x) 0
${body(pause,'bool ownsRankedPlayerPause()')}
${body(pause,'bool ownsPlacementPlayerPause()')}
bool ownsSurvivalPlayerPause(){return false;}
${body(pause,'bool ownsManagedRankedPause()')}
void dismissNativeRankedExitConfirmation(PauseLayer*){};
struct CorumRankedPauseLayer:PauseLayer {
    struct Fields{struct ActionInput{void suspend(){}};ActionInput* survivalInput=nullptr;std::optional<WeakRef<Popup>> popup,exitConfirmation,survivalConfirmation;std::optional<WeakRef<PlayLayer>> owner;std::string leaseId="lease",matchId="m";int round=1,deathmatch=0;int placementMapIndex=1;bool placement=false,survival=false;bool concessionQuit=false,nativeQuitInProgress=false,nativeQuitReachedPlay=false;}f;
    struct CheckedFields{Fields* p;CCObject* owner;Fields* operator->(){assert(owner->alive);return p;}}m_fields{&f,this};
    int exitWarnings=0;void confirmRankedExit(){++exitWarnings;}
    void unschedule(int){}
    ${body(pause,'void retireRankedPause()')}
    ${body(pause,'void releaseRankedPauseOwnership()')}
    ${body(pause,'void onResume(CCObject* sender)')}
    ${body(pause,'bool hasAcknowledgedGameplayExit()')}
${body(pause,'void onQuit(CCObject* sender)')}
};
namespace corum::ranked {
${body(pause,'bool resumeRankedPauseBeforeQuit(PlayLayer* layer)')}
${body(pause,'bool quitRankedPauseForConcession(PlayLayer* layer)')}
${body(pause,'bool quitSurvivalPauseForReturn(PlayLayer* layer)')}
}
void PlayLayer::resume(){++resumes;events.push_back("native-resume");if(refuseResume)return;m_isPaused=false;AppDelegate::get()->m_gamePaused=false;if(replacement)current=replacement;}
void PlayLayer::onQuit(){if(!corum::ranked::resumeRankedPauseBeforeQuit(this))return;
    events.push_back(m_isPaused?"play-quit-paused":"play-quit");++quits;
    // Simulate native teardown releasing its states. The Corum permission
    // boundary must let it run; Corum never writes either flag itself.
    m_isPaused=false;AppDelegate::get()->m_gamePaused=false;}
void PauseLayer::nativeQuit(){++nativeQuits;events.push_back("native-pause-quit");
    assert(parent); // Native Pause Quit is always entered with its window attached.
    if(refuseQuit)return;
    if(closeViaResume)onResume(nullptr);
    PlayLayer::get()->onQuit();
    if(!PlayLayer::get()->m_isPaused&&parent){parent=false;release();}
}
void PauseLayer::nativeResume(){++resumes;events.push_back("native-resume");if(refuseResume)return;
    PlayLayer::get()->m_isPaused=false;AppDelegate::get()->m_gamePaused=false;if(parent){parent=false;release();}}
void PauseLayer::onResume(CCObject* sender){if(dispatchingResume){nativeResume();return;}dispatchingResume=true;static_cast<CorumRankedPauseLayer*>(this)->onResume(sender);dispatchingResume=false;}
void PauseLayer::onQuit(CCObject* sender){if(dispatchingQuit){nativeQuit();return;}dispatchingQuit=true;static_cast<CorumRankedPauseLayer*>(this)->onQuit(sender);dispatchingQuit=false;}
int main(){using namespace corum::ranked;auto& r=RankedRuntime::get();PlayLayer a,b;PlayLayer::current=&a;
    {CorumRankedPauseLayer p;Popup dialog;p.f.owner.emplace(&a);p.f.popup.emplace(&dialog);activeRankedPause.emplace(&p);
     static_cast<PauseLayer*>(&p)->onQuit(nullptr);assert(p.nativeQuits==0&&a.quits==0); // manual exit remains blocked
     assert(!quitRankedPauseForConcession(&a));r.conceded=true;
     PlayLayer::current=&b;assert(!quitRankedPauseForConcession(&a)&&!quitRankedPauseForConcession(&b));
     PlayLayer::current=&a;r.v.match.roundNumber=2;assert(!quitRankedPauseForConcession(&a));r.v.match.roundNumber=1;
     assert(quitRankedPauseForConcession(&a));assert(a.quits==1); // previous source blocks the actual Play quit here
     assert(p.nativeQuits==1&&p.resumes==0&&a.resumes==1&&dialog.closes==1&&r.ends==1);
     assert((events==std::vector<std::string>{"dialog-closed","lease-ended","native-resume","native-pause-quit","play-quit"}));
     assert(!activeRankedPause&&!p.f.owner&&!p.f.popup&&!p.alive&&p.refs==0); // post-native fields access remained alive
     assert(!quitRankedPauseForConcession(&a)&&a.quits==1);
    }
    // Native Quit must be allowed even if native Resume left flags paused.
    // This was the exact blocked boundary in the device log.
    {CorumRankedPauseLayer p;a.m_isPaused=true;AppDelegate::get()->m_gamePaused=true;a.refuseResume=true;p.f.owner.emplace(&a);activeRankedPause.emplace(&p);
     assert(quitRankedPauseForConcession(&a)&&a.quits==2&&p.nativeQuits==1&&a.resumes==2);
     assert(events.back()=="play-quit-paused"&&!a.m_isPaused&&!AppDelegate::get()->m_gamePaused&&!activeRankedPause&&!p.alive);
     a.refuseResume=false;
    }
    // Both native operations refused: do not claim invocation as success.
    // An independent quit still cannot bypass failed native Resume.
    {CorumRankedPauseLayer p;a.m_isPaused=true;AppDelegate::get()->m_gamePaused=true;a.refuseResume=true;p.refuseResume=p.refuseQuit=true;p.f.owner.emplace(&a);activeRankedPause.emplace(&p);
     assert(!quitRankedPauseForConcession(&a)&&a.quits==2&&p.nativeQuits==1&&activeRankedPause&&p.f.owner&&p.alive);
     assert(!p.f.nativeQuitReachedPlay&&!p.f.nativeQuitInProgress&&!p.f.concessionQuit);
     assert(!resumeRankedPauseBeforeQuit(&a));assert(a.quits==2&&activeRankedPause);
     activeRankedPause.reset();p.f.owner.reset();
     a.refuseResume=false;
    }
    // Pause Quit refused after a successful Resume: its attached window must
    // not swallow the caller's fallback. The fallback closes it via onResume.
    {CorumRankedPauseLayer p;a.m_isPaused=true;AppDelegate::get()->m_gamePaused=true;p.refuseQuit=true;p.f.owner.emplace(&a);activeRankedPause.emplace(&p);
     assert(!quitRankedPauseForConcession(&a)&&!a.m_isPaused&&!AppDelegate::get()->m_gamePaused);
     a.onQuit();assert(a.quits==3);activeRankedPause.reset();
    }
    // Another valid native order closes its Pause inside Quit. The exact owner
    // must survive that Resume until its caller reaches the Play quit boundary.
    {CorumRankedPauseLayer p;a.m_isPaused=true;AppDelegate::get()->m_gamePaused=true;p.closeViaResume=true;p.f.owner.emplace(&a);activeRankedPause.emplace(&p);
     assert(quitRankedPauseForConcession(&a)&&a.quits==4&&p.resumes==1&&!activeRankedPause&&!p.alive);
    }
    // Native Resume can change the current scene; never quit its replacement.
    {CorumRankedPauseLayer p;a.m_isPaused=true;AppDelegate::get()->m_gamePaused=true;a.replacement=&b;p.f.owner.emplace(&a);activeRankedPause.emplace(&p);
     assert(!quitRankedPauseForConcession(&a)&&p.nativeQuits==0&&b.quits==0);
     activeRankedPause.reset();p.f.owner.reset();a.replacement=nullptr;PlayLayer::current=&a;
    }
    // Both a same-match R2 and a completely new match R1 can own a fresh Pause
    // after simulated native teardown released the shared AppDelegate state.
    for(int round:{2,1}){
     assert(!AppDelegate::get()->m_gamePaused);b.m_isPaused=true;AppDelegate::get()->m_gamePaused=true;
     CorumRankedPauseLayer next;PlayLayer::current=&b;r.v.match.roundNumber=round;next.f.round=round;
     if(round==1){r.v.match.matchId="next";}next.f.matchId=r.v.match.matchId;next.f.owner.emplace(&b);activeRankedPause.emplace(&next);
     assert(quitRankedPauseForConcession(&b)&&next.nativeQuits==1&&!activeRankedPause&&!next.parent&&!AppDelegate::get()->m_gamePaused);
    }
    // Survival ACK uses the attached native Pause transaction as Solo does.
    for(bool refusedResume:{false,true}){CorumRankedPauseLayer p;p.f.survival=true;p.f.matchId="survival";p.f.leaseId.clear();p.f.owner.emplace(&b);activeRankedPause.emplace(&p);
     PlayLayer::current=&b;b.m_isPaused=true;AppDelegate::get()->m_gamePaused=true;b.refuseResume=refusedResume;auto before=b.quits;
     SurvivalRuntime::get().pending=true;assert(!quitSurvivalPauseForReturn(&b)&&b.quits==before);
     SurvivalRuntime::get().pending=false;SurvivalRuntime::get().v.id="different";assert(!quitSurvivalPauseForReturn(&b));SurvivalRuntime::get().v.id="survival";
     assert(quitSurvivalPauseForReturn(&b)&&b.quits==before+1&&p.nativeQuits==1&&!activeRankedPause&&!AppDelegate::get()->m_gamePaused);
     assert(!quitSurvivalPauseForReturn(&b)&&b.quits==before+1);b.refuseResume=false;
    }
    // Placement uses the same attached native Quit transaction, without a
    // PvP lease. Its acknowledgement belongs to one map, not the whole stage.
    r.placement=true;r.v.match.matchId="placement";r.v.match.roundNumber=1;
    r.v.placement={"placement","MAP_PLAYING",1,1};r.skippedRunId="placement";
    {CorumRankedPauseLayer p;p.f.placement=true;p.f.placementMapIndex=1;p.f.matchId="placement";p.f.round=1;p.f.leaseId.clear();
     p.f.owner.emplace(&b);activeRankedPause.emplace(&p);b.m_isPaused=true;AppDelegate::get()->m_gamePaused=true;
     auto before=b.quits;assert(!quitRankedPauseForConcession(&b));r.skipped=true;
     assert(quitRankedPauseForConcession(&b)&&b.quits==before+1&&!activeRankedPause&&!AppDelegate::get()->m_gamePaused);
    }
    {CorumRankedPauseLayer p;r.v.placement.currentMapIndex=2;p.f.placement=true;p.f.placementMapIndex=2;p.f.matchId="placement";p.f.round=1;p.f.leaseId.clear();
     p.f.owner.emplace(&b);activeRankedPause.emplace(&p);b.m_isPaused=true;AppDelegate::get()->m_gamePaused=true;
     auto before=b.quits;assert(!quitRankedPauseForConcession(&b)&&b.quits==before); // old map ACK cannot exit map 2
     static_cast<PauseLayer*>(&p)->onQuit(nullptr); // ordinary Placement exit still uses vanilla Pause Quit
     assert(b.quits==before+1&&!activeRankedPause&&!AppDelegate::get()->m_gamePaused);
    }
}
`;
    const dir=mkdtempSync(join(tmpdir(),'corum-native-concession-'));
    try{writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}
    finally{rmSync(dir,{recursive:true,force:true});}
});

test('both concession observers queue one attached-scene handoff and leave native Pause retirement to native Quit',()=>{
    const watch=body(pause,'void watchBudget(float)');
    const concession=body(watch,'if (ranked.gameplayConceded');
    assert.match(concession,/requestConcededGameplayReturn\(owner.data\(\)\)/);
    assert.doesNotMatch(concession,/resumeRankedPause/);
    const handoff=body(play,'bool finishConcededGameplayHandoff()');
    assert.match(handoff,/m_fields->autoExitRequested\) return true/);
    assert.match(handoff,/getParent\(\) == CCDirector::sharedDirector\(\)->getRunningScene\(\)/);
    assert(handoff.indexOf('quitRankedPauseForConcession')<handoff.indexOf('layer->onQuit()'));
    const quit=body(pause,'void onQuit(CCObject* sender)');
    assert(quit.indexOf('owner->resume()')<quit.lastIndexOf('PauseLayer::onQuit(sender)'));
    assert.match(body(pause,'bool quitRankedPauseForConcession(PlayLayer* layer)'),/return rankedPause->m_fields->nativeQuitReachedPlay;/);
    assert.doesNotMatch(pause,/m_gamePaused\s*=|m_isPaused\s*=/);
});
