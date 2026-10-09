import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const read=p=>readFileSync(new URL(p,import.meta.url),'utf8');
const strip=s=>s.replace(/^#include .*?$|^#pragma once$/gm,'');
const viewer=read('SpectatorLiveViewerNativeTest.mjs');
const json=viewer.slice(viewer.indexOf('namespace matjson {'),viewer.indexOf('${readFileSync(join(root,'));
const fixture=`
${read('helpers/ResultPresentationNativeDouble.hpp').replace(/^#pragma once$/gm,'').replace('struct CCMenuItemSpriteExtra : CCNode {','struct CCMenuItem : CCNode {bool enabled=true;void setEnabled(bool value){enabled=value;}};\nstruct CCMenuItemSpriteExtra : CCMenuItem {').replace('bool enabled=true; void setEnabled(bool value)', 'void setEnabled(bool value)').replace('std::string id; float','int tag=0;void setTag(int value){tag=value;}int getTag()const{return tag;}\n    auto* getChildren(){return &children;}void schedule(int,float){scheduled=true;}void unschedule(int){scheduled=false;}\n    std::string id; float')}
#include <chrono>
#include <memory>
#include <charconv>
#include <cstdint>
#include <map>
#include <variant>
${json}
#define schedule_selector(x) 0
namespace cocos2d{using ::CCSize;using ::ccColor3B;}
template<class T>T typeinfo_cast(CCObject* n){return dynamic_cast<T>(n);}
template<class T>struct CCArrayExt{std::vector<CCNode*>* nodes;explicit CCArrayExt(std::vector<CCNode*>*n):nodes(n){}auto begin(){return nodes->begin();}auto end(){return nodes->end();}};
struct CCTouchDispatcher{int releases=0;static CCTouchDispatcher* get(){static CCTouchDispatcher d;return &d;}void unregisterForcePrio(CCNode*){++releases;}};
struct Popup:CCNode{CCNode* m_bgSprite=new CCNode;CCNode* m_closeBtn=new CCNode;CCNode* m_mainLayer=new CCNode;
 inline static std::vector<Popup*> opened;virtual bool init(){return true;}bool init(float,float){return true;}
 virtual void onClose(CCObject*){removeFromParentAndCleanup(true);}void show(){static CCNode scene;scene.addChild(this);opened.push_back(this);}};
struct TextInput:CCNode{std::string value;static TextInput* create(float,char const*){return new TextInput;}
 void setPasswordMode(bool){}void setMaxCharCount(int){}void setFilter(std::string){}auto getString()const{return value;}void setString(std::string s){value=std::move(s);}};
struct FLAlertLayer;
struct FLAlertLayerProtocol{virtual ~FLAlertLayerProtocol()=default;virtual void FLAlert_Clicked(FLAlertLayer*,bool)=0;};
struct FLAlertLayer:CCNode{FLAlertLayerProtocol* m_alertProtocol=nullptr;inline static int summaries=0,confirmations=0;
 static FLAlertLayer* create(char const*,std::string,char const*){++summaries;return new FLAlertLayer;}
 static FLAlertLayer* create(FLAlertLayerProtocol* p,char const*,char const*,char const*,char const*,int){++confirmations;auto* a=new FLAlertLayer;a->m_alertProtocol=p;return a;}
 void show(){static CCNode scene;scene.addChild(this);}void onBtn1(CCObject*){removeFromParentAndCleanup(true);}};
struct GJAccountManager{int m_accountID=1;std::string m_username="hwanhee1";static GJAccountManager* get(){static GJAccountManager a;return &a;}};
namespace geode::utils::web{
struct WebResponse{matjson::Value root;int status=201;int code()const{return status;}matjson::Result<matjson::Value> json()const{return {root};}};
struct Operation{std::string url,token;matjson::Value body;};
struct WebRequest{std::string token;matjson::Value body;void header(char const*,std::string s){token=std::move(s);}void bodyJSON(matjson::Value v){body=std::move(v);}
 void followRedirects(bool){}void timeout(std::chrono::seconds){}Operation post(std::string s){return {s,token,body};}};
struct Job{Operation op;std::function<void(WebResponse)> callback;bool cancelled=false;};inline std::vector<std::shared_ptr<Job>> jobs;
}
namespace web=geode::utils::web;
namespace geode::async{template<class T>struct TaskHolder{std::shared_ptr<web::Job> job;
 void cancel(){if(job)job->cancelled=true;}template<class F>void spawn(web::Operation op,F f){job=std::make_shared<web::Job>(web::Job{op,f,false});web::jobs.push_back(job);}};}
namespace async=geode::async;
namespace corum::ranked{enum class RuntimeStage{Ready,Matched,Error};
struct RankedRuntime{struct View{RuntimeStage stage=RuntimeStage::Ready;bool accountActiveElsewhere=true;}v;std::string token="session";int reloads=0,takeovers=0;
 static RankedRuntime& get(){static RankedRuntime r;return r;}auto const& view()const{return v;}std::string spectatorLiveSessionToken()const{return token;}
 void begin(){++reloads;}void takeOverSession(){++takeovers;v.accountActiveElsewhere=false;}};
inline int bots=0;void showDebugBotPasswordPopup(){++bots;}
namespace i18n{std::string text(char const*,char const* english){return english;}}
namespace ui{inline constexpr ccColor3B Red{255,60,60},Blue{80,120,220},Cyan{64,224,255},Surface{8,15,25},SurfaceRaised{18,25,40},Text{230,235,244},TextMuted{150,150,150};
 enum class FontWeight{SemiBold,ExtraBold,Black};CCNode* surface(CCSize s,CCPoint p,ccColor3B,ccColor3B,int,bool){auto* n=CCNode::create();n->size=s;n->position=p;return n;}
 CCLabelBMFont* menuLabel(std::string s,float scale,CCPoint p,ccColor3B color,FontWeight=FontWeight::SemiBold){auto* n=new CCLabelBMFont;n->setString(s.c_str());n->setScale(scale);n->position=p;n->color=color;return n;}
 void fitMenuLabelWidth(CCLabelBMFont*,float,float,float){}void fitFullMenuLabel(CCLabelBMFont*,float,float,float){}
 void grayButtonVisual(CCNode* node){node->color={100,100,100};}void cancelMenuTouch(CCMenu* menu){++menu->cancellations;}
}
namespace debug{inline int spectates=0;void showDebugSpectatePopup(){++spectates;}void showDebugAdminPopup();}
}
`;
function run(code,flags=[]){const dir=mkdtempSync(join(tmpdir(),'corum-admin-native-'));try{writeFileSync(join(dir,'test.cpp'),code);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',...flags,join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}
test('whole debug/Admin popups compile ON/OFF and guard names, inputs, confirmation, late callbacks and batch summary',()=>{
 const source=strip(read('../src/debug/DebugAdminPopup.cpp')).replace('class DebugAdminPopup final : public Popup, public FLAlertLayerProtocol {','class DebugAdminPopup final : public Popup, public FLAlertLayerProtocol { public:');
 const menu=strip(read('../src/debug/DebugMenuPopup.cpp')).replace('class DebugMenuPopup final : public Popup {','class DebugMenuPopup final : public Popup { public:');
 run(fixture+source+menu+`
using namespace corum::ranked;using namespace corum::ranked::debug;
int main(){
 auto& runtime=RankedRuntime::get();auto* account=GJAccountManager::get();
 account->m_username="other";assert(!DebugAdminPopup::create());auto* denied=DebugMenuPopup::create();
 auto* actions=dynamic_cast<CCMenu*>(denied->m_mainLayer->children[0]->children.back());assert(actions&&actions->children.size()==3);
 assert(!dynamic_cast<CCMenuItem*>(actions->children.back())->enabled);
 account->m_username="hwanhee1";auto* admin=DebugAdminPopup::create();assert(admin);admin->show();
 admin->execute(1,false);assert(web::jobs.empty());admin->m_password->setString("password");
 for(auto value:{"","-1","1.5","2147483648"}){admin->m_lp->setString(value);admin->execute(1,false);}assert(web::jobs.empty());
 admin->m_lp->setString("300");admin->m_mmr->setString("500");admin->execute(3,false);
 assert(web::jobs.size()==1&&admin->m_busy);auto job=web::jobs.back();assert(job->op.token=="Bearer session");
 assert(job->op.body["action"].asString().unwrapOr("")=="SET_BOTH"&&job->op.body["lp"].asInt().unwrapOr(0)==300);
 matjson::Value fail;fail["message"]="Forbidden";job->callback({fail,403});assert(!admin->m_busy&&admin->m_status->text=="Forbidden");
 admin->execute(6,false);assert(web::jobs.size()==1);CCMenuItemSpriteExtra reset;reset.setTag(6);admin->onAction(&reset);
 assert(FLAlertLayer::confirmations==1&&web::jobs.size()==1);admin->FLAlert_Clicked(nullptr,false);assert(web::jobs.size()==1);
 admin->onAction(&reset);admin->FLAlert_Clicked(nullptr,true);assert(web::jobs.size()==2);
 assert(web::jobs.back()->op.body["confirmation"].asString().unwrapOr("")=="RESET");
 matjson::Value good;good["message"]="Changes saved.";web::jobs.back()->callback({good,201});
 assert(admin->m_closed&&admin->parent&&runtime.reloads==0&&FLAlertLayer::summaries==0);drainMainQueue();
 assert(!admin->parent&&runtime.reloads==1&&FLAlertLayer::summaries==1&&admin->m_password->getString().empty());
 auto* stale=DebugAdminPopup::create();stale->show();stale->m_password->setString("password");stale->execute(0,false);job=web::jobs.back();
 stale->dismiss();drainMainQueue();assert(job->cancelled);job->callback({good,201});assert(mainQueue.empty()&&runtime.reloads==1);
 auto* foreign=DebugAdminPopup::create();foreign->show();foreign->m_password->setString("password");foreign->execute(4,false);job=web::jobs.back();
 runtime.token="replacement";job->callback({good,201});assert(mainQueue.empty());foreign->refresh(0);drainMainQueue();
 runtime.v.stage=RuntimeStage::Matched;assert(!DebugAdminPopup::create());runtime.v.stage=RuntimeStage::Ready;
 auto* debug=DebugMenuPopup::create();debug->show();CCMenuItemSpriteExtra first;first.setTag(0);debug->onSelect(&first);
 assert(bots==0&&debug->parent);drainMainQueue();assert(bots==1&&!debug->parent);
}
`,['-DCORUM_RANKED_DEBUG_BOT_MATCH=1','-DCORUM_RANKED_DEBUG_SPECTATE=1']);
 run(strip(read('../src/debug/DebugAdminPopup.cpp'))+strip(read('../src/debug/DebugMenuPopup.cpp'))+'\nint main(){}');
});
test('whole session conflict popup needs an explicit red-button press and retires before takeover',()=>{
 const source=strip(read('../src/ui/RankedSessionConflictPopup.hpp')).replace('class RankedSessionConflictPopup final : public Popup {','class RankedSessionConflictPopup final : public Popup { public:');
 run(fixture+source+`
using namespace corum::ranked;using namespace corum::ranked::ui;
int main(){
 auto& runtime=RankedRuntime::get();auto* popup=RankedSessionConflictPopup::create();assert(popup);popup->show();
 auto* button=dynamic_cast<CCMenuItemSpriteExtra*>(popup->m_actions->children[0]);assert(button&&button->id=="ranked-session-takeover-button");
 auto* text=dynamic_cast<CCLabelBMFont*>(button->getNormalImage()->children[0]);assert(text&&text->color==Red);
 popup->dismiss();assert(runtime.takeovers==0&&popup->parent);drainMainQueue();assert(!popup->parent&&runtime.takeovers==0);
 popup=RankedSessionConflictPopup::create();popup->show();popup->onTakeOver(nullptr);popup->onTakeOver(nullptr);
 assert(runtime.takeovers==0&&popup->parent&&mainQueue.size()==1);drainMainQueue();assert(runtime.takeovers==1&&!popup->parent);
 runtime.v.accountActiveElsewhere=true;popup=RankedSessionConflictPopup::create();popup->show();runtime.v.accountActiveElsewhere=false;
 popup->refresh(0);drainMainQueue();assert(!popup->parent&&runtime.takeovers==1);
}
`);
});
