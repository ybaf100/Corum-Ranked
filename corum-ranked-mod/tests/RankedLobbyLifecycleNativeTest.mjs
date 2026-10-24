import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join, resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = p => readFileSync(join(root, p), 'utf8');
function method(source, signature) {
    const start = source.indexOf(signature); assert(start >= 0, signature);
    let end = source.indexOf('{', start) + 1, depth = 1;
    while (depth) { if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end; }
    return source.slice(start, end);
}
function runCpp(code) {
    const dir = mkdtempSync(join(tmpdir(), 'corum-lobby-lifecycle-'));
    try {
        const file = join(dir, 'test.cpp'), binary = join(dir, 'test');writeFileSync(file, code);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', file, '-o', binary], {stdio:'pipe'});
        execFileSync(binary, [], {stdio:'pipe'});
    } catch(error) { throw new Error(error.stderr?.toString() || error.message); }
    finally {rmSync(dir, {recursive:true, force:true});}
}

test('actual history rows keep Details visible and correctly indexed across 30-record scrolling', () => {
    const source=read('src/RankedPopup.cpp');
    const callbacks=['    void renderHistoryList(', '    void onHistoryDetail('].map(s=>method(source,s)).join('\n');
    runCpp(`
#include "${join(root,'src/domain/SurvivalHudPresentation.hpp')}"
#include <algorithm>
#include <cassert>
#include <functional>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
struct CCPoint{float x=0,y=0;};struct CCSize{float width=0,height=0;};
inline constexpr CCPoint CCPointZero{};
struct CCNode {
 virtual ~CCNode(){for(auto*child:children)delete child;}
 CCPoint position{},anchor{.5f,.5f};CCSize size{};float scale=1;bool visible=true,ignoreAnchor=false;int tag=-1;
 std::string id;std::vector<CCNode*>children;CCNode*parent=nullptr;CCNode*getParent(){return parent;}auto const&getChildrenExt()const{return children;}bool isRunning()const{return true;}
 CCPoint convertToNodeSpace(CCPoint p)const{for(auto*n=this;n;n=n->parent){p.x-=n->position.x-n->anchor.x*n->size.width;p.y-=n->position.y-n->anchor.y*n->size.height;}return p;}
 static CCNode*create(){return new CCNode;}
 void setPositionY(float v){position.y=v;}float getPositionY()const{return position.y;}
 void setVisible(bool v){visible=v;}void setPosition(CCPoint p){position=p;}void setAnchorPoint(CCPoint p){anchor=p;}
 void setContentSize(CCSize s){size=s;}CCSize getContentSize()const{return size;}
 void setScale(float s){scale=s;}void setID(std::string s){id=std::move(s);}
 void setTag(int t){tag=t;}int getTag()const{return tag;}
 void addChild(CCNode*n,int=0){n->parent=this;children.push_back(n);}
};
using CCObject=CCNode;
struct CCDirector {
 CCSize window{568,320};static CCDirector*sharedDirector(){static CCDirector d;return &d;}
 CCSize getWinSize()const{return window;}
};
struct CCMenu:CCNode {
 static CCMenu*create(){auto*m=new CCMenu;m->size=CCDirector::sharedDirector()->window;m->ignoreAnchor=true;return m;}
};
struct CCMenuItemSpriteExtra:CCNode {
 static CCMenuItemSpriteExtra*create(CCNode*face,void*,int){auto*b=new CCMenuItemSpriteExtra;b->size=face->size;b->addChild(face);return b;}
};
struct LoadingSpinner:CCNode {static LoadingSpinner*create(int){return new LoadingSpinner;}};
struct ScrollLayer:CCNode {
 CCNode*m_contentLayer=nullptr;static ScrollLayer*create(CCSize s){auto*l=new ScrollLayer;l->size=s;
  l->m_contentLayer=new CCNode;l->m_contentLayer->anchor=CCPointZero;l->addChild(l->m_contentLayer);return l;}
 void scrollToTop(){scrollTo(size.height-m_contentLayer->size.height);}
 void scrollTo(float offset){
  m_contentLayer->position.y=offset;
  // Geode v5.10.1 GenericContentLayer bounds/culling, sha 5a05bbad7d8719f26be49c9a13f9905f9111a35f:
  // loader/src/ui/nodes/ScrollLayer.cpp. This models SDK visibility, not device rendering.
  for(auto*n:m_contentLayer->children){auto y=offset+n->position.y;
   auto anchor=n->ignoreAnchor?CCPointZero:n->anchor;
   auto top=n->scale*(y+(1-anchor.y)*n->size.height*n->scale);
   auto bottom=n->scale*(y-n->anchor.y*n->size.height*n->scale);
   n->visible=top>0&&bottom<size.height;
  }
 }
};
#include "${root}/src/domain/RankedMode.hpp"
struct Stage {int stageIndex=1;std::vector<std::string>mapGrades;std::string stageGrade="A";double stageScore=300;};
struct Json {struct Int {long long unwrapOr(int v)const{return v;}bool isOk()const{return false;}long long unwrap()const{return 0;}};struct Bool{bool unwrapOr(bool b)const{return b;}};struct String{std::string unwrapOr(std::string s)const{return s;}};Bool asBool()const{return {};}String asString()const{return {};}Int asInt()const{return {};}Json operator[](char const*)const{return {};}};
namespace corum::ranked {struct HistoryRound{std::string label;bool completed=false;std::optional<bool>ownQualified;};std::vector<HistoryRound>historyRounds(Json const&,bool){return {};}}
struct History {Json record;bool inProgress=false,debug=false;int roundWinsA=0,roundWinsB=0;std::string matchId="m",createdAt="2026-10-24",finishedAt="2026-10-24";int survivalRank=0;corum::ranked::RankedMode mode=corum::ranked::RankedMode::SoloRanked;std::string side="A",winnerSide="A",opponentName="a long player name";std::optional<int>ownRankedScoreDelta=10;};
struct RuntimeView {std::uint64_t historyRevision=0;bool historyLoading=false;std::string historyError;struct{std::vector<Stage>stageResults;}placement;std::vector<History>history;};
struct RankedRuntime{RuntimeView data;static RankedRuntime&get(){static RankedRuntime r;return r;}RuntimeView const&view()const{return data;}void fetchHistoryDetail(corum::ranked::RankedMode,std::string const&){} };
namespace fmt {std::string runtime(std::string s){return s;}template<class...T>std::string format(std::string s,T const&...){return s;}}
namespace corum::ranked::i18n {bool isKorean(){return false;}}
constexpr int ccc3(unsigned char r,unsigned char g,unsigned char b){return (r<<16)|(g<<8)|b;}
constexpr int kGold=1,kCyan=2,kRed=3,kGreen=4,kDeepPanel2=5,kDeepPanel=6;
std::string operator""_spr(char const*s,std::size_t n){return {s,n};}
#define menu_selector(...) 0
CCNode*makeNeonPanel(CCSize s,CCPoint p,int,int,int){auto*n=new CCNode;n->size=s;n->position=p;return n;}
CCNode*makeLabel(std::string,float,CCPoint p,int,std::string=""){auto*n=new CCNode;n->size={120,12};n->position=p;return n;}
CCNode*makeActionButtonVisual(std::string){auto*n=new CCNode;n->size={104,34};return n;}
template<class T>struct WeakRef{T*p;explicit WeakRef(T*v):p(v){}struct Locked{T*p;explicit operator bool()const{return p;}T*operator->(){return p;}T*data(){return p;}};Locked lock()const{return {p};}};
void queueInMainThread(std::function<void()> fn){fn();}
template<class T>struct Number{T unwrapOr(T n)const{return n;}};template<class T>Number<T>numFromString(std::string const&){return {};}
namespace corum::ranked::ui {constexpr int TextMuted=9,Text=10;ScrollLayer*createRankedTapScroll(CCSize s,std::function<void(CCPoint,CCPoint)>){return ScrollLayer::create(s);}void fitMenuLabelWidth(CCNode*,float,float,float){}}
#include "${root}/src/domain/RankedMode.hpp"
struct CorumRankedLayer {
 corum::ranked::ModeLobbyTransition m_modeTransition;corum::ranked::HistoryScope m_historyScope=corum::ranked::HistoryScope::All;void renderHistoryTabs(){}
 std::string m_historyResult="ALL",m_historySelectedId,m_historyRound;int m_historyTabPage=0;std::optional<float> m_historyListOffset;ScrollLayer*m_historyListScroll=nullptr;
 int historyColor(History const&)const{return kCyan;}std::string historyOutcome(History const&)const{return "WIN";}std::string historyDelta(History const&)const{return "LP";}
 void historyText(CCNode*p,std::string const&t,float s,CCPoint pos,float,int color=0,bool=false,CCPoint anchor={.5f,.5f}){auto*n=makeLabel(t,s,pos,color);n->setAnchorPoint(anchor);p->addChild(n);}
 void historyPlayerName(CCNode*,corum::ranked::SurvivalStanding const&,CCPoint,float,int,std::string const&,bool){}
 bool onHistoryPlayerTap(CCPoint,CCPoint){return false;}
 enum class Page {HistoryList,HistoryDetail};Page m_page=Page::HistoryList;std::size_t m_historyIndex=0;CCNode*m_root=new CCNode;
 bool isRunning()const{return true;}~CorumRankedLayer(){delete m_root;}
 std::string tr(char const*,char const*english){return english;}
 void addTopBack(bool){}void addScreenHeader(std::string,std::string,int,int){}
 void addButton(std::string,CCPoint,int,bool,float){}
 ${callbacks}
};
CCNode*menuFor(ScrollLayer*l,int tag){
 for(auto*n:l->m_contentLayer->children)if(n->tag==tag)return n;
 return nullptr;
}
int main(){
 for(auto window:{CCSize{568,320},CCSize{480,320},CCSize{800,450}})for(int stages:{0,5}){
  CCDirector::sharedDirector()->window=window;CorumRankedLayer layer;RuntimeView view;
  view.history.resize(32);view.placement.stageResults.resize(stages);RankedRuntime::get().data=view;layer.renderHistoryList(view);
  ScrollLayer*scroll=nullptr;for(auto*n:layer.m_root->children)if(auto*s=dynamic_cast<ScrollLayer*>(n))scroll=s;
  assert(scroll&&scroll->id=="ranked-history-matches-scroll");
  assert(menuFor(scroll,0)&&menuFor(scroll,29)&&!menuFor(scroll,30));
  // Stage rows can precede the first match: scroll to that exact row and retain its own tag.
  for(int index:{0,14,29}){
   auto*menu=menuFor(scroll,index);assert(menu->size.height==112);
   scroll->scrollTo(scroll->size.height/2-menu->position.y);
   assert(menu->visible);
   assert(menu->position.y==scroll->m_contentLayer->size.height-116-index*122);
   layer.onHistoryDetail(menu);assert(layer.m_page==CorumRankedLayer::Page::HistoryDetail&&layer.m_historyIndex==std::size_t(index));
  }
  scroll->scrollToTop();if(stages==0)assert(menuFor(scroll,0)->visible);
  scroll->scrollTo(scroll->size.height/2-menuFor(scroll,29)->position.y);assert(menuFor(scroll,29)->visible&&!menuFor(scroll,0)->visible);
  // The old single screen-sized menu at y=0 is culled at the same long-list top.
  auto offset=scroll->size.height-scroll->m_contentLayer->size.height;
  assert(offset+window.height<0);
 }
}
`);
});

test('actual session pulse bounds retries, closes the exact token and rejects retired replies', () => {
    const source = read('src/RankedRuntime.cpp'), header = read('src/RankedRuntime.hpp');
    const fields = header.slice(header.indexOf('    geode::async::TaskHolder<geode::utils::web::WebResponse> m_survivalProfileRequest'), header.indexOf('    std::string m_matchToken;'));
    const methods = ['void RankedRuntime::leaveRanked()', 'void RankedRuntime::updateSessionHeartbeat()']
        .map(n => method(source, n)).join('\n').replaceAll('std::chrono::steady_clock::now()', 'Clock::now()');
    runCpp(`
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
struct Clock {static inline auto current=std::chrono::steady_clock::time_point(std::chrono::seconds(100));static auto now(){return current;}};
namespace matjson {struct Value {
 std::int64_t lease=20;Value operator[](char const*)const{return *this;}
 struct Int {std::int64_t value;std::int64_t unwrapOr(int)const{return value;}};
 Int asInt()const{return {lease};}
};}
namespace geode::utils::web {
struct WebResponse {int status=200;std::int64_t lease=20;int code()const{return status;}
 struct Json {std::int64_t lease;matjson::Value unwrapOr(matjson::Value)const{return {lease};}};Json json()const{return {lease};}};
struct Request {std::string token,url;};
struct Job {Request request;std::function<void(WebResponse)> callback;bool cancelled=false;};
inline std::vector<std::shared_ptr<Job>> jobs;
struct WebRequest {std::string token;void timeout(std::chrono::seconds){}Request post(std::string url)const{return {token,url};}};
}
namespace web=geode::utils::web;
namespace geode::async {template<class T> struct TaskHolder {
 std::shared_ptr<web::Job> job;void cancel(){if(job)job->cancelled=true;}
 template<class F> void spawn(web::Request r,F callback){job=std::make_shared<web::Job>(web::Job{r,callback,false});web::jobs.push_back(job);}
};}
web::WebRequest baseRequest(std::string token){return {token};}
bool successful(web::WebResponse const& r){return r.code()>=200&&r.code()<300;}
std::string responseError(web::WebResponse const&){return "expired";}
enum class RuntimeStage {Idle,Error};
struct RankedRuntime {
${fields}
 bool m_controlBusy=true;geode::async::TaskHolder<web::WebResponse> m_controlRequest,m_historyRequest;std::uint64_t m_historyGeneration=0;int pollRetirements=0;
 std::uint64_t m_playerInfoGeneration=0,m_forumRecordsGeneration=0;
 geode::async::TaskHolder<web::WebResponse> m_playerInfoRequest,m_forumRecordsRequest;
 std::uint64_t m_standingGeneration=0;bool m_standingBusy=false;
 geode::async::TaskHolder<web::WebResponse> m_standingRequest;
 struct {bool historyLoading=true;struct {struct {bool loading=true;}forum;}playerInfo;struct {bool loading=true;}forumRecords;}m_view;
 RuntimeStage stage=RuntimeStage::Idle;
 void invalidatePollRequest(){++pollRetirements;}
 void setStage(RuntimeStage value,std::string,std::string={}){stage=value;}
 std::string endpoint(std::string path)const{return path;}
 void cancelHistoryReads(){++m_historyGeneration;m_historyRequest.cancel();m_view.historyLoading=false;}void leaveRanked();void updateSessionHeartbeat();
 void applyAudioVolumes(matjson::Value const&){}
};
${methods}
void advance(int ms){Clock::current+=std::chrono::milliseconds(ms);}
RankedRuntime connected(){RankedRuntime r;r.m_rankedOpen=true;r.m_sessionToken="token-A";r.m_sessionLeaseDeadline=Clock::now()+std::chrono::seconds(20);return r;}
int main(){
 {auto r=connected();r.updateSessionHeartbeat();assert(web::jobs.back()->request.url=="/api/ranked/session/heartbeat");
  auto first=web::jobs.back();r.updateSessionHeartbeat();assert(web::jobs.size()==1);advance(2000);first->callback({200,20});
  assert(r.m_sessionLeaseDeadline==Clock::now()+std::chrono::seconds(18));advance(2999);r.updateSessionHeartbeat();assert(web::jobs.size()==1);
  advance(1);r.updateSessionHeartbeat();assert(web::jobs.size()==2);web::jobs.back()->callback({500,120});
  assert(r.m_sessionLeaseDeadline==std::chrono::steady_clock::time_point(std::chrono::seconds(120)));
  advance(15000);r.updateSessionHeartbeat();assert(r.stage==RuntimeStage::Error&&!r.m_rankedOpen&&r.m_sessionToken.empty());
  assert(web::jobs.back()->request.url=="/api/ranked/session/close"&&web::jobs.back()->request.token=="token-A");}
 // An old heartbeat success or rejection cannot affect a reopened session.
 {web::jobs.clear();auto r=connected();r.updateSessionHeartbeat();auto old=web::jobs.back();r.leaveRanked();assert(old->cancelled);
  auto close=web::jobs.back();assert(close->request.token=="token-A");r.m_rankedOpen=true;r.m_sessionRejected=false;r.m_sessionToken="token-B";
  r.m_sessionLeaseDeadline=Clock::now()+std::chrono::seconds(20);auto deadline=r.m_sessionLeaseDeadline;
  old->callback({200,120});old->callback({401,20});close->callback({200,20});
  assert(r.m_sessionToken=="token-B"&&r.m_rankedOpen&&r.m_sessionLeaseDeadline==deadline&&!r.m_sessionHeartbeatBusy);}
 for(int code:{401,403,426}){web::jobs.clear();auto r=connected();r.updateSessionHeartbeat();web::jobs.back()->callback({code,20});
  assert(r.stage==RuntimeStage::Error&&r.m_sessionRejected&&!r.m_rankedOpen);auto count=web::jobs.size();r.updateSessionHeartbeat();assert(web::jobs.size()==count);}
 {web::jobs.clear();auto r=connected();r.updateSessionHeartbeat();advance(2000);web::jobs.back()->callback({200,90});
  auto const deadline=r.m_sessionLeaseDeadline;assert(deadline==Clock::now()+std::chrono::seconds(88));
  advance(30000);r.updateSessionHeartbeat();web::jobs.back()->callback({500,120});
  assert(r.m_rankedOpen&&r.m_sessionLeaseDeadline==deadline);
  advance(57999);r.updateSessionHeartbeat();assert(r.m_rankedOpen);
  advance(1);r.updateSessionHeartbeat();assert(!r.m_rankedOpen&&r.stage==RuntimeStage::Error);}
 {web::jobs.clear();RankedRuntime r;r.updateSessionHeartbeat();assert(web::jobs.empty());r.leaveRanked();assert(web::jobs.empty());}
}
`);
    assert.match(read('src/RankedRoundPreparation.cpp'), /void RankedRoundPreparation::update\(float dt\) \{\s*RankedRuntime::get\(\)\.updateSessionHeartbeat/);
    assert.match(method(source, 'void RankedRuntime::begin()'), /m_sessionHeartbeatRequest\.cancel\(\); m_sessionHeartbeatBusy = false/);
});

test('actual Jukebox callback opens once, shows its timer, retires owned dialogs and leaves foreign modals alone', () => {
    const source = read('src/ui/RankedJukeboxPopup.hpp');
    const methods = ['    void disableCarrierMenus(', '    void watch(', '    void onOpen(', '    void closeOwnedDialogs()',
        '    void openWhenReady()', '    void setCountdown(', '    void dismiss()', '    void onExit()']
        .map(n => method(source, n)).join('\n');
    runCpp(`
#include <algorithm>
#include <cassert>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>
struct CCPoint {float x,y;};struct CCSize {float width,height;};
struct CCNode {
 virtual ~CCNode()=default;CCNode* parent=nullptr;bool live=true;std::vector<CCNode*> children;std::string id;int exits=0;
 virtual void onExit(){++exits;}CCNode* getParent()const{return parent;}auto const& getChildrenExt()const{return children;}
 void addChild(CCNode* n,int=0){children.push_back(n);n->parent=this;}void setID(std::string value){id=value;}
 void removeFromParentAndCleanup(bool){if(parent){auto* p=parent;parent=nullptr;std::erase(p->children,this);onExit();}}
 void unschedule(int){}void retain(){}void release(){}
};
using CCObject=CCNode;struct CCScene:CCNode {};struct CCMenu:CCNode {void setTouchEnabled(bool){}};
struct FLAlertLayer:CCNode {int backs=0;virtual void keyBackClicked(){++backs;removeFromParentAndCleanup(true);}};
struct Popup:FLAlertLayer {void onClose(CCObject*){removeFromParentAndCleanup(true);}void onExit()override{CCNode::onExit();}};
struct CCLabelBMFont:CCNode {std::string text;void setString(char const* value){text=value;}};
struct CCMenuItem:CCNode {CCObject* m_pListener=nullptr;bool m_pfnSelector=true,enabled=true;std::function<void()> callback;
 bool isEnabled()const{return enabled;}void activate(){callback();}};
struct CustomSongWidget:CCNode {void* m_songInfoObject=this;CCMenuItem* opener=nullptr;
 CCNode* getChildByIDRecursive(char const*){return opener;}};
template<class T>T typeinfo_cast(CCNode* p){return dynamic_cast<T>(p);}
template<class T>struct Ref {T* p=nullptr;Ref()=default;Ref(T* value):p(value){}T* data()const{return p;}T* operator->()const{return p;}
 explicit operator bool()const{return p;}Ref& operator=(T* value){p=value;return *this;}};
template<class T>struct WeakRef {T* p=nullptr;WeakRef(T* value):p(value){}Ref<T> lock()const{return p&&p->live?p:nullptr;}};
struct Loader {bool loaded=true;static Loader* get(){static Loader l;return &l;}bool isModLoaded(char const*)const{return loaded;}};
struct CCDirector {CCScene* scene=nullptr;static CCDirector* sharedDirector(){static CCDirector d;return &d;}
 CCScene* getRunningScene(){return scene;}CCSize getWinSize()const{return {800,450};}};
struct CCTouchDispatcher {std::vector<CCNode*> retired;static CCTouchDispatcher* get(){static CCTouchDispatcher d;return &d;}
 void unregisterForcePrio(CCNode* n){retired.push_back(n);}};
std::string operator""_spr(char const* p,std::size_t size){return {p,size};}
#define schedule_selector(...) 0
using LevelPreparationContext=int;int context=1;int currentPreparationContext(){return context;}
enum class FontWeight {ExtraBold};constexpr int Cyan=1,Surface=2,Gold=3;
CCNode* surface(CCSize,CCPoint,int,int,int,bool){return new CCNode;}
CCLabelBMFont* menuLabel(std::string text,float,CCPoint,int,FontWeight){auto* n=new CCLabelBMFont;n->text=text;return n;}
void fitMenuLabelWidth(CCLabelBMFont*,float,float,float){}
${method(source, 'inline bool rankedJukeboxLoaded()')}
${method(source, 'inline CCMenuItem* rankedJukeboxOpener(')}
struct RankedJukeboxPopup:Popup {
 CustomSongWidget* m_widget=nullptr;Ref<CCNode> m_countdown;CCLabelBMFont* m_countdownLabel=nullptr;
 int m_context=1;bool m_closed=false,m_exited=false,m_openRequested=false,m_opened=false;
 std::vector<WeakRef<FLAlertLayer>> m_ownedDialogs;
${methods}
};
int main(){CCScene scene;CCDirector::sharedDirector()->scene=&scene;
 FLAlertLayer foreign;scene.addChild(&foreign);CustomSongWidget widget;CCMenuItem opener;opener.m_pListener=&widget;widget.opener=&opener;
 int opens=0;FLAlertLayer own;opener.callback=[&]{++opens;scene.addChild(&own);};
 {RankedJukeboxPopup carrier;carrier.m_widget=&widget;scene.addChild(&carrier);opener.enabled=false;
  carrier.openWhenReady();assert(opens==0);opener.enabled=true;carrier.watch(0);assert(opens==1&&carrier.m_countdown);
  carrier.setCountdown("STARTS IN 03");assert(carrier.m_countdownLabel->text=="STARTS IN 03");carrier.watch(0);assert(opens==1);
  carrier.dismiss();assert(own.backs==1&&!own.getParent()&&foreign.getParent()==&scene&&foreign.backs==0);
  assert(!carrier.m_countdown&&!carrier.getParent()&&carrier.m_closed);auto retired=CCTouchDispatcher::get()->retired.size();
  carrier.dismiss();carrier.onExit();assert(own.backs==1&&CCTouchDispatcher::get()->retired.size()==retired);}
 // Closing the native Jukebox with its X retires the hidden carrier/timer too.
 {RankedJukeboxPopup carrier;carrier.m_widget=&widget;scene.addChild(&carrier);carrier.openWhenReady();
  own.keyBackClicked();int before=own.backs;carrier.watch(0);assert(carrier.m_closed&&!carrier.m_countdown&&own.backs==before);}
 // No popup or a changed context in the synchronous opener cannot leave an
 // invisible modal registered or cause repeated activation in later watches.
 {RankedJukeboxPopup carrier;carrier.m_widget=&widget;scene.addChild(&carrier);opener.callback=[&]{++opens;};
  int before=opens;carrier.openWhenReady();carrier.watch(0);carrier.watch(0);assert(opens==before+1&&carrier.m_closed);}
 {RankedJukeboxPopup carrier;carrier.m_widget=&widget;scene.addChild(&carrier);opener.callback=[&]{scene.addChild(&own);context=2;};
  carrier.openWhenReady();assert(!own.getParent()&&!carrier.m_countdown);carrier.watch(0);assert(carrier.m_closed);}
 assert(foreign.getParent()==&scene&&foreign.backs==0);
}
`);
    assert.doesNotMatch(source, /Open Jukebox|m_open->|m_status->/);
    const screen = read('src/RankedPopup.cpp');
    const enter = method(screen, '    void maybeEnterLevel()');
    assert(enter.indexOf('popup->dismiss()') < enter.indexOf('.launch('));
    assert.match(method(screen, '    void onJukebox('), /popup->show\(\); popup->openWhenReady\(\)/);
    assert.match(method(screen, '    void updateDynamicUi()'), /popup->setCountdown\(text\)/);
});

test('actual Result ESC dispatch stays in Ranked; claimable card ordering retains original callback indices', () => {
    const source = read('src/RankedPopup.cpp');
    runCpp(`
#include "${join(root, 'src/domain/AchievementPresentation.hpp')}"
#include <cassert>
using CCObject=void;enum class RuntimeStage {Ready,Matched};
struct RuntimeView {RuntimeStage stage=RuntimeStage::Matched;struct Match {std::string state="MATCH_RESULT";}match;};
struct RankedRuntime {RuntimeView state;int dismissals=0,fetches=0;bool acknowledgementPending=false;
 static RankedRuntime& get(){static RankedRuntime r;return r;}RuntimeView const& view()const{return state;}
 void dismissMatch(){++dismissals;if(!acknowledgementPending)state.stage=RuntimeStage::Ready;}void fetchAchievements(){++fetches;}};
#include "${root}/src/domain/RankedMode.hpp"
struct Layer {
 corum::ranked::ModeLobbyTransition m_modeTransition;
 enum class Page {Live,History};Page m_page=Page::Live;
 bool m_rankTransitionLayer=false,m_resultScoreLayer=false,m_resultScorePending=false,m_modeNoticeOpen=false;std::string m_localMessage;
 int noticeCloses=0,inputSyncs=0;void closeModeNotice(){m_modeNoticeOpen=false;++noticeCloses;}void syncLobbyInput(){++inputSyncs;}
 int exits=0,historyBacks=0;void onCloseLayer(CCObject*){++exits;}void onHistoryBack(CCObject*){++historyBacks;m_page=Page::Live;}
${method(source, '    void onResultClose(')}
${method(source, '    void keyBackClicked()').replace(' override', '')}
};
int main(){auto& runtime=RankedRuntime::get();
 for(auto terminal:{"MATCH_RESULT","PLACEMENT_RESULT","CANCELLED"}){Layer layer;runtime.state={RuntimeStage::Matched,{terminal}};
  int before=runtime.dismissals;layer.keyBackClicked();assert(layer.exits==0&&runtime.dismissals==before+1&&runtime.state.stage==RuntimeStage::Ready);}
 {Layer layer;runtime.state={RuntimeStage::Matched,{"PLACEMENT_RESULT"}};runtime.acknowledgementPending=true;
  layer.keyBackClicked();assert(layer.exits==0&&runtime.state.stage==RuntimeStage::Matched);runtime.acknowledgementPending=false;}
 {Layer layer;runtime.state={RuntimeStage::Matched,{"ROUND_PLAYING"}};layer.keyBackClicked();assert(layer.exits==0&&!layer.m_localMessage.empty());}
 {Layer layer;runtime.state.stage=RuntimeStage::Ready;layer.keyBackClicked();assert(layer.exits==1);}
 {Layer layer;layer.m_modeNoticeOpen=true;layer.keyBackClicked();assert(layer.exits==0&&layer.noticeCloses==1&&layer.inputSyncs==1);}
 {Layer layer;layer.m_page=Layer::Page::History;layer.keyBackClicked();assert(layer.historyBacks==1&&layer.exits==0);}
 {Layer layer;layer.m_rankTransitionLayer=true;layer.keyBackClicked();assert(layer.exits==0);}
 {Layer layer;layer.m_resultScoreLayer=true;layer.keyBackClicked();assert(layer.exits==0);}
 {Layer layer;layer.m_resultScorePending=true;layer.keyBackClicked();assert(layer.exits==0);}
 struct Card {std::string id;int claimableRewards;};std::vector<Card> cards={{"a",0},{"b",2},{"c",0},{"d",1},{"e",0},{"f",3}};
 auto order=corum::ranked::achievementDisplayOrder(cards);assert((order==std::vector<std::size_t>{1,3,5,0,2,4}));
 assert(cards[order[0]].id=="b"&&cards[order[3]].id=="a");cards[1].claimableRewards=0;
 assert((corum::ranked::achievementDisplayOrder(cards)==std::vector<std::size_t>{3,5,0,1,2,4}));
 std::vector<Card> empty;assert(corum::ranked::achievementDisplayOrder(empty).empty());
}
`);
    const cards = method(source, '    void renderAchievements(');
    assert.match(cards, /auto const index = order\[start \+ slot\]/);
    assert.match(cards, /menu_selector\(CorumRankedLayer::onAchievementClaim\),\s*claimState == corum::ranked::AchievementClaimState::Unclaimed/);
});
