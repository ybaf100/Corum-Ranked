import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('..',import.meta.url));
const read=p=>readFileSync(join(root,p),'utf8');
function method(src,name){let a=src.indexOf(name);assert(a>=0,name);let e=src.indexOf('{',a)+1,d=1;while(d){d+=(src[e]==='{')-(src[e]==='}');e++;}return src.slice(a,e);}
function run(cpp,json=false){const dir=mkdtempSync(join(tmpdir(),'corum-followup-'));try{
 writeFileSync(join(dir,'test.cpp'),cpp);const args=['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-Wno-misleading-indentation','-Wno-unused-parameter','-I',join(root,'src')];
 if(json){args.push('-Wno-pedantic');const j=process.env.CORUM_MATJSON_SOURCE??'/workspace/scratch/geode-cpm-cache/json/f349';args.push('-I',join(j,'include'),'-I',process.env.CORUM_RESULT_INCLUDE??'/workspace/scratch/geode-cpm-cache/result/1b9c/include',...['value','impl','parser','dump','external/dragonbox'].map(p=>join(j,'src',p+'.cpp')));}
 execFileSync('g++',[...args,join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}

test('podium timeline raises 3→2→1, then drops each icon, then stamps settled labels; no early input',()=>run(`
#include "domain/SurvivalPodiumMotion.hpp"
#include "${join(root,'tests/helpers/InactiveReplayDouble.hpp')}"
#include <cassert>
using namespace corum::ranked;
int main(){
 for(int rank:{3,2,1}){auto start=.15+(3-rank)*1.30;
  auto before=survivalPodiumFrame(rank,start-.001);assert(before.rise==0&&!before.avatar&&!before.interactive&&before.stampOpacity==0);
  auto rising=survivalPodiumFrame(rank,start+.25);assert(rising.rise>0&&rising.rise<1&&!rising.avatar&&rising.stampOpacity==0);
  auto dropping=survivalPodiumFrame(rank,start+.65);assert(dropping.rise==1&&dropping.avatar&&dropping.drop>0&&dropping.drop<1&&dropping.stampOpacity==0&&!dropping.interactive);
  auto stamp=survivalPodiumFrame(rank,start+1.);assert(stamp.drop==0&&stamp.stampOpacity>0&&stamp.stampOpacity<1&&stamp.stampScale>1&&!stamp.interactive);
  auto done=survivalPodiumFrame(rank,start+1.101);assert(done.rise==1&&done.drop==0&&done.stampOpacity==1&&done.stampScale==1&&done.interactive);
 }
 assert(survivalPodiumFrame(3,1.3).interactive&&!survivalPodiumFrame(2,1.3).avatar);
 assert(survivalPodiumFrame(2,2.6).interactive&&!survivalPodiumFrame(1,2.6).avatar);
 for(int rank:{0,4})assert(!survivalPodiumFrame(rank,100).interactive);
}`));

test('selected map song row stays within its panel and outside controls across phone, iPad and wide viewports',()=>run(`
#include "domain/SurvivalSelectionLayout.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){for(auto [w,h]:{std::pair{400.f,320.f},std::pair{568.f,320.f},std::pair{432.f,324.f},std::pair{640.f,480.f},std::pair{900.f,320.f}})for(bool thumb:{false,true}){
 auto r=survivalSelectionLayout(w,h,thumb,true);assert(r.panel.contains(r.song));
 for(auto b:{r.ldm,r.shake,r.jukebox,r.ready,r.title,r.creator,r.difficulty,r.metadata})assert(!r.song.overlaps(b));
 assert((SurvivalRect{0,0,w,h}.contains(r.header)));
}}
`));

test('GDDL enrichment validates exact ID/rating, coalesces requests, retries slowly and never overwrites a removed or non-Demon cache entry',()=>{
 const request=method(read('src/RankedDemonMetadata.cpp'),'void RankedMapDetails::requestDemonTier(').replaceAll('std::chrono::steady_clock::now()','fakeNow');
 run(`
#include "domain/GddlMetadata.hpp"
#include <cassert>
#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <vector>
auto fakeNow=std::chrono::steady_clock::time_point{};
namespace web{
 struct WebResponse{int status=200;std::vector<char> bytes;matjson::Value value;int code()const{return status;}auto const&data()const{return bytes;}geode::Result<matjson::Value> json()const{return geode::Ok(value);}};
 struct WebRequest{int seconds=0;bool follow=true,tls=false;auto&timeout(std::chrono::seconds s){seconds=s.count();return *this;}auto&followRedirects(bool b){follow=b;return *this;}auto&certVerification(bool b){tls=b;return *this;}std::string get(std::string u){assert(seconds==4&&!follow&&tls);return u;}};
}
std::vector<std::function<void(web::WebResponse)>> callbacks;
namespace geode::async{template<class T>struct TaskHolder{template<class F>void spawn(std::string u,F f){assert(u.starts_with("https://gdladder.com/api/levels/"));callbacks.push_back(f);}};}
namespace corum::ranked{
 struct RankedMapDetails{struct Cache{MapMetadata metadata;};std::map<int,Cache>m_cache;int m_revision=0;void requestDemonTier(int);};
 ${request}
}
int main(){using namespace corum::ranked;MapMetadata meta;meta.kind="DEMON";matjson::Value body;body["ID"]=11;body["Rating"]=6.6;
 assert(applyGddlMetadata(body,11,meta)&&meta.gddlTier==7&&meta.gddlColor=="#dba7dc");
 auto original=meta;for(auto id:{0,12}){assert(!applyGddlMetadata(body,id,meta));assert(meta==original);}
 body["ID"]="11";assert(!applyGddlMetadata(body,11,meta));body["ID"]=11;
 for(double invalid:{-.5,0.,.49,40.5,100.}){body["Rating"]=invalid;assert(!applyGddlMetadata(body,11,meta));assert(meta==original);}
 body["Rating"]=0;body["DefaultRating"]=12;assert(applyGddlMetadata(body,11,meta)&&meta.gddlTier==12);
 RankedMapDetails d;d.requestDemonTier(11);assert(callbacks.empty());d.m_cache[11].metadata.kind="NORMAL";d.requestDemonTier(11);assert(callbacks.empty());
 d.m_cache[11].metadata.kind="DEMON";d.requestDemonTier(11);d.requestDemonTier(11);assert(callbacks.size()==1);
 body["ID"]=12;callbacks.back()({200,{},body});assert(!d.m_cache[11].metadata.gddlTier&&d.m_revision==0);d.requestDemonTier(11);assert(callbacks.size()==1);
 fakeNow+=std::chrono::minutes(5);d.requestDemonTier(11);assert(callbacks.size()==2);body["ID"]=11;callbacks.back()({200,{},body});assert(d.m_cache[11].metadata.gddlTier==12&&d.m_revision==1);d.requestDemonTier(11);assert(callbacks.size()==2);
 d.m_cache[22].metadata.kind="DEMON";d.requestDemonTier(22);d.m_cache.erase(22);body["ID"]=22;callbacks.back()({200,{},body});assert(!d.m_cache.contains(22)&&d.m_revision==1);
 d.m_cache[33].metadata.kind="DEMON";d.requestDemonTier(33);d.m_cache[33].metadata.kind="NORMAL";body["ID"]=33;callbacks.back()({200,{},body});assert(!d.m_cache[33].metadata.gddlTier&&d.m_revision==1);
}
`,true);
});

test('leaderboard cards share the native scroll owner without a swallowing row menu',()=>{
 const src=read('src/ui/RankedLeaderboardPopup.cpp');
 assert.match(src,/class LeaderboardScrollLayer final:public ScrollLayer/);
 assert.match(src,/ScrollLayer::ccTouchBegan/);assert.match(src,/ScrollLayer::ccTouchMoved/);
 assert.doesNotMatch(src,/LeaderboardRowMenu/);
 const rows=method(src,'void RankedLeaderboardPopup::showRows(');
 assert.doesNotMatch(rows,/CCMenu::create|CCMenuItemSpriteExtra::create/);
});

test('result settlement animation starts automatically once, waits for real settlement and survives reopened result screens',()=>{
 const src=read('src/ui/SurvivalGameLayer.cpp');
 assert.match(src,/if\(stage==SurvivalFlowStage::ScoreAnimation\)beginResultMotion\(\)/);
 const panel=read('src/ui/SurvivalOutcomePanel.hpp');assert.doesNotMatch(panel,/LP DETAILS/);assert.match(panel,/if\(choice\)button\(i18n::text\("survival-view-board"/);
 run(`
#include <cassert>
#include <functional>
#include <optional>
#include <string>
struct Value{bool ready=false;bool isObject()const{return ready;}};
struct ScoreAnimationInput{bool promoted=false,demoted=false;std::string beforeTier="RED",afterTier="RED";};
struct SurvivalRuntime{struct View{std::string id="match";Value result;}v;std::optional<ScoreAnimationInput> input;static auto&get(){static SurvivalRuntime r;return r;}auto const&view(){return v;}std::string resultFlowKey()const{return v.id;}auto scoreAnimation(){return input;}};
struct Mod{std::string saved;static Mod*get(){static Mod m;return &m;}template<class T>T getSavedValue(char const*){return saved;}void setSavedValue(char const*,std::string s){saved=s;}};
template<class T>struct WeakRef{T*p;WeakRef(T*p):p(p){}struct Lock{T*p;explicit operator bool()const{return p;}T*operator->(){return p;}};Lock lock()const{return {p};}};
std::string text(char const*,char const*s){return s;}
namespace ui{enum class RankTransitionType{Promotion,Demotion};struct RankTransitionLayer{static RankTransitionLayer*create(std::string,std::string,RankTransitionType,std::string,std::string,std::string,std::function<void()>,bool){return nullptr;}};}
struct Layer{using SurvivalGameLayer=Layer;std::string m_resultMotion;bool m_motion=false,keypad=true;int scores=0;void setKeypadEnabled(bool b){keypad=b;}void addChild(ui::RankTransitionLayer*,int){}void scoreMotion(ScoreAnimationInput,std::string){++scores;}
${method(src,'void beginResultMotion()')}
};
int main(){Layer l;auto&r=SurvivalRuntime::get();l.beginResultMotion();assert(l.scores==0);
 r.v.result.ready=true;l.beginResultMotion();assert(l.scores==0&&l.m_resultMotion.empty()&&Mod::get()->saved.empty());
 r.input=ScoreAnimationInput{};l.beginResultMotion();assert(l.scores==1&&l.m_motion&&!l.keypad);for(int i=0;i<20;i++)l.beginResultMotion();assert(l.scores==1);
 Layer reopened;reopened.beginResultMotion();assert(reopened.scores==1); // An interrupted animation is not falsely marked complete.
 r.v.id="next-match";r.input->promoted=true;r.input->afterTier="AQUA";reopened.beginResultMotion();assert(reopened.scores==2&&Mod::get()->saved.empty());
}
`);
});

test('Credits is inside Settings and reads real artist - title metadata without inventing music credits or taking a GD delegate',()=>{
 const src=read('src/ui/RankedCreditsPopup.cpp'),settings=read('src/ui/RankedSettingsPopup.hpp');
 assert.match(settings,/menu_selector\(RankedSettingsPopup::onCredits\)/);assert.match(settings,/ranked-credits-button/);
 assert.match(src,/PROJECT LEAD · GAME DESIGN · DEVELOPMENT/);assert.match(src,/AI-ASSISTED IMPLEMENTATION/);assert.match(src,/Code implementation assisted by AI/);assert.match(src,/add\("hwanhee1"/);assert.equal(JSON.parse(read('../ranked/translations/ui.json'))['credits-planner'].ko,'기획');
 assert.doesNotMatch(src,/m_songInfoDelegate\s*=|m_songInfoObject\s*=|Developer|개발자/);
 run(`
#include <cassert>
#include <chrono>
#include <map>
#include <string>
#include <vector>
auto wall=std::chrono::steady_clock::time_point{};
struct Label{std::string text;void setString(char const*s){text=s;}};struct Song{std::string m_artistName,m_songName;};
struct MusicDownloadManager{int requests=0;std::map<int,Song>known;static auto*sharedState(){static MusicDownloadManager m;return &m;}Song*getSongInfoObject(int id){auto i=known.find(id);return i==known.end()?nullptr:&i->second;}int getSongDownloadKey(int i){return i;}bool isDLActive(int){return false;}void getSongInfo(int,bool){++requests;}};
void fitFullMenuLabel(Label*,int,int,float){}
namespace i18n{std::string text(char const*,char const*s){return s;}}
namespace fmt{std::string format(char const*,int i){return " · GD Song #"+std::to_string(i);}}
struct Popup{struct Credit{int id=0,requests=0;std::string text;Label*label;std::chrono::steady_clock::time_point next{};};std::vector<Credit>m_songs;bool m_closed=false;
${method(src,'void refresh(float)').replaceAll('std::chrono::steady_clock::now()','wall')}
};
int main(){Label label;Popup p;p.m_songs.push_back({123,0,"",&label,{}});auto*m=MusicDownloadManager::sharedState();p.refresh(0);p.refresh(0);assert(m->requests==1);
 wall+=std::chrono::seconds(8);p.refresh(0);assert(m->requests==2);wall+=std::chrono::seconds(8);p.refresh(0);assert(label.text=="Unavailable · GD Song #123");
 m->known[123]={"Composer","Song Title"};p.refresh(0);assert(label.text=="Composer - Song Title");assert(m->requests==2);
 p.m_closed=true;m->known[123].m_songName="Changed";p.refresh(0);assert(label.text=="Composer - Song Title");
}
`);
});

test('observer exit and cleared replay retain exact scene/context/session gates and all local play suppresses official progress',()=>{
 const src=read('src/SurvivalRuntime.cpp');run(`
#include "domain/SurvivalFreePlayPolicy.hpp"
#include "${join(root,'tests/helpers/InactiveReplayDouble.hpp')}"
#include "domain/LevelPreparationPolicy.hpp"
#include <cmath>
#include <cassert>
#include <chrono>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <utility>
struct Value{std::string text;Value()=default;Value&operator=(bool){return *this;}Value&operator=(std::string s){text=s;return *this;}Value&operator=(double){return *this;}Value&operator[](char const*){return *this;}struct Read{std::string text;std::string unwrapOr(std::string s)const{return text.empty()?s:text;}};Read asString()const{return {text};}struct IntRead{int unwrapOr(int v)const{return v;}};IntRead asInt()const{return {};}};
namespace matjson{using Value=::Value;}
struct GJGameLevel{int id=40000000;};struct CCScene{};struct CCDirector{CCScene*scene=nullptr;static auto*sharedDirector(){static CCDirector d;return &d;}CCScene*getRunningScene(){return scene;}};
template<class T>struct Ref{T*p=nullptr;Ref()=default;Ref(std::nullptr_t){}T*data()const{return p;}Ref&operator=(std::nullptr_t){p=nullptr;return *this;}};
template<class T>struct WeakRef{T*p=nullptr;WeakRef(T*p):p(p){}};
namespace geode::log{template<class...T>void info(T...){} }
namespace corum::ranked{
namespace log=geode::log;
struct RankedRuntime{std::string token="session";static auto&get(){static RankedRuntime r;return r;}std::string spectatorLiveSessionToken()const{return token;}};
enum class SurvivalOutcomeChoice{PersonalResult,Undecided};
std::string preparationContextText(LevelPreparationContext const&){return "exact-context";}
struct SurvivalRuntime{
 struct View{std::string id="match",roundKey="r1",state="PLAYING";int mapGeneration=1;std::string error;bool ownEliminated=false,online=false,ownAssetsReady=true,ownEntered=true,ownAbsent=false;struct Map{int levelId=40000000;}map;Value completedRound;}m_view;
 LevelPreparationContext context{"match",40000000,1,0,false,0,0},m_prepared=context;std::string m_session="session",m_attempt="official-old",m_attemptContext="exact-context";double m_attemptBest=38;bool cleared=true,watch=false,m_freePlayNativeQuitFinished=false;long remaining=180000;int returns=0,events=0;
 SurvivalFreePlayState m_freePlay;std::optional<WeakRef<CCScene>>m_freePlayOrigin,m_freePlayQuitScene;Ref<GJGameLevel>m_freePlayQuitLevel;
 bool active()const{return true;}bool freePlaying()const{return m_freePlay.active;}bool normalRoundCleared()const{return cleared;}bool officialRoundCleared()const{return cleared;}bool surrenderPending()const{return false;}bool observing()const{return watch;}long remainingMs()const{return remaining;}
 LevelPreparationContext preparationContext()const{return context;}bool validateNative(GJGameLevel*l)const{return l&&l->id==context.levelId;}
 void chooseOutcome(SurvivalOutcomeChoice){watch=false;}void pumpFreePlayReturn(){++returns;}
 Value m_attemptOwner,m_attemptMetadata;Value event(std::string){return {};}void enqueue(Value){++events;}
 bool canFreePlay()const;bool canReplayCleared()const;bool armClearedReplay(GJGameLevel*,CCScene*);bool canPlay()const;void leaveObserver();void endAttempt(double,std::string reason="EXIT",std::string expectedAttempt="");
};
${['bool SurvivalRuntime::canFreePlay()', 'bool SurvivalRuntime::canReplayCleared()', 'bool SurvivalRuntime::armClearedReplay(', 'bool SurvivalRuntime::canPlay()', 'void SurvivalRuntime::leaveObserver()', 'void SurvivalRuntime::endAttempt('].map(n=>method(src,n)).join('\n')}
}
int main(){using namespace corum::ranked;SurvivalRuntime r;CCScene origin,other;CCDirector::sharedDirector()->scene=&origin;GJGameLevel level;
 assert(r.canReplayCleared());r.remaining=0;assert(!r.canReplayCleared());r.remaining=180000;++r.m_prepared.round;assert(!r.canReplayCleared());r.m_prepared=r.context;
 r.m_session="retired";assert(!r.canReplayCleared());r.m_session="session";assert(!r.armClearedReplay(&level,&other));++level.id;assert(!r.armClearedReplay(&level,&origin));--level.id;
 assert(r.armClearedReplay(&level,&origin));assert(r.freePlaying()&&!r.canPlay()&&!r.canReplayCleared());r.cleared=false;assert(!r.canPlay());r.endAttempt(100);assert(r.events==0&&r.m_attempt=="official-old"&&r.m_attemptBest==38);
 r.m_freePlay.finish();r.watch=true;r.m_view.ownEliminated=true;r.m_freePlay.begin({"match","r1",1},"",true);r.leaveObserver();r.leaveObserver();assert(!r.watch&&r.m_view.ownEliminated&&r.m_freePlay.returnRequested&&r.returns==1&&r.events==0);
 assert(r.m_freePlay.needsReturn({"match","r1",1},"PLAYING","",true));r.m_freePlay.finish();assert(!r.m_freePlay.returnRequested&&!r.freePlaying());
}
`);
});
