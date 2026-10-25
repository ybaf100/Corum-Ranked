import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const root=new URL('..',import.meta.url).pathname,read=p=>readFileSync(join(root,p),'utf8');
function run(source){
 const json=process.env.CORUM_MATJSON_SOURCE,result=process.env.CORUM_RESULT_INCLUDE;
 assert(json&&result,'Pinned matjson/Result dependencies are required');
 const dir=mkdtempSync(join(tmpdir(),'public-profile-'));
 try{
  writeFileSync(join(dir,'test.cpp'),source);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-isystem',join(json,'include'),'-isystem',result,join(dir,'test.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(p=>join(json,'src',p+'.cpp')),'-o',join(dir,'test')],{stdio:'pipe'});
  execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
}
test('actual public profile helpers retain scoped titles/effects, current badges, missing LP and promotion marks',()=>{
 const source=read('src/ui/PublicPlayerProfile.hpp').replace(/^#pragma once\s*|^#include .*$/gm,'');
 run(`
#include <matjson.hpp>
#include <cassert>
#include <string>
#include <vector>
struct CCPoint{float x,y;};struct CCNode{std::vector<CCNode*>children;void addChild(CCNode*p,int){children.push_back(p);}void setPosition(CCPoint){}};
struct CCLabelBMFont:CCNode{std::string text;int effect=0;bool unlocked=false;float fitted=0;void setAnchorPoint(CCPoint){}};
namespace geode::prelude{}
namespace corum::ranked::ui{
enum class FontWeight{SemiBold,ExtraBold};enum class RankBadgeDisplay{Leaderboard};constexpr int Text=1,TextMuted=2,Green=3;
namespace i18n{bool korean=false;bool isKorean(){return korean;}std::string text(char const*,char const*f){return f;}}
CCLabelBMFont*menuLabel(std::string const&s,float,CCPoint,int,FontWeight){auto*p=new CCLabelBMFont;p->text=s;return p;}
void fitSingleLineMenuLabel(CCLabelBMFont*p,float width,float,float){p->fitted=width;}
void styleAchievementText(CCLabelBMFont*p,int tier,bool unlocked){p->effect=tier;p->unlocked=unlocked;}
std::vector<std::string>ambientBadges;
CCNode*createAnimatedRankBadge(std::string const&tier,CCPoint,RankBadgeDisplay){ambientBadges.push_back(tier);return new CCNode;}
}
${source}
int main(){using namespace corum::ranked::ui;
 auto t=matjson::parse(R"({"achievementId":"survivor","scope":"SURVIVAL","nameKo":"[서바이벌] 서바이버","nameEn":"[Survival] Survivor","rarity":"LEGENDARY","level":null,"displayLabel":""})").unwrap();CCNode parent;
 assert(!publicPlayerTitle(&parent,matjson::Value(),{},120));
 auto*title=publicPlayerTitle(&parent,t,{},120);assert(title->text=="[Survival] Survivor"&&title->effect==4&&title->unlocked&&title->fitted==120);
 i18n::korean=true;assert(publicTitleText(t)=="[서바이벌] 서바이버");
 t["level"]=5;t["displayLabel"]="V";title=publicPlayerTitle(&parent,t,{},90);assert(title->text=="[서바이벌] 서바이버 V"&&title->effect==5);
 i18n::korean=false;auto profile=matjson::parse(R"({"tier":"SILVER","lp":900,"promotion":null})").unwrap();
 publicRankLine(&parent,"SURVIVAL",profile,200);assert(ambientBadges.back()=="SILVER");assert(static_cast<CCLabelBMFont*>(parent.children.back())->text=="900 LP");
 profile["lp"]=nullptr;publicRankLine(&parent,"SOLO RANKED",profile,170);assert(static_cast<CCLabelBMFont*>(parent.children.back())->text=="Unavailable");
 profile["promotion"]=matjson::parse(R"({"results":["WIN","LOSS","WIN"]})").unwrap();publicRankLine(&parent,"SOLO RANKED",profile,170);
 assert(static_cast<CCLabelBMFont*>(parent.children.back())->text=="PROMOTION MATCH O X O");
 auto before=ambientBadges.size();publicRankLine(&parent,"SURVIVAL",matjson::Value(),140);assert(ambientBadges.size()==before&&static_cast<CCLabelBMFont*>(parent.children.back())->text=="Unavailable");
}
`);
});
test('actual update-channel popup verifies Debug through Lightsail and cancels stale requests without changing channel',()=>{
 const source=read('src/ui/RankedUpdateChannelPopup.hpp').replace(/^#pragma once\s*|^#include .*$/gm,'').replace('class RankedUpdateChannelPopup final:public Popup {','class RankedUpdateChannelPopup final:public Popup { public:');
 run(`
#include <matjson.hpp>
#include <cassert>
#include <chrono>
#include <functional>
#include <string>
#include <utility>
#include <vector>
struct CCObject{virtual~CCObject()=default;};struct CCPoint{float x,y;};struct CCSize{float width,height;};
struct CCNode:CCObject{bool parent=true,visible=true;int refs=0;void*getParent(){return parent?this:nullptr;}void addChild(CCNode*,int=0){}void setVisible(bool v){visible=v;}void setPosition(CCPoint){}void setID(std::string const&){}void autorelease(){}};
struct CCLabelBMFont:CCNode{std::string value;void setString(char const*s){value=s;}};
struct CCMenu:CCNode{};struct CCMenuItemSpriteExtra:CCNode{bool enabled=true;void setEnabled(bool b){enabled=b;}static auto*create(CCNode*,CCNode*,int){return new CCMenuItemSpriteExtra;}};
using SEL_MenuHandler=int;
#define menu_selector(x) 0
std::string operator""_spr(char const*s,std::size_t n){return std::string(s,n);}
struct CCDirector{static auto*sharedDirector(){static CCDirector d;return &d;}CCSize getWinSize(){return {480,320};}};
struct Popup:CCNode{bool m_noElasticity=false;CCNode*m_bgSprite=new CCNode,*m_closeBtn=new CCNode,*m_mainLayer=new CCNode;CCMenu*m_buttonMenu=new CCMenu;bool init(float,float){return true;}virtual void onExit(){}virtual void onClose(CCObject*){parent=false;onExit();}};
struct CCTouchDispatcher{int releases=0;static auto*get(){static CCTouchDispatcher d;return &d;}void unregisterForcePrio(void*){++releases;}};
template<class T>struct Ref{T*p;Ref(T*v):p(v){++p->refs;}~Ref(){--p->refs;}};
template<class T>struct WeakRef{T*p;WeakRef(T*v):p(v){}struct Locked{T*p;explicit operator bool()const{return p;}T*operator->(){return p;}};auto lock()const{return Locked{p};}};
struct TextInput:CCNode{std::string value;bool masked=false,enabled=true;int limit=0;static auto*create(float,std::string const&){return new TextInput;}void setPasswordMode(bool b){masked=b;}void setMaxCharCount(int n){limit=n;}std::string const&getString(){return value;}void setString(std::string s){value=std::move(s);}void setEnabled(bool b){enabled=b;}};
namespace web{struct WebResponse{int status;matjson::Value body;int code(){return status;}struct Parsed{matjson::Value body;auto unwrapOr(matjson::Value){return body;}};auto json(){return Parsed{body};}};
struct Operation{std::string path;matjson::Value body;};struct WebRequest{matjson::Value body;WebRequest&timeout(std::chrono::seconds s){assert(s.count()==8);return*this;}WebRequest&followRedirects(bool b){assert(!b);return*this;}void bodyJSON(matjson::Value const&v){body=v;}Operation post(std::string path){return {path,body};}};}
namespace geode{namespace prelude{using::Ref;using::WeakRef;namespace web=::web;}namespace async{struct Job{web::Operation op;std::function<void(web::WebResponse)>callback;};inline std::vector<Job>jobs;template<class T>struct TaskHolder{int cancels=0;void cancel(){++cancels;}template<class F>void spawn(web::Operation op,F callback){jobs.push_back({op,callback});}};}}
namespace corum::ranked::ui{enum class FontWeight{ExtraBold,Black,SemiBold};constexpr int Cyan=1,Blue=2,Surface=3,SurfaceRaised=4,Text=5,TextMuted=6;
namespace i18n{std::string text(char const*,char const*f){return f;}}
struct Layout{CCSize size;};Layout rankedPopupLayout(CCSize size,CCSize){return {size};}CCNode*rankedPopupContent(CCNode*,CCMenu*,Layout){return new CCNode;}
CCNode*surface(CCSize,CCPoint,int,int,int,bool){return new CCNode;}
CCLabelBMFont*menuLabel(std::string const&,float,CCPoint,int,FontWeight){return new CCLabelBMFont;}
void fitSingleLineMenuLabel(CCLabelBMFont*,float,float,float){}
}
${source}
int main(){using namespace corum::ranked::ui;
 std::vector<std::string>chosen;auto create=[&]{return RankedUpdateChannelPopup::create([&](std::string s){chosen.push_back(s);});};
 auto*p=create();assert(p->m_password->masked&&p->m_password->limit==128&&!p->m_password->visible);p->onRelease(nullptr);assert(chosen==std::vector<std::string>({"release"})&&p->m_closed&&p->refs==0);p->onRelease(nullptr);assert(chosen.size()==1);
 p=create();p->onDebug(nullptr);assert(p->m_password->visible&&p->m_confirm->visible);auto before=geode::async::jobs.size();p->onConfirm(nullptr);assert(geode::async::jobs.size()==before);
 for(auto bad: {matjson::parse(R"({"authorized":false,"channel":"debug"})").unwrap(),matjson::parse(R"({"authorized":true,"channel":"release"})").unwrap(),matjson::Value()}){
  p->m_password->value="fixture-password";p->onConfirm(nullptr);assert(p->m_password->value.empty()&&p->m_busy&&!p->m_release->enabled&&!p->m_password->enabled);
  assert(geode::async::jobs.back().op.path=="https://api.52-74-37-130.sslip.io/api/ranked/updates/channel/debug");assert(geode::async::jobs.back().op.body["password"].asString().unwrap()=="fixture-password");
  before=geode::async::jobs.size();p->onRelease(nullptr);p->onConfirm(nullptr);assert(geode::async::jobs.size()==before&&chosen.size()==1);
  geode::async::jobs.back().callback({200,bad});assert(!p->m_busy&&!p->m_closed&&!p->m_status->value.empty()&&chosen.size()==1);
 }
 auto good=matjson::parse(R"({"authorized":true,"channel":"debug"})").unwrap();p->m_password->value="fixture-password";p->onConfirm(nullptr);geode::async::jobs.back().callback({403,good});assert(chosen.size()==1&&!p->m_closed);
 p->m_password->value="fixture-password";p->onConfirm(nullptr);auto callback=geode::async::jobs.back().callback;callback({200,good});assert(chosen.back()=="debug"&&chosen.size()==2&&p->m_closed);callback({200,good});assert(chosen.size()==2);
 p=create();p->onDebug(nullptr);p->m_password->value="fixture-password";p->onConfirm(nullptr);callback=geode::async::jobs.back().callback;p->dismiss();callback({200,good});assert(chosen.size()==2&&p->m_password->value.empty()&&p->m_request.cancels>0);
 auto releases=CCTouchDispatcher::get()->releases;p->onExit();assert(CCTouchDispatcher::get()->releases==releases);
 p=create();p->onDebug(nullptr);p->m_password->value="fixture-password";p->onConfirm(nullptr);callback=geode::async::jobs.back().callback;p->parent=false;callback({200,good});assert(chosen.size()==2);p->onExit();assert(!p->m_choose&&p->m_password->value.empty());
}
`);
});
