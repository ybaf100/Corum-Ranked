import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const root=new URL('..',import.meta.url).pathname,read=p=>readFileSync(join(root,p),'utf8');
function method(source,name){let start=source.indexOf(name);assert(start>=0,name);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');++end;}return source.slice(start,end);}
test('actual exact-ID history lookup retains the level through popup closure and routes cancelled responses without touching foreign delegates',()=>{
 const dir=mkdtempSync(join(tmpdir(),'history-level-'));
 const header=read('src/HistoryLevelLookup.hpp').replace(/^#include.*$|^#pragma once$/gm,'').replace('bool canOpenHistoryLevel();','').replace('void showHistoryLevel(int levelId);','');
 const source=read('src/HistoryLevelLookup.cpp'),hook=method(read('src/RankedLevelPreparation.cpp'),'void onGetOnlineLevelsCompleted(');
 try{
 writeFileSync(join(dir,'test.cpp'),`
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
#include <cstddef>
struct GJGameLevel {int m_levelID=0,refs=0;void retain(){++refs;}void release(){--refs;}};
namespace geode {template<class T>struct Ref {T*p=nullptr;Ref()=default;Ref(std::nullptr_t){}Ref(T*v):p(v){if(p)p->retain();}Ref(Ref const&v):Ref(v.p){}Ref(Ref&&v):p(v.p){v.p=nullptr;}~Ref(){if(p)p->release();}Ref&operator=(Ref v){std::swap(p,v.p);return *this;}T*data()const{return p;}T*operator->()const{return p;}explicit operator bool()const{return p;}};}
using geode::Ref;
namespace cocos2d {struct CCArray {std::vector<GJGameLevel*>values;unsigned count()const{return values.size();}GJGameLevel*objectAtIndex(unsigned i){return values.at(i);}};}
using cocos2d::CCArray;template<class T>T typeinfo_cast(GJGameLevel*p){return static_cast<T>(p);}namespace gd{using string=std::string;}
struct LevelManagerDelegate {virtual ~LevelManagerDelegate()=default;virtual void loadLevelsFinished(CCArray*,char const*){}virtual void loadLevelsFinished(CCArray*,char const*,int){}virtual void loadLevelsFailed(char const*){}virtual void loadLevelsFailed(char const*,int){}virtual void setupPageInfo(gd::string,char const*){}};
enum class SearchType{Type19};struct GJSearchObject {std::string query;static GJSearchObject*create(SearchType,std::string q){static GJSearchObject s;s.query=std::move(q);return &s;}std::string getKey()const{return query;}};
namespace fmt{std::string format(char const*,int id,std::uint64_t generation){return std::to_string(id)+"&gameVersion=22&corumHistory="+std::to_string(generation);}}
struct GameLevelManager {LevelManagerDelegate*m_levelManagerDelegate=nullptr;GJGameLevel*saved=nullptr;std::string requested;CCArray results;static GameLevelManager&instance(){static GameLevelManager g;return g;}static GameLevelManager*sharedState(){return &instance();}GJGameLevel*getSavedLevel(int){return saved;}void getOnlineLevels(GJSearchObject*s){requested=s->query;}void onGetOnlineLevelsCompleted(gd::string,gd::string tag){if(m_levelManagerDelegate)m_levelManagerDelegate->loadLevelsFinished(&results,tag.c_str());}};
namespace corum::ranked {struct RankedMapDetails:LevelManagerDelegate{static RankedMapDetails&get(){static RankedMapDetails d;return d;}void remember(GJGameLevel*){}std::uint64_t requestGeneration(char const*)const{return 0;}void responseCompleted(char const*,std::uint64_t){}};struct RankedLevelPreparation:LevelManagerDelegate{static RankedLevelPreparation&get(){static RankedLevelPreparation p;return p;}std::uint64_t requestGeneration(char const*)const{return 0;}void responseCompleted(char const*,std::uint64_t){}};}
namespace log{template<class...T>void warn(T const&...){} }
${header}
namespace corum::ranked {
${['HistoryLevelLookup& HistoryLevelLookup::get(','void HistoryLevelLookup::cancel(','std::uint64_t HistoryLevelLookup::begin(','bool HistoryLevelLookup::ownsResponse(','geode::Ref<GJGameLevel> HistoryLevelLookup::level(','std::string HistoryLevelLookup::error(','void HistoryLevelLookup::loadLevelsFinished(','void HistoryLevelLookup::loadLevelsFailed(','void HistoryLevelLookup::responseCompleted('].map(n=>method(source,n)).join('\n')}
}
struct Router:GameLevelManager{${hook}};
struct Foreign:LevelManagerDelegate{int calls=0;void loadLevelsFinished(CCArray*,char const*)override{++calls;}};
int main(){using namespace corum::ranked;auto& lookup=HistoryLevelLookup::get();auto& manager=GameLevelManager::instance();Router routed;Foreign foreign;routed.m_levelManagerDelegate=&foreign;
 auto a=lookup.begin(123);auto keyA=manager.requested;assert(keyA.starts_with("123&"));assert(!lookup.level(a));
 GJGameLevel wrong{999},exact{123};routed.results.values={&wrong,&exact};routed.onGetOnlineLevelsCompleted("ok",keyA);assert(routed.m_levelManagerDelegate==&foreign&&foreign.calls==0);
 {auto kept=lookup.level(a);assert(kept&&kept->m_levelID==123&&exact.refs==2);lookup.cancel(a);assert(kept->m_levelID==123&&exact.refs==1);}assert(exact.refs==0);
 auto b=lookup.begin(456);auto keyB=manager.requested;routed.onGetOnlineLevelsCompleted("stale",keyA);assert(!lookup.level(b)&&foreign.calls==0);
 routed.results.values={&wrong};routed.onGetOnlineLevelsCompleted("wrong exact ID",keyB);assert(!lookup.level(b)&&!lookup.error(b).empty());lookup.cancel(b);
 manager.saved=&exact;auto saved=lookup.begin(123);assert(lookup.level(saved));lookup.cancel(saved);manager.saved=nullptr;
 auto c=lookup.begin(123);auto keyC=manager.requested;lookup.cancel(c);routed.results.values={&exact};routed.onGetOnlineLevelsCompleted("cancelled",keyC);assert(foreign.calls==0&&!lookup.level(c));
 routed.onGetOnlineLevelsCompleted("foreign","other-key");assert(foreign.calls==1&&routed.m_levelManagerDelegate==&foreign);
}
`);
 execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-Wno-builtin-declaration-mismatch',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});

test('actual history scene handoff stops lobby BGM only after success and restores it only after returning',()=>{
 const dir=mkdtempSync(join(tmpdir(),'history-level-audio-'));
 try{
 writeFileSync(join(dir,'test.cpp'),`
#include <cassert>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
struct CCNode {CCNode*coordinator=nullptr;CCNode*getChildByIDRecursive(std::string const&){return coordinator;}};
struct CCScene:CCNode{};
template<class T>struct Ref{T*p=nullptr;Ref()=default;Ref(T*v):p(v){}T*data()const{return p;}T*operator->()const{return p;}explicit operator bool()const{return p;}};
template<class T>struct WeakRef{T*p=nullptr;WeakRef(T*v):p(v){}Ref<T>lock()const{return p;}};
std::string operator""_spr(char const*p,std::size_t n){return {p,n};}
std::vector<std::string>trace;
struct CCDirector{
 CCScene*scene=nullptr,*next=nullptr;int pushes=0;
 static CCDirector*sharedDirector(){static CCDirector d;return &d;}
 CCScene*getRunningScene(){return scene;}CCScene*getNextScene(){return next;}
 void pushScene(CCScene*s){assert(s);next=s;++pushes;trace.push_back("push");}
};
struct GJGameLevel{int m_levelID=0;};
struct LevelInfoLayer{
 static inline CCScene native;static inline bool succeeds=true;static inline int exactId=0,calls=0;
 static CCScene*scene(GJGameLevel*level,bool editor){assert(!editor);exactId=level->m_levelID;++calls;return succeeds?&native:nullptr;}
};
struct CCTransitionFade:CCScene{
 static inline bool succeeds=true;static inline CCScene*incoming=nullptr;
 static CCTransitionFade*create(float,CCScene*s){static CCTransitionFade t;incoming=s;return succeeds?&t:nullptr;}
};
namespace log{template<class...T>void info(T const&...){} }
namespace fmt{template<class...T>std::string format(T const&...){return "stage";}}
enum class RankedAudioMode{Silent,Lobby,LobbyPromotion,LobbyDemotionWarning,ResultWin,ResultLose,Round12,Round3DeathmatchWait};
struct RankedAudioManager{
 RankedAudioMode mode=RankedAudioMode::Lobby;int handoffs=0,requests=0;
 static RankedAudioManager&get(){static RankedAudioManager a;return a;}
 void fadeOutForGameplay(){mode=RankedAudioMode::Silent;++handoffs;trace.push_back("handoff");}
 void setMode(RankedAudioMode m){mode=m;++requests;}
};
enum class RuntimeStage{Ready,Matched,AwaitingUpdate};
struct RuntimeView{
 RuntimeStage stage=RuntimeStage::Ready;struct{bool active=false;}promotion;bool demotionWarning=false;
 struct{std::string state="ROUND_PLAYING",winnerSide="A",side="A";bool placement=false;int roundNumber=1;}match;
 struct{std::string runId;struct Stage{int stageIndex=1;std::string stageGrade="A";};std::vector<Stage>stageResults;}placement;
};
struct RankedRuntime{RuntimeView state;static RankedRuntime&get(){static RankedRuntime r;return r;}RuntimeView const&view()const{return state;}};
bool navigationAllowed=true;bool canOpenHistoryLevel(){return navigationAllowed;}
struct HistoryLevelLookup{
 Ref<GJGameLevel>found;std::string failure;
 static HistoryLevelLookup&get(){static HistoryLevelLookup l;return l;}
 Ref<GJGameLevel>level(std::uint64_t){return found;}std::string error(std::uint64_t){return failure;}
};
struct HistoryLevelPopup{
 std::uint64_t m_ticket=1;std::optional<WeakRef<CCScene>>m_origin;
 std::chrono::steady_clock::time_point m_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
 int closed=0,failed=0;
 void onClose(void*){++closed;trace.push_back("close");HistoryLevelLookup::get().found={};}
 void fail(){++failed;}
 ${method(read('src/HistoryLevelLookup.cpp'),'    void refresh(float)')}
};
struct Lobby{
 enum class Page{History,Live};Page m_page=Page::History;CCNode*parent=nullptr;bool running=true,gate=false;
 bool m_enteringSongGate=false,m_enteringLevel=false;std::string m_stageResultAudioPlayedKey;
 bool isRunning()const{return running;}CCNode*getParent(){return parent;}bool showResourceGate(){return gate;}
 ${method(read('src/RankedPopup.cpp'),'    void syncAudioMode()')}
};
int main(){
 CCScene origin,foreign;GJGameLevel exact{81121258};auto&d=*CCDirector::sharedDirector();
 auto&a=RankedAudioManager::get();auto&lookup=HistoryLevelLookup::get();auto&r=RankedRuntime::get();
 Lobby lobby;lobby.parent=&origin;d.scene=&origin;HistoryLevelPopup popup;popup.m_origin.emplace(&origin);
 popup.refresh(0);assert(a.handoffs==0&&popup.closed==0&&popup.failed==0&&d.pushes==0);
 lookup.found=&exact;popup.refresh(0);
 assert(a.handoffs==1&&a.mode==RankedAudioMode::Silent&&popup.closed==1&&d.pushes==1);
 assert(LevelInfoLayer::exactId==81121258&&CCTransitionFade::incoming==&LevelInfoLayer::native);
 assert((trace==std::vector<std::string>{"handoff","close","push"}));
 // A queued outgoing refresh, the fade, LevelInfo and normal Play must not
 // reclaim the independent Ranked channel. The retained history returns intact.
 lobby.syncAudioMode();assert(a.requests==0);
 d.scene=&LevelInfoLayer::native;d.next=nullptr;lobby.syncAudioMode();assert(a.requests==0);
 d.scene=&foreign;lobby.syncAudioMode();assert(a.requests==0);
 d.scene=&origin;lobby.running=false;lobby.syncAudioMode();assert(a.requests==0);
 lobby.running=true;lobby.syncAudioMode();assert(a.requests==1&&a.mode==RankedAudioMode::Lobby);
 r.state.promotion.active=true;lobby.syncAudioMode();assert(a.mode==RankedAudioMode::LobbyPromotion);
 r.state.promotion.active=false;r.state.demotionWarning=true;lobby.syncAudioMode();assert(a.mode==RankedAudioMode::LobbyDemotionWarning);
 r.state={};
 // Every failed/cancelled lookup leaves currently playing lobby audio alone.
 for(int path=0;path<7;++path){
  HistoryLevelPopup p;p.m_origin.emplace(&origin);lookup.found=&exact;lookup.failure.clear();
  d.scene=&origin;d.next=nullptr;LevelInfoLayer::succeeds=true;CCTransitionFade::succeeds=true;navigationAllowed=true;
  if(path==0)LevelInfoLayer::succeeds=false;
  if(path==1)CCTransitionFade::succeeds=false;
  if(path==2){lookup.found={};lookup.failure="unavailable";}
  if(path==3){lookup.found={};p.m_deadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);}
  if(path==4)d.scene=&foreign;
  if(path==5)d.next=&foreign;
  if(path==6)navigationAllowed=false;
  auto handoffs=a.handoffs,pushes=d.pushes;p.refresh(0);assert(a.handoffs==handoffs&&d.pushes==pushes);
  assert(path<4?p.failed==1:p.closed==1);
 }
 navigationAllowed=true;d.scene=&origin;d.next=nullptr;LevelInfoLayer::succeeds=true;CCTransitionFade::succeeds=true;
 // Existing active-round audio policy and the Survival coordinator retain ownership.
 lobby.m_page=Lobby::Page::Live;r.state.stage=RuntimeStage::Matched;lobby.syncAudioMode();assert(a.mode==RankedAudioMode::Round12);
 r.state.match.roundNumber=3;lobby.syncAudioMode();assert(a.mode==RankedAudioMode::Round3DeathmatchWait);
 auto before=a.requests;origin.coordinator=&origin;lobby.syncAudioMode();assert(a.requests==before);origin.coordinator=nullptr;
}
`);
 execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-Wno-builtin-declaration-mismatch',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
 execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
