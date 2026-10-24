// Real runtime methods at controlled Cocos/HTTP boundaries. Device teardown,
// native record saving and FMOD playback still require a GD device.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
function method(source,name){let start=source.indexOf(name);assert(start>=0,name);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');end++;}return source.slice(start,end).replaceAll('std::chrono::steady_clock::now()','testNow()');}
function run(code){const dir=mkdtempSync(join(tmpdir(),'corum-free-play-'));try{
 writeFileSync(join(dir,'test.cpp'),code);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}
const viewer=read('tests/SpectatorLiveViewerNativeTest.mjs');
const json=viewer.slice(viewer.indexOf('namespace matjson {'),viewer.indexOf('${readFileSync(join(root,'))
 .replace('bool isObject()const', 'void erase(std::string const& key){if(auto p=std::get_if<Object>(&data))p->erase(key);}bool isObject()const');
const preamble=`
#include <algorithm>
#include <cassert>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>
${json}
auto wall=std::chrono::steady_clock::time_point{};auto testNow(){return wall;}
`;

test('100% needs a terminal server ACK; Free Play returns once from menus/native play and defers scene transitions',()=>{
 const source=read('src/SurvivalRuntime.cpp');
 run(preamble+`
#include "domain/SurvivalFreePlayPolicy.hpp"
#include "domain/SurvivalHudPresentation.hpp"
template<class T>struct Ref{T* p=nullptr;Ref()=default;Ref(T* p):p(p){}T* data()const{return p;}T* operator->()const{return p;}explicit operator bool()const{return p;}};
template<class T>struct WeakRef{T* p;explicit WeakRef(T* p):p(p){}Ref<T> lock()const{return {p};}};
struct CCNode{virtual ~CCNode()=default;CCNode* parent=nullptr;bool running=true;bool coordinator=false;bool isRunning()const{return running;}CCNode* getParent()const{return parent;}CCNode* getChildByID(std::string const&){return coordinator?this:nullptr;}};
struct CCScene:CCNode{};struct CCTransitionScene:CCScene{};
template<class T> T typeinfo_cast(CCNode* node){return dynamic_cast<T>(node);}
struct CCDirector{std::vector<CCScene*>stack;CCScene* next=nullptr;int pops=0;static CCDirector* sharedDirector(){static CCDirector d;return &d;}CCScene* getRunningScene(){return stack.empty()?nullptr:stack.back();}CCScene* getNextScene(){return next;}void pushScene(CCScene* s){stack.push_back(s);}int levelForSceneInStack(CCScene* s){auto it=std::find(stack.begin(),stack.end(),s);return it==stack.end()?-1:it-stack.begin();}void popToSceneInStack(CCScene* s){++pops;stack.resize(levelForSceneInStack(s)+1);}};
struct MenuLayer{static CCScene* scene(bool){static CCScene menu;return &menu;}};
struct GJGameLevel{};struct PlayLayer:CCNode{static inline PlayLayer* current=nullptr;static PlayLayer* get(){return current;}};
std::vector<std::function<void()>> queued;void queueInMainThread(std::function<void()> fn){queued.push_back(std::move(fn));}void drain(){auto tasks=std::exchange(queued,{});for(auto& fn:tasks)fn();}
constexpr char const* operator""_spr(char const* s,std::size_t){return s;}
namespace corum::ranked {
int returnRequests=0,fallbacks=0;
bool requestSurvivalGameplayReturn(PlayLayer*){++returnRequests;return true;}void showSurvivalGame(){++fallbacks;}
struct RankedAudioManager{int restored=0;static RankedAudioManager&get(){static RankedAudioManager r;return r;}void restoreMenuMusic(){++restored;}};
struct RankedRuntime{std::string token="session";static RankedRuntime&get(){static RankedRuntime r;return r;}std::string spectatorLiveSessionToken()const{return token;}};
struct SurvivalRuntime{
 struct View{std::string id="match",roundKey="r1",phase="ROUND_1",state="PLAYING",ownId="human",activeAttemptId;int mapGeneration=1;bool ownEliminated=false;std::vector<SurvivalStanding> standings;matjson::Value completedRound;}m_view;
 std::string m_session="session";SurvivalFreePlayState m_freePlay;std::optional<WeakRef<CCScene>>m_freePlayOrigin,m_freePlayQuitScene;Ref<GJGameLevel>m_freePlayQuitLevel;bool m_freePlayReturnQueued=false,m_freePlayNativeQuitFinished=false;
 bool active()const{return !m_view.id.empty();}bool freePlaying()const{return m_freePlay.active;}
 bool normalRoundCleared()const;bool canFreePlay()const;bool beginFreePlay();bool freePlayReturnPending()const;bool beginFreePlayNativeQuit(GJGameLevel*);bool interceptFreePlayNativeQuit(GJGameLevel*)const;void finishFreePlayNativeQuit();void pumpFreePlayReturn();
};
${['bool SurvivalRuntime::normalRoundCleared()', 'bool SurvivalRuntime::canFreePlay()', 'bool SurvivalRuntime::beginFreePlay()', 'bool SurvivalRuntime::freePlayReturnPending()', 'bool SurvivalRuntime::beginFreePlayNativeQuit(', 'bool SurvivalRuntime::interceptFreePlayNativeQuit(', 'void SurvivalRuntime::finishFreePlayNativeQuit()', 'void SurvivalRuntime::pumpFreePlayReturn()'].map(n=>method(source,n)).join('\n')}
}
int main(){using namespace corum::ranked;SurvivalRuntime r;CCScene origin;origin.coordinator=true;auto* d=CCDirector::sharedDirector();d->stack={&origin};
 SurvivalStanding own;own.id="human";own.score=own.bestProgress=100;r.m_view.standings={own};assert(!r.canFreePlay());
 r.m_view.standings[0].cleared=true;r.m_view.standings[0].attemptActive=true;assert(!r.canFreePlay());r.m_view.standings[0].attemptActive=false;r.m_view.activeAttemptId="a";assert(!r.canFreePlay());r.m_view.activeAttemptId.clear();
 r.m_view.phase="TIEBREAK";assert(!r.canFreePlay());r.m_view.phase="ROUND_1";assert(r.canFreePlay());
 assert(r.beginFreePlay());assert(!r.beginFreePlay());drain();assert(d->stack.size()==2&&RankedAudioManager::get().restored==1);
 wall+=std::chrono::hours(1);r.pumpFreePlayReturn();assert(queued.empty()&&!r.freePlayReturnPending()); // A local timeout grants no authority.
 CCScene ordinary;PlayLayer play;play.running=false;play.parent=&ordinary;PlayLayer::current=&play;d->stack.push_back(&ordinary);r.m_view.state="ROUND_RESULT";
 d->next=&ordinary;r.pumpFreePlayReturn();assert(queued.empty());d->next=nullptr;r.pumpFreePlayReturn();r.pumpFreePlayReturn();assert(queued.size()==1);drain();assert(returnRequests==1&&r.freePlaying());
 GJGameLevel level,other;assert(r.beginFreePlayNativeQuit(&level));assert(r.interceptFreePlayNativeQuit(&level)&&!r.interceptFreePlayNativeQuit(&other));r.finishFreePlayNativeQuit();drain();assert(!r.freePlaying()&&d->getRunningScene()==&origin&&d->pops==1);
 r.pumpFreePlayReturn();assert(queued.empty());PlayLayer::current=nullptr;r.m_view.state="PLAYING";assert(r.beginFreePlay());drain();r.m_view.roundKey="r2";r.m_view.mapGeneration=2;assert(r.freePlayReturnPending());r.pumpFreePlayReturn();drain();assert(d->getRunningScene()==&origin&&d->pops==2);
 // A newly observed completed round also returns from a LevelInfo/menu scene.
 assert(r.beginFreePlay());drain();r.m_view.completedRound["key"]="completed-r2";r.pumpFreePlayReturn();drain();assert(!r.freePlaying()&&d->pops==3&&fallbacks==0);
}
`);
});

test('Force Clear keeps exact retries, waits behind regular events and rejects stale round callbacks',()=>{
 const source=read('src/SurvivalRuntime.cpp');
 run(preamble+`
namespace geode::log{template<class...T>void info(T const&...){}}
namespace log=geode::log;
namespace utils::random{std::string generateUUID(){static int n=0;return "request-"+std::to_string(++n);}}
namespace web{struct WebResponse{int status;matjson::Value root;int code()const{return status;}matjson::Result<matjson::Value>json()const{return {root};}};struct Operation{matjson::Value body;std::string url;};struct WebRequest{matjson::Value body;void bodyJSON(matjson::Value v){body=v;}Operation post(std::string const& url){return {body,url};}};struct Pending{Operation op;std::function<void(WebResponse)>done;};std::vector<Pending>pending;}
web::WebRequest request(std::string const&){return {};}
struct Request{int cancellations=0;void cancel(){++cancellations;}void spawn(web::Operation op,std::function<void(web::WebResponse)> fn){web::pending.push_back({op,fn});}};
namespace corum::ranked{
struct RankedRuntime{static auto&get(){static RankedRuntime r;return r;}std::string spectatorLiveSessionToken()const{return "session";}};
struct SurvivalRuntime{
 struct View{double activeAttemptProgress=38;std::string id="match",roundKey="r1",state="PLAYING",phase="ROUND_1",error;int mapGeneration=1;bool debug=true,online=false,ownEliminated=false,ownAssetsReady=true,ownEntered=true;}m_view;
 bool local=false;int ends=0;long remainingMs()const{return 180000;}bool freePlaying()const{return local;}void endAttempt(double p){assert(p==38);++ends;m_events.push_back(event("ATTEMPT_END"));}
 bool cleared=false,m_forceClearBusy=false,m_eventBusy=false;std::uint64_t m_generation=1;std::string m_session="session",m_attempt="attempt";std::optional<matjson::Value> m_forceClearBody;std::deque<matjson::Value>m_events;std::chrono::steady_clock::time_point m_forceClearRetryAt{},m_nextPoll{};Request m_forceClearRequest;
 bool normalRoundCleared()const{return cleared;}bool canPlay()const{return true;}bool active()const{return !m_view.id.empty();}bool current(std::uint64_t gen,std::string const& session){return gen==m_generation&&session==m_session;}
 bool forceClearPending()const{return m_forceClearBody.has_value();}bool surrenderPending()const{return false;}std::string debugEndpoint(std::string const& p){return p;}matjson::Value event(std::string type){matjson::Value v;v["type"]=type;v["roundKey"]=m_view.roundKey;v["mapGeneration"]=m_view.mapGeneration;return v;}void observe(matjson::Value const& v){cleared=v["cleared"].asBool().unwrapOr(false);auto state=v["state"].asString().unwrapOr("");if(!state.empty())m_view.state=state;}
 bool canForceClear()const;void forceClear();void pumpForceClear();bool canEndRound()const;void endRound();
};
${['bool SurvivalRuntime::canForceClear()', 'void SurvivalRuntime::forceClear()', 'void SurvivalRuntime::pumpForceClear()', 'bool SurvivalRuntime::canEndRound()', 'void SurvivalRuntime::endRound()'].map(n=>method(source,n)).join('\n')}
}
int main(){using namespace corum::ranked;SurvivalRuntime r;
 r.m_view.online=true;assert(!r.canForceClear());r.m_view.online=false;r.m_view.phase="TIEBREAK";assert(!r.canForceClear());r.m_view.phase="ROUND_1";
 r.m_eventBusy=true;r.forceClear();assert(r.forceClearPending()&&web::pending.empty());r.forceClear();r.m_eventBusy=false;r.pumpForceClear();assert(web::pending.size()==1);
 auto id=web::pending[0].op.body["requestId"].asString().unwrapOr("");web::pending[0].done({500,{}});r.pumpForceClear();assert(web::pending.size()==1);wall+=std::chrono::seconds(1);r.pumpForceClear();assert(web::pending.size()==2&&web::pending[1].op.body["requestId"].asString().unwrapOr("")==id);
 auto delayed=web::pending[1].done;r.m_view.roundKey="r2";r.m_attempt="new-round";matjson::Value accepted;accepted["cleared"]=true;delayed({201,accepted});assert(!r.cleared&&r.m_attempt=="new-round");r.pumpForceClear();assert(!r.forceClearPending());
 r.forceClear();web::pending.back().done({409,{}});assert(!r.forceClearPending()&&r.m_view.error=="survival-force-clear-rejected");
 r.forceClear();web::pending.back().done({201,{}});assert(!r.cleared&&r.forceClearPending()&&r.m_attempt=="new-round");wall+=std::chrono::seconds(1);r.pumpForceClear();web::pending.back().done({201,accepted});assert(r.cleared&&!r.forceClearPending()&&r.m_attempt.empty());
 r.cleared=false;r.m_attempt="a";r.forceClear();r.cleared=true;r.pumpForceClear();assert(!r.forceClearPending()&&r.m_attempt.empty()); // Poll recovers a lost success response.
 // END ROUND shares the retry/ACK boundary but never fabricates a clear.
 r.local=true;assert(!r.canEndRound());r.local=false;r.m_view.online=true;assert(!r.canEndRound());r.m_view.online=false;
 r.m_view.ownAssetsReady=false;assert(!r.canEndRound());r.m_view.ownAssetsReady=true;r.m_view.phase="TIEBREAK";assert(r.canEndRound());
 r.cleared=false;r.m_attempt="last-attempt";auto count=web::pending.size();r.endRound();r.endRound();assert(r.ends==1&&r.forceClearPending()&&r.m_events.size()==1&&web::pending.size()==count);
 assert(r.m_events.front()["type"].asString().unwrapOr("")=="ATTEMPT_END");r.m_events.clear();r.pumpForceClear();assert(web::pending.size()==count+1);
 assert(web::pending.back().op.url=="/matches/match/end-round");assert(web::pending.back().op.body["type"].asString().unwrapOr("").empty());
 auto requestId=web::pending.back().op.body["requestId"].asString().unwrapOr("");web::pending.back().done({500,{}});assert(r.forceClearPending()&&r.m_attempt=="last-attempt"&&!r.cleared);
 wall+=std::chrono::seconds(1);r.pumpForceClear();assert(web::pending.back().op.body["requestId"].asString().unwrapOr("")==requestId);
 matjson::Value ended;ended["state"]="ROUND_RESULT";web::pending.back().done({201,ended});assert(!r.forceClearPending()&&r.m_attempt.empty()&&!r.cleared);

}
`);
});

test('NOW telemetry can decrease and coalesce without lowering the scoring payload or submitting Free Play progress',()=>{
 const source=read('src/SurvivalRuntime.cpp');
 run(preamble+`
#include <cmath>
namespace corum::ranked{
struct SurvivalRuntime{
 bool playing=true;std::string m_attempt="attempt";double m_attemptBest=0,m_lastProgress=-1;std::chrono::steady_clock::time_point m_nextProgress{};std::deque<matjson::Value>m_events;
 bool canPlay()const{return playing;}matjson::Value event(std::string type){matjson::Value v;v["type"]=type;return v;}void enqueue(matjson::Value v){m_events.push_back(v);}void progress(double);
};
${method(source,'void SurvivalRuntime::progress(')}
}
int main(){using corum::ranked::SurvivalRuntime;SurvivalRuntime r;r.progress(87.9);assert(r.m_events.size()==1);wall+=std::chrono::milliseconds(250);r.progress(40.2);assert(r.m_events.size()==2);
 auto const& p=r.m_events.back();assert(p["progress"].asDouble().unwrapOr(0)==87.9&&p["currentProgress"].asDouble().unwrapOr(0)==40.2);
 wall+=std::chrono::milliseconds(250);r.progress(30.1);assert(r.m_events.size()==2&&r.m_events.back()["progress"].asDouble().unwrapOr(0)==87.9&&r.m_events.back()["currentProgress"].asDouble().unwrapOr(0)==30.1);
 r.playing=false;wall+=std::chrono::seconds(1);r.progress(100);assert(r.m_events.size()==2&&r.m_attemptBest==87.9);
}
`);
});

test('live cards retain identity, retarget from the current position and recolor elimination independently of movement',()=>{
 const source=read('src/ui/SurvivalGameLayer.cpp');
 run(preamble+`
#include <cmath>
#include <array>
#include <unordered_map>
#include "domain/SurvivalBoardPresentation.hpp"
#include "domain/SurvivalHudPresentation.hpp"
#include "domain/ScoreDisplay.hpp"
struct CCPoint{float x=0,y=0;bool operator==(CCPoint const&)const=default;};struct CCSize{float width,height;};struct Color{unsigned char r,g,b;};using ccColor3B=Color;Color ccc3(unsigned char r,unsigned char g,unsigned char b){return {r,g,b};}
struct CCAction{CCPoint from,to;int tag=0;void setTag(int n){tag=n;}};
struct CCMoveTo{static CCAction* create(float,CCPoint point){auto* a=new CCAction;a->to=point;return a;}};struct CCEaseSineOut{static CCAction* create(CCAction* a){return a;}};
struct CCNode{CCPoint pos;CCSize size{100,50};bool visible=true;CCAction* action=nullptr;int moves=0,stops=0;std::vector<CCNode*>children;virtual ~CCNode()=default;static CCNode* create(){return new CCNode;}void setPosition(CCPoint p){pos=p;}CCPoint getPosition()const{return pos;}void setScale(float){}void setContentSize(CCSize value){size=value;}void setVisible(bool b){visible=b;}bool isVisible()const{return visible;}void addChild(CCNode* c,int=0){children.push_back(c);}void removeFromParentAndCleanup(bool){}void removeAllChildrenWithCleanup(bool){children.clear();}void stopActionByTag(int){action=nullptr;++stops;}void runAction(CCAction* a){a->from=pos;action=a;++moves;}void advance(float t){assert(action);pos={action->from.x+(action->to.x-action->from.x)*t,action->from.y+(action->to.y-action->from.y)*t};}};
using CCObject=CCNode;
const CCPoint CCPointZero{0,0};
struct CCMenu:CCNode{static CCMenu* create(){return new CCMenu;}};
struct CCMenuItemSpriteExtra:CCNode{int tag=0;bool enabled=true;void setEnabled(bool value){enabled=value;}static CCMenuItemSpriteExtra* create(CCNode*,CCObject*,int){return new CCMenuItemSpriteExtra;}void setTag(int n){tag=n;}int getTag()const{return tag;}};
#define menu_selector(...) 0
struct CCLabelBMFont:CCNode{std::string text;int writes=0;void setString(char const* s){text=s;++writes;}void setColor(Color){}};
namespace fmt{std::string runtime(std::string s){return s;}template<class...T>std::string format(std::string text,T const&...values){auto put=[&](auto const& v){std::ostringstream out;out<<v;auto at=text.find('{');if(at!=std::string::npos){auto end=text.find('}',at);text.replace(at,end-at+1,out.str());}};(put(values),...);return text;}}
template<class T>struct Number{T unwrapOr(T v){return v;}};template<class T>Number<T>numFromString(std::string const&){return {};}
namespace corum::ranked{
float rankedMenuTextScale(float scale){return scale;}
namespace i18n{bool isKorean(){return false;}std::string text(char const*,char const* value){return value;}}std::string text(char const*,char const* s){return s;}
namespace ui{inline constexpr Color Gold{255,195,55},Blue{0,0,255},SurfaceRaised{0,0,0},Cyan{0,255,255},Text{255,255,255},TextMuted{160,160,160},Red{255,0,0};enum class FontWeight{Black,ExtraBold};enum class RankBadgeDisplay{Leaderboard};
 CCNode* surface(CCSize size,CCPoint pos,Color,Color,unsigned char,bool){auto* node=new CCNode;node->size=size;node->pos=pos;return node;}
 CCLabelBMFont* menuLabel(std::string const& text,float,CCPoint pos,Color,FontWeight){auto* label=new CCLabelBMFont;label->text=text;label->pos=pos;return label;}CCLabelBMFont* playerNameLabel(std::string const& text,float scale,CCPoint pos,Color color,FontWeight weight){return menuLabel(text,scale,pos,color,weight);}void fitMenuLabelWidth(CCLabelBMFont*,float,float,float){}void fitFullMenuLabel(CCLabelBMFont*,float,float,float){}
 struct PlayerIcon{static CCNode* createLocal(){return CCNode::create();}static CCNode* createBot(int){return CCNode::create();}static CCNode* createRemote(int){return CCNode::create();}};
 CCNode* createAnimatedRankBadge(std::string const&,CCSize,RankBadgeDisplay){return CCNode::create();}
}
struct SurvivalMatchView{std::uint64_t revision=1;std::string id="m",roundKey="r1",ownId="a",phase="ROUND_1";int mapGeneration=1,matchPlayerCount=16;std::vector<SurvivalStanding>standings,roundPlayers;matjson::Value result;};
struct Coordinator:CCNode{
 struct Tile{CCNode* root=nullptr;CCLabelBMFont* name=nullptr;CCLabelBMFont* record=nullptr;CCPoint destination;CCNode* icon=nullptr;CCNode* tierBadge=nullptr;std::string tier="";CCNode* eliminatedBackground=nullptr;std::string titleText={},recordText={};std::array<CCNode*,3>medals{};int infoTag=0;SurvivalProgressTween progress{};};
 double m_frameDelta=1./60.;std::string m_gridContext;CCNode grid;CCNode* m_grid=&grid;CCNode pager;CCNode* m_pager=&pager;CCLabelBMFont pageLabel;CCLabelBMFont* m_pageLabel=&pageLabel;std::size_t m_cardPage=0,m_cardPages=1;std::unordered_map<std::string,Tile>m_tiles;std::unordered_map<int,SurvivalStanding>m_infoByTag;int m_nextInfoTag=1000;void onPlayerInfo(CCObject*){}CCSize getContentSize()const{return {568,320};}
 ${method(source,'void showParticipants(')}
};
}
int main(){using namespace corum::ranked;SurvivalMatchView view;SurvivalStanding a,b;a.id="a";a.name="Alice";a.rank=1;a.score=a.bestProgress=80;a.passing=true;b.id="b";b.name="Bob";b.rank=2;b.score=b.bestProgress=60;b.passing=true;view.standings={a,b};Coordinator ui;ui.showParticipants(view,false);
 auto* alice=ui.m_tiles.at("a").root;auto* name=ui.m_tiles.at("a").name;auto* icon=ui.m_tiles.at("a").icon;auto initial=alice->pos;
 assert(ui.m_tiles.at("a").medals[0]->visible&&ui.m_tiles.at("b").medals[1]->visible);
 for(int rank:{0,4,3,2,1}){view.standings[0].rank=rank;ui.showParticipants(view,false);auto const& tile=ui.m_tiles.at("a");
  for(int n=1;n<=3;n++)assert(tile.medals[n-1]->visible==(n==rank));
 }
 // Predicted elimination before the round ends must already be red.
 view.standings[0].rank=4;view.standings[0].passing=false;ui.showParticipants(view,false);
 assert(ui.m_tiles.at("a").eliminatedBackground->visible);
 view.standings[0].rank=1;view.standings[0].passing=true;ui.showParticipants(view,false);
 assert(!ui.m_tiles.at("a").eliminatedBackground->visible);
 view.standings[0].score=81;ui.showParticipants(view,false);assert(alice->moves==0&&name==ui.m_tiles.at("a").name&&icon==ui.m_tiles.at("a").icon);
 std::swap(view.standings[0],view.standings[1]);view.standings[0].rank=1;view.standings[1].rank=2;ui.showParticipants(view,false);assert(alice->moves==1);alice->advance(.5f);auto halfway=alice->pos;assert(halfway!=initial);
 std::swap(view.standings[0],view.standings[1]);view.standings[0].rank=1;view.standings[1].rank=2;view.standings[0].eliminated=true;ui.showParticipants(view,false);
 assert(alice==ui.m_tiles.at("a").root&&alice->moves==2&&alice->stops==2&&alice->pos==halfway&&alice->action->from==halfway&&alice->action->to==initial);assert(ui.m_tiles.at("a").eliminatedBackground->visible);for(auto* medal:ui.m_tiles.at("a").medals)assert(!medal->visible);
 auto moves=alice->moves;view.standings[0].currentProgress=41;view.standings[0].attemptActive=true;ui.showParticipants(view,false);assert(alice->moves==moves);assert(ui.m_tiles.at("a").record->text.find("ELIMINATED")!=std::string::npos);
 view.standings[0].eliminated=false;view.standings[0].rank=3;view.standings[1].rank=3;ui.showParticipants(view,false);
 assert(ui.m_tiles.at("a").medals[2]->visible&&ui.m_tiles.at("b").medals[2]->visible);
 assert(!ui.m_tiles.at("a").eliminatedBackground->visible);
 matjson::Value::Array podium;for(int rank=1;rank<=3;++rank){matjson::Value p;p["participantId"]=std::string("final-")+std::to_string(rank);p["name"]="HwanHee";p["rank"]=rank;p["delta"]=50;podium.push_back(p);}
 view.result["standings"]=podium;ui.showParticipants(view,true);assert(ui.m_tiles.size()==3);
 for(int rank=1;rank<=3;++rank){auto const& t=ui.m_tiles.at(std::string("final-")+std::to_string(rank));assert(t.medals[rank-1]->visible&&t.name->text=="HwanHee");}
 view.result={};view.roundKey="r2";view.standings={b};ui.showParticipants(view,false);assert(ui.m_tiles.size()==1&&!ui.m_tiles.contains("a"));
 view.id="large";view.roundKey="large-r1";view.matchPlayerCount=64;view.standings.clear();
 for(int i=0;i<64;++i){SurvivalStanding p;p.id="large-"+std::to_string(i);p.name="Nickname "+std::to_string(i);p.rank=i+1;p.bestProgress=99-i;p.bot=true;p.passing=i<40;view.standings.push_back(p);}
 ui.showParticipants(view,false);assert(ui.m_cardPages==2&&ui.m_tiles.size()==48&&ui.pager.visible&&ui.pageLabel.text=="1 / 2");
 auto* first=ui.m_tiles.at("large-0").root;
 for(auto const&[id,tile]:ui.m_tiles){assert(tile.name->text.starts_with("Nickname ")&&!tile.record&&!tile.tierBadge);for(auto* medal:tile.medals)assert(!medal);}
 ui.m_cardPage=1;ui.showParticipants(view,false);assert(ui.m_tiles.size()==64&&ui.pageLabel.text=="2 / 2");
 int visible=0;for(auto const&[id,tile]:ui.m_tiles)visible+=tile.root->visible;assert(visible==16&&!first->visible);
 ui.m_cardPage=0;++view.revision;ui.showParticipants(view,false);assert(ui.m_tiles.at("large-0").root==first&&first->visible&&ui.m_tiles.size()==64);
 view.roundKey="large-final";view.standings.resize(5);ui.showParticipants(view,false);assert(ui.m_tiles.size()==5&&ui.m_cardPages==1&&!ui.pager.visible);

}
`);
});

test('direct cleared replay quits release local ownership for repeat entry without ending menu Free Play',()=>{
 const source=read('src/SurvivalRuntime.cpp');
 run(preamble+`
#include "domain/SurvivalFreePlayPolicy.hpp"
struct GJGameLevel{int m_levelID=42;};struct CCScene{};
struct PlayLayer{GJGameLevel* m_level=nullptr;CCScene* parent=nullptr;CCScene* getParent(){return parent;}static inline PlayLayer* current=nullptr;static PlayLayer*get(){return current;}};
struct CCDirector{CCScene* current=nullptr;static auto* sharedDirector(){static CCDirector d;return &d;}CCScene* getRunningScene(){return current;}};
namespace corum::ranked{struct SurvivalRuntime{SurvivalFreePlayState m_freePlay;void requestDirectFreePlayReturn(PlayLayer*);};
${method(source,'void SurvivalRuntime::requestDirectFreePlayReturn(')}
}
int main(){using namespace corum::ranked;SurvivalRuntime r;GJGameLevel level;CCScene scene,other;PlayLayer play;play.m_level=&level;play.parent=&scene;PlayLayer::current=&play;CCDirector::sharedDirector()->current=&scene;
 for(int repeat=0;repeat<100;++repeat){
  r.m_freePlay.begin({"match","r1",1},"done",false,42);assert(!r.m_freePlay.returnRequested);
  r.requestDirectFreePlayReturn(&play);assert(r.m_freePlay.returnRequested);r.m_freePlay.finish();assert(!r.m_freePlay.active);
 }
 r.m_freePlay.begin({"match","r1",1},"done");r.requestDirectFreePlayReturn(&play);assert(!r.m_freePlay.returnRequested);
 r.m_freePlay.begin({"match","r1",1},"done",true,42);level.m_levelID=43;r.requestDirectFreePlayReturn(&play);assert(!r.m_freePlay.returnRequested);
 level.m_levelID=42;play.parent=&other;r.requestDirectFreePlayReturn(&play);assert(!r.m_freePlay.returnRequested);
 play.parent=&scene;r.requestDirectFreePlayReturn(&play);assert(r.m_freePlay.returnRequested);
}
`);
 const native=read('src/PlayLayer.cpp');const quit=method(native,'    void onQuit()');
 assert(quit.indexOf('requestDirectFreePlayReturn(this)')<quit.indexOf('freePlayReturnPending()'));
});
