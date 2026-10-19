import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
test('actual Survival custom popup resolves exact stats/forum identity, never looks up bots, and retires late callbacks/delegates',()=>{
 const source=read('src/ui/SurvivalPlayerInfoPopup.hpp').replace(/^#pragma once\s*|^#include .*$/gm,'').replace('class SurvivalPlayerInfoPopup final:public Popup,public UserInfoDelegate {','class SurvivalPlayerInfoPopup final:public Popup,public UserInfoDelegate { public:');
 const cpp=String.raw`
#include <matjson.hpp>
#include "domain/PlayerInfoPresentation.hpp"
#include "domain/SurvivalHudPresentation.hpp"
#include <cassert>
#include <chrono>
#include <functional>
#include <vector>
#include <string>
using namespace corum::ranked;
using std::chrono::steady_clock;
struct CCObject{virtual ~CCObject()=default;};struct CCPoint{float x,y;};struct CCSize{float width,height;};struct ccColor3B{unsigned char r,g,b;};
struct CCNode:CCObject{bool parent=true;static CCNode* create(){return new CCNode;}void addChild(CCNode*,int=0){}void removeFromParentAndCleanup(bool){parent=false;}void* getParent(){return parent?this:nullptr;}void setVisible(bool){}void setID(std::string const&){}void setPosition(CCPoint){}void setScale(float){}void autorelease(){}void schedule(int,float){}void unschedule(int){}};
struct CCLabelBMFont:CCNode{inline static std::vector<std::string> labels;std::string text;void setAnchorPoint(CCPoint){}};
struct CCMenu:CCNode{};
struct CCMenuItemSpriteExtra:CCNode{static auto* create(CCNode*,CCNode*,int){return new CCMenuItemSpriteExtra;}};
struct CCDirector{static auto* sharedDirector(){static CCDirector d;return &d;}CCSize getWinSize(){return {568,320};}};
struct Popup:CCNode{CCNode *m_bgSprite=new CCNode,*m_closeBtn=new CCNode,*m_mainLayer=new CCNode;CCMenu*m_buttonMenu=new CCMenu;bool init(float,float){return true;}virtual void onExit(){}virtual void onClose(CCObject*){parent=false;onExit();}};
struct CCTouchDispatcher{int releases=0;static auto* get(){static CCTouchDispatcher d;return &d;}void unregisterForcePrio(CCNode*){++releases;}};
struct GJUserScore{int m_accountID=123,m_globalRank=5,m_stars=42,m_moons=12,m_demons=8;std::string m_demonInfo="0,0,0,0,3,0,0,0,0,2,0,0";};
struct UserInfoDelegate{virtual ~UserInfoDelegate()=default;virtual void getUserInfoFinished(GJUserScore*){}virtual void getUserInfoFailed(int){}virtual void userInfoChanged(GJUserScore*){}};
struct GameLevelManager{UserInfoDelegate* m_userInfoDelegate=nullptr;GJUserScore* cached=nullptr;int requests=0,account=0;static auto* sharedState(){static GameLevelManager m;return &m;}GJUserScore* userInfoForAccountID(int id){return cached&&cached->m_accountID==id?cached:nullptr;}void getGJUserInfo(int id){requests++;account=id;}};
#define schedule_selector(x) 0
#define menu_selector(x) 0
std::string operator""_spr(char const*s,std::size_t n){return std::string(s,n);}
template<class T>struct Ref{explicit Ref(T*){}};
template<class T>struct WeakRef{T* p;explicit WeakRef(T* value):p(value){}struct Locked{T*p;explicit operator bool()const{return p!=nullptr;}T*operator->(){return p;}};auto lock()const{return Locked{p};}};
namespace web {struct WebResponse{int status=200;matjson::Value value;int code(){return status;}struct Parsed{matjson::Value value;matjson::Value unwrapOr(matjson::Value){return value;}};auto json(){return Parsed{value};}};
struct Operation{std::string path;};struct WebRequest{WebRequest&timeout(std::chrono::seconds){return *this;}WebRequest&followRedirects(bool){return *this;}void header(std::string const&,std::string const&){}Operation get(std::string const& path){return {path};}};}
namespace geode {template<class T>using WeakRef=::WeakRef<T>;namespace utils::web{using WebResponse=::web::WebResponse;}
namespace async{inline std::vector<std::function<void(::web::WebResponse)>> callbacks;inline std::vector<std::string> paths;template<class T>struct TaskHolder{int cancellations=0;void cancel(){++cancellations;}void spawn(::web::Operation op,std::function<void(T)> f){paths.push_back(op.path);callbacks.push_back(std::move(f));}};}
namespace prelude{using ::Ref;using ::WeakRef;namespace web=::web;}}
namespace fmt {template<class T>std::string format(char const* pattern,T const& value){std::string s(pattern);auto pos=s.find("{}");if(pos!=std::string::npos)s.replace(pos,2,std::to_string(value));return s;}}
template<class T>struct Number{std::string value;T unwrapOr(T fallback){try{return static_cast<T>(std::stoll(value));}catch(...){return fallback;}}};template<class T>auto numFromString(std::string value){return Number<T>{value};}
namespace corum::ranked {
struct SurvivalRuntime{std::string id="match";bool live=true;static auto& get(){static SurvivalRuntime r;return r;}bool active(){return live;}struct View{std::string id;};auto view(){return View{id};}};
struct RankedRuntime{std::string session="session";static auto&get(){static RankedRuntime r;return r;}std::string spectatorLiveSessionToken(){return session;}};
namespace i18n{std::string text(char const*,char const*fallback){return fallback;}}
namespace ui{enum class FontWeight{SemiBold,ExtraBold,Black};constexpr ccColor3B Cyan{1,2,3},Blue{1,2,3},Surface{1,2,3},SurfaceRaised{1,2,3},Text{1,2,3},TextMuted{1,2,3},Gold{1,2,3};
CCNode* surface(CCSize,CCPoint,ccColor3B,ccColor3B,int,bool){return new CCNode;}
CCLabelBMFont* menuLabel(std::string const& s,float,CCPoint,ccColor3B,FontWeight){auto*n=new CCLabelBMFont;n->text=s;CCLabelBMFont::labels.push_back(s);return n;}
CCLabelBMFont* playerNameLabel(std::string const&s,float scale,CCPoint p,ccColor3B c,FontWeight w){return menuLabel(s,scale,p,c,w);}
void fitFullMenuLabel(CCLabelBMFont*,float,float,float){}
enum class RankBadgeDisplay{Compact};CCNode*createAnimatedRankBadge(std::string const&,CCSize,RankBadgeDisplay){return new CCNode;}
}}
`+source+String.raw`
int main(){using namespace corum::ranked;using ui::SurvivalPlayerInfoPopup;auto*manager=GameLevelManager::sharedState();UserInfoDelegate foreign;
 SurvivalStanding human;human.id="human";human.accountId=123;human.name="Player";human.tier="GOLD";human.rank=1;
 manager->m_userInfoDelegate=&foreign;auto*p=SurvivalPlayerInfoPopup::create(human,"match");assert(p);p->refresh(0);assert(manager->requests==0&&manager->m_userInfoDelegate==&foreign);
 assert(geode::async::paths.back().ends_with("/match/player-info/human"));
 manager->m_userInfoDelegate=nullptr;p->refresh(0);assert(manager->requests==1&&manager->account==123);
 GJUserScore wrong;wrong.m_accountID=999;p->getUserInfoFinished(&wrong);assert(!p->m_statistics&&manager->m_userInfoDelegate==p);
 GJUserScore score;p->getUserInfoFinished(&score);assert(p->m_statistics&&p->m_statistics->extreme==5&&manager->m_userInfoDelegate==nullptr);
 matjson::Value forum;forum["participantId"]="human";forum["gdAccountId"]="123";matjson::Value record;record["provider"]="GDKF";record["rank"]=7;record["status"]="RANKED";forum["records"]=std::vector<matjson::Value>{record};
 auto callback=geode::async::callbacks.front();callback({200,forum});assert(!p->m_forumFailed&&p->m_forum["gdAccountId"].asString().unwrapOr("")=="123");
 for(auto const* label:{"Stars","Moons","Demons","Extreme Demons","GDKF","Pointercrate","#7","42","12","8","5"})assert(std::find(CCLabelBMFont::labels.begin(),CCLabelBMFont::labels.end(),label)!=CCLabelBMFont::labels.end());
 p->dismiss();auto count=CCLabelBMFont::labels.size();callback({200,forum});p->getUserInfoFinished(&score);assert(CCLabelBMFont::labels.size()==count&&manager->m_userInfoDelegate==nullptr);delete p;
 SurvivalStanding bot=human;bot.bot=true;bot.accountId=999;auto requests=manager->requests;auto reads=geode::async::paths.size();auto*b=SurvivalPlayerInfoPopup::create(bot,"match");b->refresh(0);assert(manager->requests==requests&&geode::async::paths.size()==reads&&b->m_player.accountId==0);b->dismiss();delete b;
 auto*q=SurvivalPlayerInfoPopup::create(human,"match");q->refresh(0);assert(manager->m_userInfoDelegate==q);manager->m_userInfoDelegate=&foreign;SurvivalRuntime::get().id="other";q->refresh(0);assert(q->m_closed&&manager->m_userInfoDelegate==&foreign);delete q;
}
`;
 const dir=mkdtempSync(join(tmpdir(),'survival-player-info-'));
 const json=process.env.CORUM_MATJSON_SOURCE??'/workspace/scratch/geode-cpm-cache/json/f349';
 const result=process.env.CORUM_RESULT_INCLUDE??'/workspace/scratch/geode-cpm-cache/result/1b9c/include';
 try{writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-I',join(root,'src'),'-I',join(json,'include'),'-I',result,join(dir,'test.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(s=>join(json,'src',s+'.cpp')),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
 assert.doesNotMatch(read('src/ui/SurvivalProfile.hpp'),/ProfilePage/);
});
