// Compile real lobby state/actions with Cocos boundaries. No device rendering claim.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
const strip=s=>s.replace(/^#include .*?$|^#pragma once$/gm,'');
const fadeSource=()=>strip(read('src/ui/ModeLobbyMotion.hpp')).split('/** One action')[0]+'\n} // namespace corum::ranked::ui';
function method(source,signature){const start=source.indexOf(signature);assert(start>=0,signature);let end=source.indexOf('{',start)+1,depth=1;while(depth){if(source[end]==='{')++depth;if(source[end]==='}')--depth;++end;}return source.slice(start,end);}
function run(cpp){const dir=mkdtempSync(join(tmpdir(),'corum-mode-lobby-'));try{writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}catch(error){throw new Error(error.stderr?.toString()||error.message);}finally{rmSync(dir,{recursive:true,force:true});}}

test('mode state rejects rapid clicks/Back, recovers Solo, and fits cards on varied aspect ratios',()=>run(`
#include <cassert>
#include "domain/RankedMode.hpp"
#include "domain/AchievementPresentation.hpp"
int main(){using namespace corum::ranked;
 for(auto mode:{RankedMode::Survival,RankedMode::SoloRanked}){
  ModeLobbyTransition state;assert(!state.back());assert(state.select(mode));
  for(int i=0;i<30;++i){assert(!state.select(RankedMode::Survival)&&!state.select(RankedMode::SoloRanked)&&!state.back());}
  state.finish();assert(state.selected()==mode&&!state.moving());assert(!state.select(mode));assert(state.back());
  for(int i=0;i<30;++i){assert(!state.back()&&!state.select(mode));}state.finish();assert(!state.selected());
  assert(state.select(mode));state.finish();state.recoverSolo();assert(state.selected()==RankedMode::SoloRanked);
 }
 for(auto size:{std::pair{480.f,320.f},std::pair{568.f,320.f},std::pair{800.f,450.f},std::pair{430.f,300.f},std::pair{1024.f,768.f}}){
  auto l=modeLobbyLayout(size.first,size.second);assert(l.scale>0&&l.scale<=1);
  assert(l.leftX-94*l.scale>=20&&l.rightX+94*l.scale<=size.first-20);
  assert(l.centerY-105*l.scale>=44-.001f&&l.centerY+123*l.scale<=size.second-36+.001f);
  if(size.first>=480&&size.second>=320)assert(l.scale==1);
 }
 struct Card{std::string scope;int claimableRewards;};
 std::vector<Card> cards={{"SOLO_RANKED",0},{"SHARED",1},{"SOLO_RANKED",2},{"SHARED",0}};
 assert((achievementDisplayOrderForScope(cards,"SOLO_RANKED")==std::vector<std::size_t>{2,0}));
 assert((achievementDisplayOrderForScope(cards,"SHARED")==std::vector<std::size_t>{1,3}));
 assert(achievementDisplayOrderForScope(cards,"SURVIVAL").empty());
 assert(achievementScopeName("SHARED",true)=="공통"&&achievementScopeName("SOLO_RANKED",false)=="Solo Ranked");
}`));

test('real subtree fade crosses plain panel nodes and preserves local alpha and badge pulses',()=>run(`
#include <cassert>
#include <cmath>
#include <vector>
#include <algorithm>
using GLubyte=unsigned char;
namespace cocos2d {
struct CCNode{virtual ~CCNode()=default;std::vector<CCNode*> children;auto*getChildren(){return &children;}void addChild(CCNode*n){children.push_back(n);}};
struct CCRGBAProtocol{virtual ~CCRGBAProtocol()=default;virtual void updateDisplayedOpacity(GLubyte)=0;virtual bool isCascadeOpacityEnabled()=0;virtual GLubyte getDisplayedOpacity()=0;};
struct CCLayerColor:CCNode,CCRGBAProtocol{int real=255,displayed=255;bool cascade=false;void setOpacity(GLubyte v){real=v;}GLubyte getOpacity(){return real;}void updateDisplayedOpacity(GLubyte v)override{displayed=std::lround(real*v/255.f);}bool isCascadeOpacityEnabled()override{return cascade;}GLubyte getDisplayedOpacity()override{return displayed;}};
struct CCActionInterval{virtual ~CCActionInterval()=default;bool initWithDuration(float){return true;}void autorelease(){}virtual void startWithTarget(CCNode*){}virtual void update(float){}};
}
namespace geode::cocos{template<class T>auto const&CCArrayExt(std::vector<cocos2d::CCNode*>*v){return *v;}}
${fadeSource()}
int main(){using namespace cocos2d;using namespace corum::ranked::ui;
 CCLayerColor group,shadow,pulse,disabled,caption;CCNode panel,badge;
 group.addChild(&panel);panel.addChild(&shadow);panel.addChild(&badge);badge.addChild(&pulse);
 shadow.real=82;pulse.real=100;disabled.real=100;disabled.cascade=true;caption.real=200;panel.addChild(&disabled);disabled.addChild(&caption);
 auto*fade=ModeLobbyFade::create(.36f,0);fade->startWithTarget(&group);fade->update(.5f);
 assert(group.real==128&&shadow.displayed==41&&pulse.displayed==50);
 assert(disabled.displayed==50&&caption.displayed==39&&caption.real==200);
 fade->update(1);assert(shadow.displayed==0&&pulse.displayed==0&&pulse.real==100);
 pulse.setOpacity(60);auto*reverse=ModeLobbyFade::create(.36f,255);reverse->startWithTarget(&group);reverse->update(.5f);
 assert(pulse.displayed==30&&pulse.real==60);reverse->update(1);assert(shadow.displayed==82&&pulse.displayed==60);
 delete fade;delete reverse;
}`));

test('real badge containers restore every tier sprite after another mode card fades over them',()=>run(`
#include <cassert>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
using GLubyte=unsigned char;
struct ccColor4B{GLubyte r,g,b,a;};
namespace cocos2d {
struct CCSize{float width=568,height=320;};inline constexpr CCSize CCSizeZero{0,0};
struct CCNode{virtual ~CCNode()=default;std::vector<CCNode*> children;auto*getChildren(){return &children;}void addChild(CCNode*n,int=0){children.push_back(n);}};
struct CCRGBAProtocol{virtual ~CCRGBAProtocol()=default;virtual void updateDisplayedOpacity(GLubyte)=0;virtual bool isCascadeOpacityEnabled()=0;virtual GLubyte getDisplayedOpacity()=0;};
struct CCLayerColor:CCNode,CCRGBAProtocol{
 int real=255,displayed=255;bool cascade=false,colorCascade=false;ccColor4B color{255,255,255,255};CCSize size;
 static auto*create(ccColor4B c){auto*n=new CCLayerColor;n->color=c;n->real=n->displayed=c.a;return n;}
 void setContentSize(CCSize s){size=s;}void setCascadeColorEnabled(bool b){colorCascade=b;}void setCascadeOpacityEnabled(bool b){cascade=b;}
 void setOpacity(GLubyte v){real=v;}GLubyte getOpacity(){return real;}void updateDisplayedOpacity(GLubyte v)override{displayed=std::lround(real*v/255.f);}
 bool isCascadeOpacityEnabled()override{return cascade;}GLubyte getDisplayedOpacity()override{return displayed;}
};
struct CCActionInterval{virtual ~CCActionInterval()=default;bool initWithDuration(float){return true;}void autorelease(){}virtual void startWithTarget(CCNode*){}virtual void update(float){}};
}
using namespace cocos2d;
namespace geode::cocos{template<class T>auto const&CCArrayExt(std::vector<cocos2d::CCNode*>*v){return *v;}}
${method(read('src/ui/RankBadgeAssets.cpp'),'CCLayerColor* centeredRoot()')}
${fadeSource()}
int main(){using namespace corum::ranked::ui;
 for(auto const& tier:{"UNRANKED","RED","AQUA","BRONZE","SILVER","GOLD"}){
  (void)tier;CCLayerColor solo,survival,body,halo;halo.real=100;CCNode panel;
  auto* badge=centeredRoot();auto* motion=centeredRoot();auto* backdrop=centeredRoot();
  solo.addChild(&panel);panel.addChild(badge);badge->addChild(motion);motion->addChild(&body);motion->addChild(backdrop);backdrop->addChild(&halo);
  for(auto* container:{badge,motion,backdrop}){
   assert(container->size.width==0&&container->size.height==0);
   assert(container->color.r==255&&container->color.g==255&&container->color.b==255&&container->colorCascade);
  }
  auto* hide=ModeLobbyFade::create(.36f,0);hide->startWithTarget(&solo);hide->update(1);
  assert(body.displayed==0&&halo.displayed==0&&survival.displayed==255);
  auto* show=ModeLobbyFade::create(.36f,255);show->startWithTarget(&solo);show->update(.5f);
  assert(body.displayed==128&&halo.displayed==50&&body.real==255&&halo.real==100);
  show->update(1);assert(body.displayed==255&&halo.displayed==100);
  // Full UI fade/re-parenting followed by another select/Back cycle must still
  // leave the main tier sprite visible, not just its animated additive halo.
  setModeLobbyOpacity(&solo,0);setModeLobbyOpacity(&solo,255);
  assert(body.displayed==255&&halo.displayed==100&&survival.displayed==255);
  delete hide;delete show;delete backdrop;delete motion;delete badge;
 }
}`));

test('actual lobby action callbacks move cards continuously and reverse the same path with input locked',()=>{
 const popup=read('src/RankedPopup.cpp');
 run(`
#include <cassert>
#include <cmath>
#include <functional>
#include <vector>
#include <string>
#include "domain/RankedMode.hpp"
using GLubyte=unsigned char;
namespace cocos2d {
struct CCPoint{float x=0,y=0;};struct CCSize{float width=480,height=320;};
struct CCActionInterval;
struct CCNode{virtual ~CCNode()=default;std::string id;std::vector<CCNode*> children;std::vector<CCActionInterval*> actions;CCPoint position;float sx=1,sy=1;bool visible=true;
 auto*getChildren(){return &children;}CCNode*getChildByID(std::string const&s){for(auto*n:children)if(n->id==s)return n;return nullptr;}CCNode*getParent(){return this;}void setVisible(bool v){visible=v;}void setZOrder(int){}void setPosition(CCPoint p){position=p;}void setScale(float s){sx=sy=s;}void runAction(CCActionInterval*);void step(float);};
struct CCRGBAProtocol{virtual ~CCRGBAProtocol()=default;virtual void updateDisplayedOpacity(GLubyte)=0;virtual bool isCascadeOpacityEnabled()=0;virtual GLubyte getDisplayedOpacity()=0;};
struct CCLayerColor:CCNode,CCRGBAProtocol{int real=255,displayed=255;void setOpacity(GLubyte v){real=v;}GLubyte getOpacity(){return real;}void updateDisplayedOpacity(GLubyte v)override{displayed=std::lround(real*v/255.f);}bool isCascadeOpacityEnabled()override{return false;}GLubyte getDisplayedOpacity()override{return displayed;}};
struct CCMenu:CCNode{bool touch=true;int cancels=0;void setTouchEnabled(bool v){touch=v;}};
struct CCActionInterval{CCNode*target=nullptr;virtual ~CCActionInterval()=default;bool initWithDuration(float){return true;}void autorelease(){}virtual void startWithTarget(CCNode*t){target=t;}virtual void update(float){}};
void CCNode::runAction(CCActionInterval*a){actions.push_back(a);a->startWithTarget(this);}void CCNode::step(float t){auto copy=actions;for(auto*a:copy)a->update(t);if(t==1){actions.clear();for(auto*a:copy)delete a;}}
struct CCMoveTo:CCActionInterval{CCPoint from,to;static auto*create(float,CCPoint p){auto*a=new CCMoveTo;a->to=p;return a;}void startWithTarget(CCNode*t)override{CCActionInterval::startWithTarget(t);from=t->position;}void update(float t)override{target->position={from.x+(to.x-from.x)*t,from.y+(to.y-from.y)*t};}};
struct CCScaleTo:CCActionInterval{float fx=0,fy=0,x=0,y=0;static auto*create(float,float x,float y){auto*a=new CCScaleTo;a->x=x;a->y=y;return a;}void startWithTarget(CCNode*t)override{CCActionInterval::startWithTarget(t);fx=t->sx;fy=t->sy;}void update(float t)override{target->sx=fx+(x-fx)*t;target->sy=fy+(y-fy)*t;}};
struct CCEaseSineInOut:CCActionInterval{CCActionInterval*inner=nullptr;~CCEaseSineInOut(){delete inner;}static auto*create(CCActionInterval*a){auto*e=new CCEaseSineInOut;e->inner=a;return e;}void startWithTarget(CCNode*t)override{inner->startWithTarget(t);}void update(float t)override{inner->update((1-std::cos(t*3.14159265f))/2);}};
struct CCDelayTime:CCActionInterval{static auto*create(float){return new CCDelayTime;}};
struct CallFuncExt:CCActionInterval{std::function<void()>fn;template<class F>static auto*create(F fn){auto*a=new CallFuncExt;a->fn=fn;return a;}void update(float t)override{if(t==1)fn();}};
struct CCSequence:CCActionInterval{CCActionInterval*delay=nullptr,*call=nullptr;~CCSequence(){delete delay;delete call;}static auto*create(CCActionInterval*d,CCActionInterval*c,std::nullptr_t){auto*a=new CCSequence;a->delay=d;a->call=c;return a;}void startWithTarget(CCNode*t)override{delay->startWithTarget(t);call->startWithTarget(t);}void update(float t)override{delay->update(t);call->update(t);}};
struct CCDirector{CCSize size;static auto*sharedDirector(){static CCDirector d;return &d;}CCSize getWinSize(){return size;}};
}
using namespace cocos2d;
std::string operator""_spr(char const*s,std::size_t n){return {s,n};}
template<class T>auto const&CCArrayExt(std::vector<CCNode*>*v){return *v;}template<class T>T typeinfo_cast(CCNode*n){return dynamic_cast<T>(n);}template<class T>struct WeakRef{T*ptr;WeakRef(T*p):ptr(p){}T*lock()const{return ptr;}};
using CCAction=CCActionInterval;using CCEaseSineIn=CCEaseSineInOut;using CCEaseSineOut=CCEaseSineInOut;
namespace geode{template<class T>struct Ref{T*ptr=nullptr;Ref()=default;explicit Ref(T*p):ptr(p){}Ref&operator=(T*p){ptr=p;return *this;}T*data()const{return ptr;}T*operator->()const{return ptr;}explicit operator bool()const{return ptr!=nullptr;}};}
namespace geode::cocos{template<class T>auto const&CCArrayExt(std::vector<CCNode*>*v){return *v;}}
using geode::Ref;
namespace corum::ranked::ui{void cancelMenuTouch(CCMenu*m){++m->cancels;}void fadeModeLobbySurfaces(CCNode*,float,bool){}struct LobbyHover{void setActive(bool){}};}
${strip(read('src/ui/ModeLobbyMotion.hpp'))}
enum class RuntimeStage{Ready,Queued,Matched};struct RuntimeView{RuntimeStage stage;};
struct Owner:CCNode{
 corum::ranked::ModeLobbyTransition m_modeTransition;CCLayerColor survival,solo,panel,survivalControls,soloControls;
 CCLayerColor *m_lobbySurvival=&survival,*m_lobbySolo=&solo,*m_lobbyPanel=&panel,*m_lobbyContents=nullptr;
 corum::ranked::ui::LobbyHover*m_lobbyHover=nullptr;void refreshLobbyHover(){}
 CCMenu survivalMenu,soloMenu,panelMenu,menu,settings;
 CCMenu toolbar;CCMenu*m_menu=&menu,*m_settingsMenu=&settings,*m_lobbyToolbar=&toolbar;bool m_uiFadingOut=false,m_returnToModesAfterQueue=false,m_modeNoticeOpen=false;std::string m_renderedUiKey;
 enum class Page{Live,History};Page m_page=Page::Live;
 int headerUpdates=0;std::string presentationKey(){return "selected";}void updateModeLobbyHeader(){++headerUpdates;}
 Owner(){survivalControls.id=soloControls.id="ranked-mode-entry-controls";survivalControls.children={&survivalMenu};soloControls.children={&soloMenu};survival.children={&survivalControls};solo.children={&soloControls};panel.children={&panelMenu};}
 ${method(popup,'    void setLobbyNodeInput(')}
 ${method(popup,'    void syncLobbyInput(')}
 ${method(popup,'    void animateModeLobby(')}
 ${method(popup,'    void resumeModeSelectionAfterQueue(')}
 void step(float t){survival.step(t);solo.step(t);panel.step(t);survivalControls.step(t);soloControls.step(t);}
};
int main(){using namespace corum::ranked;
 for(auto mode:{RankedMode::Survival,RankedMode::SoloRanked}){
  Owner o;auto l=modeLobbyLayout(480,320);o.survival.position={l.leftX,l.centerY};o.solo.position={l.rightX,l.centerY};
  o.panel.sx=o.panel.sy=l.scale;o.panel.real=0;o.panel.visible=false;
  assert(o.m_modeTransition.select(mode));o.animateModeLobby(mode,true);
  assert(!o.menu.touch&&!o.settings.touch&&!o.survivalMenu.touch&&!o.soloMenu.touch&&!o.panelMenu.touch);
  assert(!o.toolbar.touch);
  assert(o.solo.position.x==l.rightX&&o.survival.position.x==l.leftX&&o.panel.sx==l.scale);
  o.step(.25f);float quarterX=o.solo.position.x,quarterSurvivalX=o.survival.position.x,quarterPanelX=o.panel.position.x;
  int quarterSurvivalAlpha=o.survival.real,quarterMenuAlpha=o.panel.real;
  assert(o.panel.sx==l.scale&&o.panel.position.x>l.leftX-94*l.scale&&o.panel.position.x<l.rightX-94*l.scale);
  if(mode==RankedMode::SoloRanked)assert(o.survival.position.x>l.leftX&&o.survival.real==255&&o.panel.real==0);
  else assert(o.survival.position.x==l.leftX&&o.panel.real==255);
  if(mode==RankedMode::SoloRanked)assert(quarterX<l.rightX&&quarterX>l.leftX);else assert(quarterX==l.rightX);
  assert(!o.m_modeTransition.back()&&!o.m_modeTransition.select(mode));
  o.step(.5f);assert(!o.panelMenu.touch);
  if(mode==RankedMode::SoloRanked){assert(std::abs(o.solo.position.x-o.survival.position.x)<.001f);assert(o.survival.real>=127&&o.survival.real<=128&&o.panel.real>=127&&o.panel.real<=128);}
  o.step(1);
  assert(!o.m_modeTransition.moving()&&o.menu.touch&&o.panelMenu.touch&&!o.survivalMenu.touch&&!o.soloMenu.touch);
  assert(o.panel.sx==l.scale&&o.panel.real==255);
  auto& controls=mode==RankedMode::Survival?o.survivalControls:o.soloControls;
  assert(controls.real==0&&!controls.visible&&o.toolbar.touch);
  if(mode==RankedMode::SoloRanked)assert(o.solo.position.x==l.leftX&&!o.survival.visible);else assert(!o.solo.visible);
  assert(o.m_modeTransition.back());o.animateModeLobby(mode,false);assert(!o.menu.touch&&!o.panelMenu.touch);
  o.step(.75f);assert(std::abs(o.solo.position.x-quarterX)<.001f&&std::abs(o.panel.position.x-quarterPanelX)<.001f&&std::abs(o.survival.position.x-quarterSurvivalX)<.001f&&o.survival.real==quarterSurvivalAlpha&&o.panel.real==quarterMenuAlpha);
  o.step(1);assert(!o.m_modeTransition.selected()&&!o.m_modeTransition.moving()&&!o.panel.visible);
  assert(o.solo.position.x==l.rightX&&o.survival.position.x==l.leftX&&o.solo.visible&&o.survival.visible);
  assert(o.menu.touch&&o.survivalMenu.touch&&o.soloMenu.touch&&!o.panelMenu.touch&&o.headerUpdates==2);
  assert(controls.real==255&&controls.visible&&o.toolbar.touch);
 }
 Owner o;auto l=modeLobbyLayout(480,320);o.survival.position={l.leftX,l.centerY};o.solo.position={l.rightX,l.centerY};o.panel.sx=l.scale;
 assert(o.m_modeTransition.select(RankedMode::SoloRanked));o.animateModeLobby(RankedMode::SoloRanked,true);o.step(1);
 o.m_returnToModesAfterQueue=true;o.resumeModeSelectionAfterQueue({RuntimeStage::Queued});assert(!o.m_modeTransition.moving());
 o.m_uiFadingOut=true;o.resumeModeSelectionAfterQueue({RuntimeStage::Ready});assert(o.m_returnToModesAfterQueue);
 o.m_uiFadingOut=false;o.resumeModeSelectionAfterQueue({RuntimeStage::Ready});assert(!o.m_returnToModesAfterQueue&&o.m_modeTransition.moving());
 o.resumeModeSelectionAfterQueue({RuntimeStage::Ready});assert(o.panel.actions.size()==1);o.step(1);assert(!o.m_modeTransition.selected());
 assert(o.m_modeTransition.select(RankedMode::SoloRanked));o.animateModeLobby(RankedMode::SoloRanked,true);o.step(1);
 o.m_returnToModesAfterQueue=true;o.resumeModeSelectionAfterQueue({RuntimeStage::Matched});
 assert(!o.m_returnToModesAfterQueue&&!o.m_modeTransition.moving()&&o.m_modeTransition.selected()==RankedMode::SoloRanked);
}
 `);
});

test('mode lobby owns Cocos actions and defers full UI replacement while moving',()=>{
 const popup=read('src/RankedPopup.cpp');
 const animate=method(popup,'    void animateModeLobby(');
 assert.match(animate,/ModeLobbySlide::create\(duration,m_lobbySurvival,m_lobbySolo,layout,mode,opening\)/);
 assert.match(animate,/CCEaseSineInOut::create\(motion\)/);
 assert.doesNotMatch(animate,/CCScaleTo|CCMoveTo|removeAllChildren/);
 assert.match(animate,/setZOrder\(5\)/);
 assert.match(method(popup,'    bool prepareUiTransition('),/if \(m_modeTransition.moving\(\)\).*return false/);
 assert.match(method(popup,'    void render()'),/m_modeTransition.moving\(\)/);
 assert.match(method(popup,'    void onCloseLayer('),/m_modeTransition.back\(\).*animateModeLobby\(mode,false\)/);
 const card=method(popup,'    void renderModeCard(');
 assert.match(card,/hitArea->setContentSize\(\{188,210\}\)/);
 assert.match(card,/select->setEnabled\(source.stage == RuntimeStage::Ready\)/);
 assert.match(card,/survival-card-select/);assert.match(card,/solo-ranked-card-select/);
 assert.doesNotMatch(card,/survival-entry|solo-ranked-entry|mode-browse/);
 const panel=method(popup,'    void renderModePanel(');
 assert.match(card,/makeNeonPanel\(\{188.0f, 210.0f\}/);
 assert.match(panel,/makeNeonPanel\(\{188.0f, 210.0f\}/);
 assert.match(panel,/makeNeonPanel\(\{188,210\}/);
 assert.match(panel,/rightCenter.x,rightCenter.y \+ 80.0f/);
 assert.match(panel,/rankIcon_1_001\.png/);assert.doesNotMatch(panel,/GJ_highscoreBtn_001\.png/);
 assert.match(panel,/queue->setEnabled\(false\)/);
});

test('surface chrome fades its own GD opacity through plain node containers',()=>run(`
#include <cassert>
#include <cmath>
#include <vector>
using GLubyte=unsigned char;
namespace cocos2d {
struct CCNode;
struct CCActionInterval{CCNode*target=nullptr;virtual ~CCActionInterval()=default;virtual void start(CCNode*t){target=t;}virtual void update(float)=0;};
struct CCSize{float width=0,height=0;};
struct CCNode{virtual ~CCNode()=default;CCSize size;std::vector<CCNode*>children;std::vector<CCActionInterval*>actions;
 auto*getChildren(){return &children;}CCSize getContentSize(){return size;}
 void runAction(CCActionInterval*a){actions.push_back(a);a->start(this);}
 void step(float t){for(auto*a:actions)a->update(t);if(t==1){for(auto*a:actions)delete a;actions.clear();}}};
struct CCLayerColor:CCNode{GLubyte opacity=255;GLubyte getOpacity(){return opacity;}void setOpacity(GLubyte a){opacity=a;}};
struct CCFadeTo:CCActionInterval{int from=0,to=0;static auto*create(float,GLubyte alpha){auto*a=new CCFadeTo;a->to=alpha;return a;}
 void start(CCNode*t)override{CCActionInterval::start(t);from=dynamic_cast<CCLayerColor*>(t)->getOpacity();}
 void update(float t)override{dynamic_cast<CCLayerColor*>(target)->setOpacity(static_cast<GLubyte>(std::lround(from+(to-from)*t)));}};
struct CCEaseSineOut:CCActionInterval{CCActionInterval*inner=nullptr;~CCEaseSineOut(){delete inner;}
 static auto*create(CCActionInterval*i){auto*a=new CCEaseSineOut;a->inner=i;return a;}void start(CCNode*t)override{inner->start(t);}
 void update(float t)override{inner->update(std::sin(t*1.57079632679f));}};
struct CCEaseSineIn:CCActionInterval{CCActionInterval*inner=nullptr;~CCEaseSineIn(){delete inner;}
 static auto*create(CCActionInterval*i){auto*a=new CCEaseSineIn;a->inner=i;return a;}void start(CCNode*t)override{inner->start(t);}
 void update(float t)override{inner->update(1-std::cos(t*1.57079632679f));}};
}
namespace geode::cocos{template<class T>auto const&CCArrayExt(std::vector<cocos2d::CCNode*>*v){return *v;}}
${strip(read('src/ui/ModeLobbySurfaceFade.hpp'))}
int main(){using namespace cocos2d;
 CCLayerColor group,fill,frame;CCNode surface,menu,button;group.opacity=0;
 fill.size={86,86};fill.opacity=190;frame.size={30,1};frame.opacity=250;
 group.children={&surface,&menu};surface.children={&fill};menu.children={&button};button.children={&frame};
 corum::ranked::ui::fadeModeLobbySurfaces(&group,.18f,true);
 assert(group.opacity==0 && group.actions.empty());assert(fill.opacity==0 && frame.opacity==0);
 fill.step(.5f);frame.step(.5f);assert(fill.opacity>0 && fill.opacity<190 && frame.opacity>0 && frame.opacity<250);
 fill.step(1);frame.step(1);assert(fill.opacity==190 && frame.opacity==250);
 corum::ranked::ui::fadeModeLobbySurfaces(&group,.18f,false);
 fill.step(.5f);frame.step(.5f);assert(fill.opacity>0 && fill.opacity<190 && frame.opacity>0 && frame.opacity<250);
 fill.step(1);frame.step(1);assert(fill.opacity==0 && frame.opacity==0);
}
`));
