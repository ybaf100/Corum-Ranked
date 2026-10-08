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

test('authenticated concession enters the owned native Pause Quit once; native refusal keeps ownership and blocks gameplay quit',()=>{
    // The Corum callbacks below are extracted verbatim. Native Quit/Resume and
    // Cocos retain/release are boundary doubles, not an iOS implementation or
    // evidence that AppDelegate::m_gamePaused caused the user's failure.
    const cpp=`
#include <cassert>
#include <optional>
#include <string>
#include <vector>
#include <utility>
struct CCObject {int refs=1;bool alive=true;void retain(){assert(alive);++refs;}void release(){assert(alive&&refs>0);if(--refs==0)alive=false;}};
struct Level {int m_levelID=123;};
struct PlayLayer:CCObject {inline static PlayLayer* current=nullptr;Level level;Level* m_level=&level;bool m_isPaused=true;int quits=0;static PlayLayer* get(){return current;}void onQuit();};
struct AppDelegate {bool m_gamePaused=true;static AppDelegate* get(){static AppDelegate a;return &a;}};
template<class T>struct Ref {T* p=nullptr;Ref()=default;explicit Ref(T* v):p(v){if(p)p->retain();}Ref(Ref const& v):Ref(v.p){}Ref(Ref&& v):p(std::exchange(v.p,nullptr)){}~Ref(){if(p)p->release();}T* data()const{return p;}T* operator->()const{assert(p&&p->alive);return p;}explicit operator bool()const{return p;}};
template<class T>struct WeakRef {T* p;explicit WeakRef(T* v):p(v){}Ref<T> lock()const{return Ref<T>(p&&p->alive?p:nullptr);}};
namespace geode::log {template<class...T>void info(T...) {}}
std::vector<std::string> events;
struct PauseLayer:CCObject {bool parent=true,dispatching=false,refuseResume=false;int nativeQuits=0,resumes=0;void* getParent(){return parent?this:nullptr;}void onQuit(CCObject*);void onResume(CCObject*){++resumes;if(!refuseResume){PlayLayer::get()->m_isPaused=false;parent=false;}};void nativeQuit();};
struct Popup:CCObject {int closes=0;void dismiss(){++closes;events.push_back("dialog-closed");}};
namespace corum::ranked {
enum class RuntimeStage{Idle,Matched};
struct RankedRuntime {
    struct Match{std::string matchId="m";int roundNumber=1,deathmatchSequence=0;};
    struct View{RuntimeStage stage=RuntimeStage::Matched;Match match;}v;
    bool conceded=false,placement=false,spectator=false;int ends=0;
    static RankedRuntime& get(){static RankedRuntime r;return r;}
    auto const& view()const{return v;}
    bool isPlacement()const{return placement;}bool isSpectating()const{return spectator;}bool isGameplayLevel(int id)const{return id==123;}
    bool gameplayConceded(int round,int death)const{return conceded&&round==v.match.roundNumber&&death==v.match.deathmatchSequence;}
    void endMatchGameplayPause(std::string const& lease){if(!lease.empty()){++ends;events.push_back("lease-ended");}}
};
}
using corum::ranked::RankedRuntime;
std::optional<WeakRef<PauseLayer>> activeRankedPause;
#define schedule_selector(x) 0
${body(pause,'bool ownsRankedPlayerPause()')}
struct CorumRankedPauseLayer:PauseLayer {
    struct Fields{std::optional<WeakRef<Popup>> popup;std::optional<WeakRef<PlayLayer>> owner;std::string leaseId="lease",matchId="m";int round=1,deathmatch=0;bool concessionQuit=false,nativeQuitInProgress=false,nativeQuitReachedPlay=false;}f;
    struct CheckedFields{Fields* p;CCObject* owner;Fields* operator->(){assert(owner->alive);return p;}}m_fields{&f,this};
    void unschedule(int){}
    ${body(pause,'void retireRankedPause()')}
    ${body(pause,'void releaseRankedPauseOwnership()')}
    ${body(pause,'void onQuit(CCObject* sender)')}
};
namespace corum::ranked {
${body(pause,'bool resumeRankedPauseBeforeQuit(PlayLayer* layer)')}
${body(pause,'bool quitRankedPauseForConcession(PlayLayer* layer)')}
}
void PlayLayer::onQuit(){if(!corum::ranked::resumeRankedPauseBeforeQuit(this))return;assert(!m_isPaused);events.push_back("play-quit");++quits;}
void PauseLayer::nativeQuit(){++nativeQuits;events.push_back("native-pause-quit");
    if(!refuseResume){PlayLayer::get()->m_isPaused=false;AppDelegate::get()->m_gamePaused=false;events.push_back("native-resume");}
    PlayLayer::get()->onQuit();
    if(!PlayLayer::get()->m_isPaused){parent=false;release();}
}
void PauseLayer::onQuit(CCObject* sender){if(dispatching){nativeQuit();return;}dispatching=true;static_cast<CorumRankedPauseLayer*>(this)->onQuit(sender);dispatching=false;}
int main(){using namespace corum::ranked;auto& r=RankedRuntime::get();PlayLayer a,b;PlayLayer::current=&a;
    {CorumRankedPauseLayer p;Popup dialog;p.f.owner.emplace(&a);p.f.popup.emplace(&dialog);activeRankedPause.emplace(&p);
     static_cast<PauseLayer*>(&p)->onQuit(nullptr);assert(p.nativeQuits==0&&a.quits==0); // manual exit remains blocked
     assert(!quitRankedPauseForConcession(&a));r.conceded=true;
     PlayLayer::current=&b;assert(!quitRankedPauseForConcession(&a)&&!quitRankedPauseForConcession(&b));
     PlayLayer::current=&a;r.v.match.roundNumber=2;assert(!quitRankedPauseForConcession(&a));r.v.match.roundNumber=1;
     assert(quitRankedPauseForConcession(&a));assert(a.quits==1&&p.nativeQuits==1&&p.resumes==0&&dialog.closes==1&&r.ends==1);
     assert((events==std::vector<std::string>{"dialog-closed","lease-ended","native-pause-quit","native-resume","play-quit"}));
     assert(!activeRankedPause&&!p.f.owner&&!p.f.popup&&!p.alive&&p.refs==0); // post-native fields access remained alive
     assert(!quitRankedPauseForConcession(&a)&&a.quits==1);
    }
    {CorumRankedPauseLayer p;a.m_isPaused=true;p.refuseResume=true;p.f.owner.emplace(&a);activeRankedPause.emplace(&p);
     assert(quitRankedPauseForConcession(&a)&&a.quits==1&&p.nativeQuits==1&&activeRankedPause&&p.f.owner&&p.alive);
     assert(!p.f.nativeQuitReachedPlay&&!p.f.nativeQuitInProgress&&!p.f.concessionQuit);
     assert(!resumeRankedPauseBeforeQuit(&a));assert(a.quits==1&&activeRankedPause); // refusal cannot be bypassed by a second route
     activeRankedPause.reset();p.f.owner.reset();
    }
    {CorumRankedPauseLayer next;PlayLayer::current=&b;r.v.match.matchId="next";next.f.matchId="next";next.f.owner.emplace(&b);activeRankedPause.emplace(&next);
     assert(quitRankedPauseForConcession(&b)&&b.quits==1&&next.nativeQuits==1&&!activeRankedPause);
    }
}
`;
    const dir=mkdtempSync(join(tmpdir(),'corum-native-concession-'));
    try{writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}
    finally{rmSync(dir,{recursive:true,force:true});}
});

test('both concession observers queue one attached-scene handoff and leave native Pause retirement to native Quit',()=>{
    const watch=body(pause,'void watchBudget(float)');
    const concession=watch.slice(watch.indexOf('if (ranked.gameplayConceded'),watch.indexOf('if ((!m_fields->leaseId'));
    assert.match(concession,/requestConcededGameplayReturn\(owner.data\(\)\)/);
    assert.doesNotMatch(concession,/resumeRankedPause/);
    const handoff=body(play,'bool finishConcededGameplayHandoff()');
    assert.match(handoff,/m_fields->autoExitRequested\) return true/);
    assert.match(handoff,/getParent\(\) == CCDirector::sharedDirector\(\)->getRunningScene\(\)/);
    assert(handoff.indexOf('quitRankedPauseForConcession')<handoff.indexOf('layer->onQuit()'));
    assert.doesNotMatch(pause,/m_gamePaused\s*=|m_isPaused\s*=/);
});
