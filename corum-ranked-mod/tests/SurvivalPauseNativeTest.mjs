// Production methods run against explicit Cocos/HTTP boundaries. Device touch,
// native scene teardown, FMOD and vanilla persistence still need a GD device.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
function method(source,name){const start=source.indexOf(name);assert(start>=0,name);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');end++;}return source.slice(start,end).replaceAll('std::chrono::steady_clock::now()','testNow()');}
function run(code,flags=[]){const dir=mkdtempSync(join(tmpdir(),'survival-pause-'));try{
 writeFileSync(join(dir,'test.cpp'),code);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),...flags,join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}
const runtime=read('src/SurvivalRuntime.cpp'),pause=read('src/RankedPauseLayer.cpp');
const viewer=read('tests/SpectatorLiveViewerNativeTest.mjs');
const json=viewer.slice(viewer.indexOf('namespace matjson {'),viewer.indexOf('${readFileSync(join(root,'));
const common=`
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>
${json}
auto wall=std::chrono::steady_clock::time_point{};auto testNow(){return wall;}
template<class T>struct Ref{T* p=nullptr;Ref()=default;Ref(T* p):p(p){}T* data()const{return p;}T* operator->()const{return p;}explicit operator bool()const{return p;}};
template<class T>struct WeakRef{T* p;explicit WeakRef(T* p):p(p){}Ref<T> lock()const{return {p&&p->alive?p:nullptr};}};
struct CCNode{virtual ~CCNode()=default;CCNode* parent=nullptr;bool alive=true,running=true;bool isRunning()const{return running;}CCNode* getParent()const{return parent;}};
struct CCScene:CCNode{};struct CCTransitionScene:CCScene{};
template<class T>T typeinfo_cast(CCNode* n){return dynamic_cast<T>(n);}
struct CCDirector{CCScene* scene=nullptr;CCScene* next=nullptr;static CCDirector* sharedDirector(){static CCDirector d;return &d;}CCScene* getRunningScene(){return scene;}CCScene* getNextScene(){return next;}struct Size{float width=568;};Size getWinSize()const{return {};}};
struct PlayLayer:CCNode{static inline PlayLayer* current=nullptr;static PlayLayer* get(){return current;}int quits=0;virtual void onQuit(){++quits;running=false;}};
std::vector<std::function<void()>> queued;void queueInMainThread(std::function<void()> fn){queued.push_back(std::move(fn));}void drain(){auto tasks=std::exchange(queued,{});for(auto& fn:tasks)fn();}
namespace fmt{template<class...T>std::string format(std::string text,T const&...values){auto put=[&](auto const& value){auto at=text.find('{'),end=text.find('}',at);std::ostringstream out;if(text.substr(at,end-at)=="{:02")out<<std::setfill('0')<<std::setw(2);out<<value;text.replace(at,end-at+1,out.str());};(put(values),...);return text;}}
`;

test('Pause preserves a synchronized server countdown across errors, polling gaps and reopening; unknown time stays unknown in every phase',()=>{
 const watch=method(pause,'void watchBudget(float)');
 const survival=watch.slice(0,watch.indexOf('        auto& ranked ='))+'}';
 run(common+`
#include "domain/ServerClock.cpp"
std::int64_t localMillis=0;std::int64_t localNow(){return localMillis;}
namespace corum::ranked {
struct SurvivalRuntime{
 struct View{std::string id="m",roundKey="r1",state="PLAYING",phase="ROUND_1",error,endsAt="2026-10-16T00:03:00Z";int mapGeneration=1;bool ownEliminated=false;}v;
 ServerClock m_clock;struct{std::string endsAt="2026-10-16T00:03:00Z";}m_view;
 static SurvivalRuntime&get(){static SurvivalRuntime r;return r;}void tick(){}auto const&view()const{return v;}bool canPlay()const{return !v.ownEliminated;}bool canSurrender()const{return !v.ownEliminated;}bool surrenderPending()const{return false;}bool normalRoundCleared()const{return false;}
 std::optional<std::int64_t> roundRemainingMs()const;
};
${method(runtime,'std::optional<std::int64_t> SurvivalRuntime::roundRemainingMs()')}
int exits=0;bool requestSurvivalGameplayReturn(PlayLayer*){++exits;return true;}
}
using corum::ranked::SurvivalRuntime;
namespace i18n{std::string text(std::string const&,std::string fallback){return fallback;}}
struct Label{std::string value;void setString(char const* s){value=s;}};
namespace ui{void fitLabelWidth(Label*,float,float,float){}}
struct Button{bool enabled=true;void setEnabled(bool b){enabled=b;}};
struct Pause{Label clock,status;Button button;struct Fields{bool survival=true;Label*budget;Label*survivalStatus;Button*surrender;std::optional<WeakRef<PlayLayer>>owner;std::optional<WeakRef<CCNode>>survivalConfirmation;std::string matchId="m",survivalRoundKey="r1";int survivalMapGeneration=1;}fields{true,&clock,&status,&button,{},{},"m","r1",1};Fields*m_fields=&fields;
 void synchronizeSurvivalPauseInput(){}
 ${survival}
};
int main(){auto&r=SurvivalRuntime::get();Pause p;PlayLayer play;p.fields.owner.emplace(&play);
 p.watchBudget(0);assert(p.clock.value=="R1 · --:--");assert(corum::ranked::exits==0);
 // The local wall clock is deliberately far from the server's UTC clock.
 localMillis=900000;assert(r.m_clock.observe("2026-10-16T00:00:00Z",localMillis));
 for(auto phase:{"ROUND_1","ROUND_2","ROUND_3","FINAL","TIEBREAK"}){
  r.v.phase=phase;p.watchBudget(0);assert(p.clock.value.ends_with("03:00"));
 }
 assert(p.clock.value=="DEATH MATCH · 03:00");r.v.phase="FINAL";r.v.error="Survival state changed. Synchronizing...";
 localMillis+=61000;p.watchBudget(0);assert(p.clock.value=="FINAL · 01:59"&&p.status.value==r.v.error);
 r.v.error="network failed";localMillis+=1000;p.watchBudget(0);assert(p.clock.value=="FINAL · 01:58");
 Pause reopened;reopened.watchBudget(0);assert(reopened.clock.value==p.clock.value);
 assert(!r.m_clock.observe("invalid",localMillis));localMillis+=1000;p.watchBudget(0);assert(p.clock.value=="FINAL · 01:57");
 assert(r.m_clock.observe("2026-10-16T00:01:04Z",localMillis));r.v.error.clear();p.watchBudget(0);assert(p.clock.value=="FINAL · 01:56");
 localMillis+=999999;p.watchBudget(0);assert(p.clock.value=="FINAL · 00:00"&&corum::ranked::exits==0);
 r.m_view.endsAt="invalid";p.watchBudget(0);assert(p.clock.value=="FINAL · --:--");
 r.m_clock={};r.m_view.endsAt="2026-10-16T00:03:00Z";p.watchBudget(0);assert(p.clock.value=="FINAL · --:--");
 CCNode detached;detached.parent=&play;p.fields.survivalConfirmation.emplace(&detached);
 p.watchBudget(0);assert(!p.button.enabled);detached.parent=nullptr;p.watchBudget(0);
 assert(p.button.enabled&&!p.fields.survivalConfirmation);
 r.v.ownEliminated=true;p.watchBudget(0);assert(corum::ranked::exits==1);
}
`);
});

test('Surrender serializes End before one stable FORFEIT, waits for server evidence, returns directly on ACK and rejects errors/stale scene owners',()=>{
 run(common+`
namespace utils::random{std::string generateUUID(){static int n=0;return "event-"+std::to_string(++n);}}
namespace web{struct WebResponse{int status;matjson::Value root;int code()const{return status;}matjson::Result<matjson::Value>json()const{return {root};}};struct Operation{matjson::Value body;};struct WebRequest{matjson::Value body;void bodyJSON(matjson::Value v){body=v;}Operation post(std::string const&){return {body};}};struct Pending{Operation op;std::function<void(WebResponse)>done;};std::vector<Pending>pending;}
web::WebRequest request(std::string const&){return {};}
struct Request{int cancellations=0;void cancel(){++cancellations;}void spawn(web::Operation op,std::function<void(web::WebResponse)>fn){web::pending.push_back({op,fn});}};
namespace corum::ranked{
bool isOfficialSurvivalPlayLayer(PlayLayer*p){return p&&p==PlayLayer::get();}int returns=0;bool requestSurvivalGameplayReturn(PlayLayer*){++returns;return true;}
std::string preparationContextText(std::string s){return s;}
struct SurvivalRuntime{
 struct View{std::string id="match",roundKey="r1",state="PLAYING",phase="ROUND_1",error,activeAttemptId="a";double activeAttemptProgress=45;int mapGeneration=1;bool ownEliminated=false;struct{int levelId=4;}map;}m_view;
 std::string m_surrenderEventId,m_surrenderMatchId,m_session="session",m_attempt="a",m_attemptContext="r1",m_presentationContext;bool m_surrenderAccepted=false,m_eventBusy=false;
 std::optional<WeakRef<PlayLayer>>m_surrenderOwner;std::optional<WeakRef<CCScene>>m_surrenderScene;
 std::uint64_t m_generation=1,m_eventGeneration=0;double m_attemptBest=77;std::deque<matjson::Value>m_events;Request m_eventRequest;std::chrono::steady_clock::time_point m_retryAt{},m_nextPoll{};
 bool active()const{return !m_view.id.empty();}bool current(std::uint64_t g,std::string const&s)const{return g==m_generation&&s==m_session;}
 bool surrenderPending()const{return !m_surrenderEventId.empty()&&!m_surrenderAccepted;}
 std::string preparationContext()const{return m_view.roundKey;}std::string matchEndpoint(std::string const&p)const{return p;}
 void observe(matjson::Value const&v){if(v.contains("eliminated"))m_view.ownEliminated=v["eliminated"].asBool().unwrapOr(false);}
 matjson::Value event(std::string const&)const;void endAttempt(double);void enqueue(matjson::Value);void pumpEvents();void discardObsoleteEvents();void surrender();void resetSurrender();void pumpSurrenderReturn();
};
${['matjson::Value SurvivalRuntime::event(', 'void SurvivalRuntime::enqueue(', 'void SurvivalRuntime::endAttempt(', 'void SurvivalRuntime::pumpEvents()', 'void SurvivalRuntime::discardObsoleteEvents()', 'void SurvivalRuntime::resetSurrender()', 'void SurvivalRuntime::surrender()', 'void SurvivalRuntime::pumpSurrenderReturn()'].map(n=>method(runtime,n)).join('\n')}
}
int main(){using namespace corum::ranked;CCScene scene,other;PlayLayer play,replacement;play.parent=&scene;replacement.parent=&scene;PlayLayer::current=&play;CCDirector::sharedDirector()->scene=&scene;
 matjson::Value accepted;accepted["eliminated"]=true;
 for(auto phase:{"ROUND_1","ROUND_2","ROUND_3","FINAL","TIEBREAK"}){
  SurvivalRuntime r;r.m_view.phase=phase;auto start=web::pending.size();int before=returns;
  r.surrender();r.surrender();assert(r.surrenderPending()&&web::pending.size()==start+1&&returns==before);
  assert(web::pending[start].op.body["type"].asString().unwrapOr("")=="ATTEMPT_END");
  assert(web::pending[start].op.body["progress"].asDouble().unwrapOr(0)==77);assert(r.m_events.size()==2);
  web::pending[start].done({201,{}});r.pumpEvents();auto forfeit=web::pending.back();auto id=r.m_surrenderEventId;
  assert(forfeit.op.body["type"].asString().unwrapOr("")=="FORFEIT");
  forfeit.done({503,{}});assert(returns==before&&r.surrenderPending());wall+=std::chrono::milliseconds(500);r.pumpEvents();assert(web::pending.back().op.body["eventId"].asString().unwrapOr("")==id);
  // A 2xx response without authoritative elimination is not a successful exit.
  web::pending.back().done({201,{}});assert(returns==before&&r.surrenderPending()&&r.m_events.size()==1);
  wall+=std::chrono::milliseconds(500);r.pumpEvents();web::pending.back().done({201,accepted});
  assert(returns==before+1&&!r.surrenderPending()&&r.m_surrenderAccepted&&r.m_events.empty()); // No tick/poll/user action.
  r.surrender();assert(web::pending.back().op.body["eventId"].asString().unwrapOr("")==id);
 }
 {SurvivalRuntime r;r.m_view.state="MAP_SELECTION";r.m_view.map.levelId=0;r.m_attempt.clear();PlayLayer::current=nullptr;
  auto before=returns;auto requests=web::pending.size();r.surrender();r.surrender();assert(web::pending.size()==requests+1);
  assert(web::pending.back().op.body["type"].asString().unwrapOr("")=="FORFEIT");web::pending.back().done({201,accepted});
  assert(r.m_surrenderAccepted&&!r.surrenderPending()&&r.m_events.empty()&&returns==before);PlayLayer::current=&play;
 }
 {SurvivalRuntime r;r.surrender();web::pending.back().done({403,{}});assert(!r.surrenderPending()&&r.m_events.empty());} // Rejected prerequisite End cannot strand the UI.
 for(int status:{400,401,403,404,409}){SurvivalRuntime r;r.m_attempt.clear();r.surrender();web::pending.back().done({status,{}});assert(!r.surrenderPending()&&!r.m_surrenderAccepted&&r.m_events.empty());}
 {SurvivalRuntime r;r.m_attempt.clear();r.surrender();auto delayed=web::pending.back().done;auto before=returns;PlayLayer::current=&replacement;delayed({201,accepted});assert(returns==before);PlayLayer::current=&play;r.pumpSurrenderReturn();assert(returns==before+1);}
 {SurvivalRuntime r;r.m_attempt.clear();r.surrender();auto before=returns;CCDirector::sharedDirector()->scene=&other;web::pending.back().done({201,accepted});assert(returns==before);CCDirector::sharedDirector()->scene=&scene;}
 {SurvivalRuntime r;r.m_attempt.clear();r.surrender();auto delayed=web::pending.back().done;++r.m_generation;auto before=returns;delayed({201,accepted});assert(!r.m_surrenderAccepted&&returns==before);}
 {SurvivalRuntime r;r.m_attempt.clear();r.surrender();r.m_view.roundKey="r2";r.discardObsoleteEvents();assert(!r.surrenderPending()&&r.m_events.empty());}
 {SurvivalRuntime r;r.m_attempt.clear();r.surrender();auto before=returns;r.m_view.ownEliminated=true;r.pumpSurrenderReturn();assert(returns==before+1&&!r.surrenderPending());} // Recovery poll.
}
`);
});

test('production deferred Survival return queues once, respects pause refusal/completion and never quits a replacement scene',()=>{
 const playSource=read('src/PlayLayer.cpp');
 run(common+`
struct UILayer{int disabled=0;void disableMenu(){++disabled;}};
namespace corum::ranked{bool completion=false,resumable=true;int resumes=0;bool quitThroughSurvivalCompletionOverlay(PlayLayer*){return completion;}bool resumeSurvivalFreePlayPauseBeforeQuit(PlayLayer*){++resumes;return resumable;}}
struct CorumRankedPlayLayer:PlayLayer{
 struct Fields{bool autoExitRequested=false,safetyRetired=false,rankedInitialized=true,survivalLevel=true,survivalNativeClearFinished=false,survivalFreePlayHud=false;}fields;Fields*m_fields=&fields;
 bool m_hasCompletedLevel=false;std::vector<int>m_queuedButtons{1};UILayer ui;UILayer*m_uiLayer=&ui;bool touch=true,keyboard=true;
 void setTouchEnabled(bool b){touch=b;}void setKeyboardEnabled(bool b){keyboard=b;}
 void onQuit()override{if(corum::ranked::resumable)PlayLayer::onQuit();}
 ${method(playSource,'void queueSurvivalGameplayReturn()')}
};
`
+`
int main(){using namespace corum::ranked;CCScene scene,other;auto*d=CCDirector::sharedDirector();d->scene=&scene;
 {CorumRankedPlayLayer p;PlayLayer::current=&p;p.parent=&scene;p.queueSurvivalGameplayReturn();p.queueSurvivalGameplayReturn();assert(queued.size()==1&&p.quits==0&&!p.touch&&!p.keyboard&&p.m_queuedButtons.empty());drain();assert(p.quits==1);p.queueSurvivalGameplayReturn();assert(queued.empty());}
 {CorumRankedPlayLayer p;PlayLayer::current=&p;p.parent=&scene;resumable=false;p.queueSurvivalGameplayReturn();drain();assert(p.quits==0&&!p.fields.autoExitRequested);resumable=true;p.queueSurvivalGameplayReturn();drain();assert(p.quits==1);}
 {CorumRankedPlayLayer p;PlayLayer::current=&p;p.parent=&scene;d->next=&other;p.queueSurvivalGameplayReturn();assert(queued.empty());d->next=nullptr;p.queueSurvivalGameplayReturn();d->next=&other;drain();assert(p.quits==0&&!p.fields.autoExitRequested);d->next=nullptr;}
 {CorumRankedPlayLayer p,replacement;PlayLayer::current=&p;p.parent=&scene;p.queueSurvivalGameplayReturn();PlayLayer::current=&replacement;drain();assert(p.quits==0&&replacement.quits==0);}
 {CorumRankedPlayLayer p;PlayLayer::current=&p;p.parent=&scene;p.queueSurvivalGameplayReturn();d->scene=&other;drain();assert(p.quits==0);d->scene=&scene;}
 {CorumRankedPlayLayer p;PlayLayer::current=&p;p.parent=&scene;p.m_hasCompletedLevel=true;p.queueSurvivalGameplayReturn();assert(queued.empty());p.fields.survivalNativeClearFinished=true;completion=true;p.queueSurvivalGameplayReturn();drain();assert(p.quits==0&&p.fields.autoExitRequested);completion=false;}
}
`);
});

test('Force Clear hit target bypasses native Pause swallowing priorities and real ConfirmPopup cancels, confirms once and rejects retired contexts',()=>{
 const strip=s=>s.replace(/^#include .*?$|^#pragma once$/gm,'');
 const surrender=method(pause,'void onSurrender(CCObject*)');
 const survivalSurrender=surrender.slice(0,surrender.indexOf('        if (!m_fields->placement'))+'}';
 run(common+`
struct CCObject{virtual ~CCObject()=default;};
struct CCPoint{float x=0,y=0;};struct CCSize{float width,height;};constexpr CCPoint CCPointZero{};
struct Node:CCNode{std::vector<Node*>children;std::string id;CCSize size{0,0};CCPoint pos;int tag=0;bool visible=true;
 void addChild(Node*n,int=0){children.push_back(n);n->parent=this;}void setID(std::string s){id=s;}void setPosition(CCPoint p){pos=p;}void setVisible(bool b){visible=b;}void setTag(int n){tag=n;}int getTag()const{return tag;}void autorelease(){}
 void removeFromParentAndCleanup(bool){parent=nullptr;running=false;}virtual void onExit(){}
};
struct Handler{int m_nPriority;};
struct CCTouchDispatcher{std::map<CCNode*,Handler>handlers;int releases=0;std::set<CCNode*>forced;static CCTouchDispatcher*get(){static CCTouchDispatcher d;return &d;}Handler*findHandler(CCNode*n){auto it=handlers.find(n);return it==handlers.end()?nullptr:&it->second;}void setPriority(int p,CCNode*n){handlers[n].m_nPriority=p;}void unregisterForcePrio(CCNode*n){if(forced.erase(n))++releases;}};
struct CCMenu:Node{static CCMenu*create(){return new CCMenu;}};
struct CCMenuItemSpriteExtra:Node{bool enabled=true;static CCMenuItemSpriteExtra*create(Node*face,CCNode*,int){auto*n=new CCMenuItemSpriteExtra;n->size=face->size;n->addChild(face);return n;}void setEnabled(bool b){enabled=b;}};
struct Label:Node{};
struct Popup:Node{~Popup(){CCTouchDispatcher::get()->unregisterForcePrio(this);}Node*m_bgSprite=new Node;Node*m_closeBtn=new Node;Node*m_mainLayer=new Node;CCMenu*m_buttonMenu=new CCMenu;static inline Popup*last=nullptr;static inline int opens=0;bool init(float,float){CCTouchDispatcher::get()->forced.insert(this);return true;}virtual void onClose(CCObject*){removeFromParentAndCleanup(true);onExit();}void show(){static Node scene;scene.addChild(this);last=this;++opens;}};
std::string operator""_spr(char const*s,std::size_t n){return std::string(s,n);}
#define menu_selector(x) 0
namespace geode::prelude{}
namespace geode::log{template<class...T>void debug(T const&...){}template<class...T>void info(T const&...){}}
namespace log=geode::log;
namespace corum::ranked{
namespace i18n{std::string text(char const*,char const*fallback){return fallback;}}
namespace ui{constexpr int Gold=1,Surface=2,SurfaceRaised=3,Text=4,Blue=5;enum class FontWeight{ExtraBold,SemiBold};Node*surface(CCSize s,CCPoint p,int,int,int,bool){auto*n=new Node;n->size=s;n->pos=p;return n;}Label*menuLabel(std::string,float,CCPoint,int,FontWeight){return new Label;}void fitMenuLabelWidth(Label*,float,float,float){}void fitFullMenuLabel(Label*,float,float,float){}
}
struct SurvivalRuntime{struct View{std::string id="m",roundKey="r1",phase="ROUND_1";int mapGeneration=1;bool debug=true,online=false,ownEliminated=false,ownAssetsReady=true,ownEntered=true;}m_view;bool playable=true,pending=false,cleared=false,surrenderable=true;int submissions=0,surrenders=0;static SurvivalRuntime&get(){static SurvivalRuntime r;return r;}auto const&view()const{return m_view;}bool canPlay()const{return playable;}bool canSurrender()const{return surrenderable;}std::string surrenderMessage()const{return "Forfeit?";}void surrender(){++surrenders;surrenderable=false;}bool normalRoundCleared()const{return cleared;}bool forceClearPending()const{return pending;}bool canForceClear()const;void forceClear(){if(canForceClear()&&!pending){++submissions;pending=true;}}};
${method(runtime,'bool SurvivalRuntime::canForceClear()')}
}
using corum::ranked::SurvivalRuntime;namespace ui=corum::ranked::ui;namespace i18n=corum::ranked::i18n;
#define CORUM_RANKED_NATIVE_TEST 1
${strip(read('src/ui/RankedConfirmPopup.hpp'))}
struct PauseLayer:Node{virtual void onEnter(){}}
;
struct CorumRankedPauseLayer:PauseLayer{
 CCMenu center;
 struct Fields{bool survival=true;CCMenu*survivalActions=nullptr;CCMenuItemSpriteExtra*forceClear=nullptr;CCMenu*forceClearMenu=nullptr;std::optional<WeakRef<ui::RankedConfirmPopup>>survivalConfirmation;std::optional<WeakRef<PlayLayer>>owner;std::string matchId="m",survivalRoundKey="r1";int survivalMapGeneration=1;}fields;Fields*m_fields=&fields;
 CCNode*getChildByIDRecursive(char const*){return &center;}
 ${method(pause,'void setupSurvivalForceClear(')}
 ${method(pause,'void synchronizeSurvivalPauseInput()')}
 ${method(pause,'void onForceClear(')}
 ${survivalSurrender}
};
int main(){CCScene scene;PlayLayer play;play.parent=&scene;PlayLayer::current=&play;CCDirector::sharedDirector()->scene=&scene;auto&r=SurvivalRuntime::get();
 for(int native:{-128,-502,-1502}){
  CorumRankedPauseLayer p;p.parent=&scene;p.fields.owner.emplace(&play);r.m_view.ownEntered=false;
  p.setupSurvivalForceClear({568,320});assert(p.fields.forceClear&&!r.canForceClear());
  p.onForceClear(nullptr);r.m_view.ownEntered=true;p.setupSurvivalForceClear({568,320});auto*button=p.fields.forceClear;auto*menu=p.fields.forceClearMenu;assert(button&&menu&&button->parent==menu&&menu->parent==&p);
  assert(button->size.width==112&&button->size.height==30&&button->pos.x==492&&button->pos.y==282);
  // The action is beyond the native center row and to the right of the clock.
  assert(button->pos.y-button->size.height/2>160+81.5/2);assert(button->pos.x-button->size.width/2>308);
  auto*d=CCTouchDispatcher::get();d->handlers.clear();d->handlers[&p]={native-1};d->handlers[&p.center]={native};d->handlers[menu]={-128};
  auto tap=[&](float x,float y){
   std::vector<std::pair<int,std::function<bool()>>> handlers;
   handlers.push_back({d->handlers[&p].m_nPriority,[]{return true;}}); // Native Pause swallowing boundary.
   handlers.push_back({d->handlers[menu].m_nPriority,[&]{
    if(!button->enabled||!button->visible||std::abs(x-button->pos.x)>button->size.width/2||std::abs(y-button->pos.y)>button->size.height/2)return false;
    p.onForceClear(nullptr);return true;
   }});
   std::stable_sort(handlers.begin(),handlers.end(),[](auto const&a,auto const&b){return a.first<b.first;});
   for(auto const&handler:handlers)if(handler.second())break;
  };
  auto beforeTap=Popup::opens;tap(button->pos.x,button->pos.y);assert(Popup::opens==beforeTap);
  p.synchronizeSurvivalPauseInput();assert(d->handlers[menu].m_nPriority<d->handlers[&p].m_nPriority&&d->handlers[menu].m_nPriority<d->handlers[&p.center].m_nPriority);
  auto opened=Popup::opens;tap(10,10);assert(Popup::opens==opened);tap(button->pos.x,button->pos.y);tap(button->pos.x,button->pos.y);assert(Popup::opens==opened+1&&r.submissions==0);
  auto*dialog=static_cast<ui::RankedConfirmPopup*>(Popup::last);
  d->handlers[menu].m_nPriority=1;p.synchronizeSurvivalPauseInput();assert(d->handlers[menu].m_nPriority==1);
  auto*choices=dialog->m_buttonMenu;auto*cancel=choices->children[0];
  auto released=d->releases;
  dialog->onChoice(reinterpret_cast<CCObject*>(cancel));drain();assert(!p.fields.survivalConfirmation&&r.submissions==0);
  assert(d->releases==released+1);delete dialog;assert(d->releases==released+1);
  p.onForceClear(nullptr);dialog=static_cast<ui::RankedConfirmPopup*>(Popup::last);choices=dialog->m_buttonMenu;auto*confirm=choices->children[1];
  // onChoice uses typeinfo_cast at the native CCObject boundary below.
  dialog->onChoice(static_cast<CCObject*>(confirm));dialog->onChoice(static_cast<CCObject*>(confirm));drain();assert(r.submissions==1&&r.pending&&!p.fields.survivalConfirmation);(void)confirm;
  p.onForceClear(nullptr);assert(Popup::opens==opened+2);r.pending=false;r.submissions=0;
  p.onForceClear(nullptr);dialog=static_cast<ui::RankedConfirmPopup*>(Popup::last);++r.m_view.mapGeneration;dialog->finish(true);drain();assert(r.submissions==0);--r.m_view.mapGeneration;
 }
 // The same production confirmation path is reachable when gameplay's
 // clock/attempt gate is false. Confirm once, cancel, and reject stale scenes.
 {CorumRankedPauseLayer p;p.parent=&scene;p.fields.owner.emplace(&play);r.playable=false;
  auto before=Popup::opens;p.onSurrender(nullptr);p.onSurrender(nullptr);assert(Popup::opens==before+1&&r.surrenders==0);
  auto*dialog=static_cast<ui::RankedConfirmPopup*>(Popup::last);dialog->finish(false);drain();assert(r.surrenders==0&&!p.fields.survivalConfirmation);
  p.onSurrender(nullptr);dialog=static_cast<ui::RankedConfirmPopup*>(Popup::last);dialog->finish(true);dialog->finish(true);drain();assert(r.surrenders==1&&!p.fields.survivalConfirmation);
  p.onSurrender(nullptr);assert(Popup::opens==before+2);r.surrenderable=true;
  p.onSurrender(nullptr);dialog=static_cast<ui::RankedConfirmPopup*>(Popup::last);play.running=false;dialog->finish(true);drain();assert(r.surrenders==2);play.running=true; // paused native owner remains authorized
 }
 // A platform may register the native menu without a Pause-layer handler.
 {CorumRankedPauseLayer p;p.parent=&scene;p.fields.survivalActions=&p.center;p.setupSurvivalForceClear({568,320});
  auto* d=CCTouchDispatcher::get();d->handlers.clear();d->handlers[&p.center]={-1502};d->handlers[p.fields.forceClearMenu]={-128};
  p.synchronizeSurvivalPauseInput();assert(d->handlers[p.fields.forceClearMenu].m_nPriority<=-1502);
  auto priority=d->handlers[p.fields.forceClearMenu].m_nPriority;p.synchronizeSurvivalPauseInput();assert(d->handlers[p.fields.forceClearMenu].m_nPriority==priority);
 }
 struct Coordinator:Node{std::optional<WeakRef<ui::RankedConfirmPopup>>m_confirmation;std::string m_confirmationContext;
  static std::string text(char const*,char const*fallback){return fallback;}
    void confirmSurrender() {
        if(m_confirmation || !SurvivalRuntime::get().canSurrender())return;
        auto const& view=SurvivalRuntime::get().view();m_confirmationContext=view.id+":"+view.roundKey+":"+std::to_string(view.mapGeneration);auto const id=view.id,round=view.roundKey;auto const generation=view.mapGeneration;
        auto* popup=ui::RankedConfirmPopup::create(text("surrender-confirm","CONFIRM SURRENDER"),
            SurvivalRuntime::get().surrenderMessage(),
            [weak=WeakRef(this),id,round,generation](bool accepted){
                auto owner=weak.lock();if(!owner || !owner->getParent())return;owner->m_confirmation.reset();
                auto const& current=SurvivalRuntime::get().view();
                if(accepted && current.id==id && current.roundKey==round && current.mapGeneration==generation &&
                    SurvivalRuntime::get().canSurrender())SurvivalRuntime::get().surrender();
            });
        if(popup){m_confirmation.emplace(popup);popup->show();}
    }
};
 {Coordinator room;room.parent=&scene;PlayLayer::current=nullptr;r.surrenderable=true;
  auto before=r.surrenders;room.confirmSurrender();room.confirmSurrender();auto* dialog=static_cast<ui::RankedConfirmPopup*>(Popup::last);
  dialog->finish(true);drain();assert(r.surrenders==before+1&&!room.m_confirmation);PlayLayer::current=&play;r.surrenderable=true;
 }
 r.m_view.online=true;assert(!r.canForceClear());r.m_view.online=false;r.m_view.debug=false;assert(!r.canForceClear());r.m_view.debug=true;r.m_view.phase="TIEBREAK";assert(!r.canForceClear());
}
` .replace('struct CCObject{virtual ~CCObject()=default;};','using CCObject=CCNode;')
 .replace('CCDirector::sharedDirector()->getTouchDispatcher()', 'CCTouchDispatcher::get()')
 .replace('log::debug', 'geode::log::debug')
 .replace('log::info', 'geode::log::info')
 .replace('private:', 'public:')
 .replace('class RankedConfirmPopup final : public Popup {','class RankedConfirmPopup final : public Popup { public:')
 .replace('reinterpret_cast<CCObject*>(cancel)', 'static_cast<CCObject*>(cancel)'),['-DCORUM_RANKED_DEBUG_BOT_MATCH=1']);
});


test('surrender remains available without a countdown sample and rejects inactive, foreign and terminal sessions',()=>{
 run(common+`
namespace corum::ranked {
struct RankedRuntime{std::string session="live";static RankedRuntime&get(){static RankedRuntime r;return r;}std::string spectatorLiveSessionToken()const{return session;}};
struct SurvivalRuntime{
 struct{std::string state="PLAYING";bool ownEliminated=false;}m_view;
 std::string m_session="live";bool exists=true,pending=false,cleared=false,free=false;
 bool active()const{return exists;}bool surrenderPending()const{return pending;}
 bool normalRoundCleared()const{return cleared;}bool freePlaying()const{return free;}
 bool canSurrender()const;
};
${method(runtime,'bool SurvivalRuntime::canSurrender()')}
}
int main(){using namespace corum::ranked;SurvivalRuntime r;
 for(auto* state:{"MAP_SELECTION","PREPARING","ENTERING","COUNTDOWN","PLAYING"}){r.m_view.state=state;assert(r.canSurrender());}
 for(auto* state:{"ROUND_RESULT","FINAL_RESULT","CANCELLED",""}){r.m_view.state=state;assert(!r.canSurrender());}
 r.m_view.state="PLAYING";r.pending=true;assert(!r.canSurrender());r.pending=false;
 r.cleared=true;assert(!r.canSurrender());r.cleared=false;r.free=true;assert(!r.canSurrender());r.free=false;
 r.m_session="old";assert(!r.canSurrender());r.m_session.clear();assert(!r.canSurrender());r.m_session="live";
 r.m_view.ownEliminated=true;assert(!r.canSurrender());r.m_view.ownEliminated=false;r.exists=false;assert(!r.canSurrender());
}
`);
 assert.doesNotMatch(pause,/void onEnter\(\)/); // inherited onEnter has no PauseLayer modify binding
 assert.match(method(pause,'void watchBudget(float)'),/synchronizeSurvivalPauseInput\(\)/);
});
