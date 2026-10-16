import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const read = path => readFileSync(new URL(path, import.meta.url), 'utf8');
const pause = read('../src/RankedPauseLayer.cpp');
function body(source, signature) {
  const start=source.indexOf(signature); assert(start>=0,signature);
  let end=source.indexOf('{',start)+1,depth=1;
  while(depth){depth+=(source[end]==='{')-(source[end]==='}');++end;}
  return source.slice(start,end);
}
function run(cpp) {
  const dir=mkdtempSync(join(tmpdir(),'corum-ranked-exit-'));
  try { writeFileSync(join(dir,'test.cpp'),cpp);
    execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-O2',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
    execFileSync(join(dir,'test'),[],{stdio:'pipe'});
  } finally {rmSync(dir,{recursive:true,force:true});}
}

test('native confirmation boundary obtains Fields only from the tracked Pause, independent of its incoming receiver',()=>{
  const quit=body(pause,'void onQuit(CCObject* sender)');
  assert.doesNotMatch(quit.replace(/\/\/[^\n]*/g,''), /\bthis\b/); // no incoming receiver dereference or guard
  assert.match(quit,/auto& fields = rankedPause->m_fields/);
  // A free ABI adapter models a null native callback receiver without invoking
  // a C++ member on null (which itself is undefined behaviour). Native GD,
  // Fields allocation and the hook chain remain explicit boundary doubles.
  const adapter=quit.replace('void onQuit(CCObject* sender)',
    'void rankedQuitBoundary([[maybe_unused]] void* incoming, CCObject* sender)')
    .replaceAll('PauseLayer::onQuit(sender)','nativeOriginal(sender)');
  run(`
#include <cassert>
#include <optional>
#include <string>
struct CCObject{};
template<class T>struct Ref{T* p=nullptr;Ref()=default;explicit Ref(T* v):p(v){}T* data()const{return p;}T* operator->()const{return p;}explicit operator bool()const{return p;}};
template<class T>struct WeakRef{T* p;explicit WeakRef(T* v):p(v){}Ref<T> lock()const{return Ref<T>(p);}};
struct SurvivalRuntime {static SurvivalRuntime& get(){static SurvivalRuntime r;return r;}bool surrenderPending()const{return false;}};
struct AppDelegate{bool m_gamePaused=true;static AppDelegate* get(){static AppDelegate a;return &a;}};
struct PlayLayer{inline static PlayLayer* current=nullptr;bool m_isPaused=true;static PlayLayer* get(){return current;}void resume(){m_isPaused=false;AppDelegate::get()->m_gamePaused=false;}};
struct PauseLayer{bool parent=true;void* getParent(){return parent?this:nullptr;}};
namespace geode::log{template<class...T>void info(T...) {}}
namespace corum::ranked{struct RankedRuntime{struct Match{std::string matchId="m";}match;struct View{Match match;}v;bool conceded=false;static RankedRuntime& get(){static RankedRuntime r;return r;}auto const& view()const{return v;}bool gameplayConceded(int,int)const{return conceded;}bool placementMapSkipped(std::string const&,int,int)const{return false;}};}
using corum::ranked::RankedRuntime;
bool owned=true;bool ownsRankedPlayerPause(){return owned;}bool ownsManagedRankedPause(){return ownsRankedPlayerPause();}
std::optional<WeakRef<PauseLayer>> activeRankedPause;
struct CorumRankedPauseLayer:PauseLayer{struct Fields{std::optional<WeakRef<PlayLayer>> owner;int placementMapIndex=1;bool placement=false,survival=false;bool concessionQuit=false,nativeQuitInProgress=false,nativeQuitReachedPlay=false;std::string matchId="m";int round=1,deathmatch=0;}f;Fields* m_fields=&f;${body(pause,'bool hasAcknowledgedGameplayExit()')} int warnings=0;void confirmRankedExit(){++warnings;}void retireRankedPause(){}void releaseRankedPauseOwnership(){activeRankedPause.reset();}};
int originals=0;void nativeOriginal(CCObject*){++originals;}
${adapter}
int main(){PlayLayer play;PlayLayer::current=&play;CorumRankedPauseLayer p;p.f.owner.emplace(&play);activeRankedPause.emplace(&p);
 rankedQuitBoundary(nullptr,nullptr);assert(p.warnings==1&&originals==0);
 rankedQuitBoundary(reinterpret_cast<void*>(0x68),nullptr);assert(p.warnings==2&&originals==0);
 p.parent=false;rankedQuitBoundary(nullptr,nullptr);assert(p.warnings==2&&originals==0);p.parent=true;
 activeRankedPause.reset();rankedQuitBoundary(nullptr,nullptr);assert(originals==0);
 owned=false;rankedQuitBoundary(nullptr,nullptr);assert(originals==1);
 activeRankedPause.emplace(&p);rankedQuitBoundary(nullptr,nullptr);assert(originals==2);
 owned=true;PlayLayer other;PlayLayer::current=&other;rankedQuitBoundary(nullptr,nullptr);assert(originals==2&&p.warnings==2);
}
`);
});

test('native English surrender dialog submits MATCH only for its exact live context and closes its own alert',()=>{
  const source=read('../src/ui/RankedExitConfirmation.hpp').replace(/^#pragma once\s*|^#include .*$/gm,'');
  assert.match(source,/FLAlertLayer::create\(this, "Surrender Match\?"/);
  assert.doesNotMatch(source,/i18n::|m_gamePaused\s*=|m_isPaused\s*=/);
  run(`
#include <cassert>
#include <optional>
#include <string>
struct CCObject{virtual ~CCObject()=default;};
struct CCNode:CCObject{bool parent=true;virtual bool init(){return true;}virtual void onExit(){}void autorelease(){}void* getParent(){return parent?this:nullptr;}void removeFromParentAndCleanup(bool){parent=false;}void schedule(int,float){}void unschedule(int){}};
template<class T>struct Ref{T* p=nullptr;Ref()=default;explicit Ref(T* v):p(v){}T* data()const{return p;}T* operator->()const{return p;}explicit operator bool()const{return p;}};
template<class T>struct WeakRef{T* p;explicit WeakRef(T* v):p(v){}Ref<T> lock()const{return Ref<T>(p);}};
struct PauseLayer:CCNode{};struct PlayLayer:CCNode{inline static PlayLayer* current=nullptr;static PlayLayer* get(){return current;}};
struct FLAlertLayer;struct FLAlertLayerProtocol{virtual void FLAlert_Clicked(FLAlertLayer*,bool){}};
struct CCTouchDispatcher{int releases=0;static CCTouchDispatcher* get(){static CCTouchDispatcher d;return &d;}void unregisterForcePrio(FLAlertLayer*){++releases;}};
struct FLAlertLayer:CCNode{inline static FLAlertLayer* last=nullptr;FLAlertLayerProtocol* m_alertProtocol=nullptr;bool m_forcePrioRegistered=true;int closes=0;std::string title,description,first,second;
 static FLAlertLayer* create(FLAlertLayerProtocol* d,char const* t,char const* v,char const* a,char const* b,float){auto* r=new FLAlertLayer;r->m_alertProtocol=d;r->title=t;r->description=v;r->first=a;r->second=b;last=r;return r;}
 void setID(std::string const&){}void show(){}void onBtn1(CCObject*){++closes;if(m_alertProtocol)m_alertProtocol->FLAlert_Clicked(this,false);parent=false;}};
std::string operator""_spr(char const* s,std::size_t n){return std::string(s,n);}
#define schedule_selector(x) 0
namespace geode::prelude{}
namespace corum::ranked{enum class RuntimeStage{Idle,Matched};struct RankedRuntime{struct Match{std::string matchId="m";int roundNumber=1,deathmatchSequence=0;};struct View{RuntimeStage stage=RuntimeStage::Matched;Match match;}v;
 bool conceded=false,spectator=false,whole=false;double remaining=30000;int submissions=0;static RankedRuntime& get(){static RankedRuntime r;return r;}auto const& view()const{return v;}bool isSpectating()const{return spectator;}double matchPauseRemainingMs()const{return remaining;}bool gameplayConceded(int,int)const{return conceded;}bool placementMapSkipped(std::string const&,int,int)const{return false;}void surrenderCurrentMatch(bool v){whole=v;++submissions;}};}
${source}
int main(){using namespace corum::ranked;using ui::RankedExitConfirmation;auto& runtime=RankedRuntime::get();PlayLayer play;PlayLayer::current=&play;PauseLayer pause;
 {auto* c=RankedExitConfirmation::create(&pause,&play);c->show();auto* a=FLAlertLayer::last;assert(a->first=="Stay"&&a->second=="Surrender");c->FLAlert_Clicked(a,false);assert(runtime.submissions==0&&!c->getParent()&&!a->m_alertProtocol);delete a;delete c;}
 {auto* c=RankedExitConfirmation::create(&pause,&play);c->show();auto* a=FLAlertLayer::last;c->FLAlert_Clicked(a,true);c->FLAlert_Clicked(a,true);assert(runtime.submissions==1&&runtime.whole);delete a;delete c;}
 {auto* c=RankedExitConfirmation::create(&pause,&play);c->show();auto* a=FLAlertLayer::last;runtime.v.match.roundNumber=2;c->FLAlert_Clicked(a,true);assert(runtime.submissions==1);delete a;delete c;}
 {auto* c=RankedExitConfirmation::create(&pause,&play);c->show();auto* a=FLAlertLayer::last;auto before=CCTouchDispatcher::get()->releases;c->dismiss();assert(!a->m_alertProtocol&&!a->m_forcePrioRegistered&&a->closes==1);c->dismiss();assert(a->closes==1&&CCTouchDispatcher::get()->releases==before+1);delete a;delete c;}
 {auto* c=RankedExitConfirmation::create(&pause,&play);c->show();auto* a=FLAlertLayer::last;c->onExit();assert(!a->m_alertProtocol&&a->closes==1);delete a;delete c;}
 {auto* c=RankedExitConfirmation::create(&pause,&play);c->show();auto* a=FLAlertLayer::last;runtime.remaining=0;c->FLAlert_Clicked(a,true);assert(runtime.submissions==1);delete a;delete c;}
}
`);
});

test('budget retirement cancels only native alerts delegated to the exact live Pause',()=>{
  const close=body(pause,'void dismissNativeRankedExitConfirmation(PauseLayer* pause)');
  run(`
#include <cassert>
#include <vector>
#include <utility>
struct CCNode{virtual ~CCNode()=default;CCNode* parent=nullptr;std::vector<CCNode*> children;auto const& getChildrenExt()const{return children;}void* getParent(){return parent;}void addChild(CCNode* n){children.push_back(n);n->parent=this;}};
struct FLAlertLayerProtocol{};struct PauseLayer:CCNode,FLAlertLayerProtocol{};
struct FLAlertLayer:CCNode{FLAlertLayerProtocol* m_alertProtocol=nullptr;bool m_forcePrioRegistered=true;int cancels=0;void onBtn1(void*){++cancels;parent=nullptr;}};
template<class T>struct Ref{T* p;explicit Ref(T* v):p(v){}T* data()const{return p;}T* operator->()const{return p;}};
template<class T>T typeinfo_cast(CCNode* n){return dynamic_cast<T>(n);}
struct CCTouchDispatcher{std::vector<FLAlertLayer*> released;static CCTouchDispatcher* get(){static CCTouchDispatcher d;return &d;}void unregisterForcePrio(FLAlertLayer* n){released.push_back(n);}};
struct CCDirector{CCNode* scene=nullptr;static CCDirector* sharedDirector(){static CCDirector d;return &d;}CCNode* getRunningScene(){return scene;}};
${close}
int main(){CCNode scene,nested;PauseLayer exact,other;FLAlertLayer own,foreign;scene.addChild(&nested);nested.addChild(&own);scene.addChild(&foreign);own.m_alertProtocol=&exact;foreign.m_alertProtocol=&other;CCDirector::sharedDirector()->scene=&scene;
 dismissNativeRankedExitConfirmation(&exact);assert(own.cancels==1&&!own.m_forcePrioRegistered&&foreign.cancels==0&&foreign.m_forcePrioRegistered&&foreign.parent==&scene);
 dismissNativeRankedExitConfirmation(&exact);assert(own.cancels==1&&CCTouchDispatcher::get()->released.size()==1);
 CCDirector::sharedDirector()->scene=nullptr;dismissNativeRankedExitConfirmation(&other);assert(foreign.cancels==0);
}
`);
});
