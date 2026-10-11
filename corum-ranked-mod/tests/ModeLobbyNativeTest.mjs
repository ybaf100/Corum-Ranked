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
  assert(l.centerY-150*l.scale>=12&&l.centerY+135*l.scale<=size.second-44);
 }
 struct Card{std::string scope;int claimableRewards;};
 std::vector<Card> cards={{"SOLO_RANKED",0},{"SHARED",1},{"SOLO_RANKED",2},{"SHARED",0}};
 assert((achievementDisplayOrderForScope(cards,"SOLO_RANKED")==std::vector<std::size_t>{2,0}));
 assert((achievementDisplayOrderForScope(cards,"SHARED")==std::vector<std::size_t>{1,3}));
 assert(achievementDisplayOrderForScope(cards,"SURVIVAL").empty());
 assert(achievementScopeName("SHARED",true)=="공용"&&achievementScopeName("SOLO_RANKED",false)=="Solo Ranked");
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
${strip(read('src/ui/ModeLobbyMotion.hpp'))}
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
struct CCNode{virtual ~CCNode()=default;std::vector<CCNode*> children;std::vector<CCActionInterval*> actions;CCPoint position;float sx=1,sy=1;bool visible=true;
 auto*getChildren(){return &children;}CCNode*getParent(){return this;}void setVisible(bool v){visible=v;}void runAction(CCActionInterval*);void step(float);};
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
struct CCSequence:CCActionInterval{CCActionInterval*delay=nullptr,*call=nullptr;~CCSequence(){delete delay;delete call;}static auto*create(CCActionInterval*d,CCActionInterval*c,std::nullptr_t){auto*a=new CCSequence;a->delay=d;a->call=c;return a;}void update(float t)override{call->update(t);}};
struct CCDirector{CCSize size;static auto*sharedDirector(){static CCDirector d;return &d;}CCSize getWinSize(){return size;}};
}
using namespace cocos2d;
template<class T>auto const&CCArrayExt(std::vector<CCNode*>*v){return *v;}template<class T>T typeinfo_cast(CCNode*n){return dynamic_cast<T>(n);}template<class T>struct WeakRef{T*ptr;WeakRef(T*p):ptr(p){}T*lock()const{return ptr;}};
namespace geode::cocos{template<class T>auto const&CCArrayExt(std::vector<CCNode*>*v){return *v;}}
namespace corum::ranked::ui{void cancelMenuTouch(CCMenu*m){++m->cancels;}}
${strip(read('src/ui/ModeLobbyMotion.hpp'))}
enum class RuntimeStage{Ready,Queued,Matched};struct RuntimeView{RuntimeStage stage;};
struct Owner:CCNode{
 corum::ranked::ModeLobbyTransition m_modeTransition;CCLayerColor survival,solo,panel;
 CCLayerColor *m_lobbySurvival=&survival,*m_lobbySolo=&solo,*m_lobbyPanel=&panel;
 CCMenu survivalMenu,soloMenu,panelMenu,menu,settings;
 CCMenu*m_menu=&menu,*m_settingsMenu=&settings;bool m_uiFadingOut=false,m_returnToModesAfterQueue=false;std::string m_renderedUiKey;
 enum class Page{Live,History};Page m_page=Page::Live;
 int headerUpdates=0;std::string presentationKey(){return "selected";}void updateModeLobbyHeader(){++headerUpdates;}
 Owner(){survival.children={&survivalMenu};solo.children={&soloMenu};panel.children={&panelMenu};}
 ${method(popup,'    void setLobbyNodeInput(')}
 ${method(popup,'    void syncLobbyInput(')}
 ${method(popup,'    void animateModeLobby(')}
 ${method(popup,'    void resumeModeSelectionAfterQueue(')}
 void step(float t){survival.step(t);solo.step(t);panel.step(t);}
};
int main(){using namespace corum::ranked;
 for(auto mode:{RankedMode::Survival,RankedMode::SoloRanked}){
  Owner o;auto l=modeLobbyLayout(480,320);o.survival.position={l.leftX,l.centerY};o.solo.position={l.rightX,l.centerY};
  o.panel.sx=l.scale*.02f;o.panel.sy=l.scale;o.panel.real=0;o.panel.visible=false;
  assert(o.m_modeTransition.select(mode));o.animateModeLobby(mode,true);
  assert(!o.menu.touch&&!o.settings.touch&&!o.survivalMenu.touch&&!o.soloMenu.touch&&!o.panelMenu.touch);
  assert(o.solo.position.x==l.rightX&&o.survival.position.x==l.leftX&&o.panel.sx==l.scale*.02f);
  o.step(.25f);float quarterX=o.solo.position.x,quarterScale=o.panel.sx;
  assert(o.panel.sx>l.scale*.02f&&o.panel.sx<l.scale);
  if(mode==RankedMode::SoloRanked)assert(quarterX<l.rightX&&quarterX>l.leftX);else assert(quarterX==l.rightX);
  assert(!o.m_modeTransition.back()&&!o.m_modeTransition.select(mode));
  o.step(.5f);assert(!o.panelMenu.touch);o.step(1);
  assert(!o.m_modeTransition.moving()&&o.menu.touch&&o.panelMenu.touch&&!o.survivalMenu.touch&&!o.soloMenu.touch);
  assert(o.panel.sx==l.scale&&o.panel.real==255);
  if(mode==RankedMode::SoloRanked)assert(o.solo.position.x==l.leftX&&!o.survival.visible);else assert(!o.solo.visible);
  assert(o.m_modeTransition.back());o.animateModeLobby(mode,false);assert(!o.menu.touch&&!o.panelMenu.touch);
  o.step(.75f);assert(std::abs(o.solo.position.x-quarterX)<.001f&&std::abs(o.panel.sx-quarterScale)<.001f);
  o.step(1);assert(!o.m_modeTransition.selected()&&!o.m_modeTransition.moving()&&!o.panel.visible);
  assert(o.solo.position.x==l.rightX&&o.survival.position.x==l.leftX&&o.solo.visible&&o.survival.visible);
  assert(o.menu.touch&&o.survivalMenu.touch&&o.soloMenu.touch&&!o.panelMenu.touch&&o.headerUpdates==2);
 }
 Owner o;auto l=modeLobbyLayout(480,320);o.survival.position={l.leftX,l.centerY};o.solo.position={l.rightX,l.centerY};o.panel.sx=l.scale*.02f;
 assert(o.m_modeTransition.select(RankedMode::SoloRanked));o.animateModeLobby(RankedMode::SoloRanked,true);o.step(1);
 o.m_returnToModesAfterQueue=true;o.resumeModeSelectionAfterQueue({RuntimeStage::Queued});assert(!o.m_modeTransition.moving());
 o.m_uiFadingOut=true;o.resumeModeSelectionAfterQueue({RuntimeStage::Ready});assert(o.m_returnToModesAfterQueue);
 o.m_uiFadingOut=false;o.resumeModeSelectionAfterQueue({RuntimeStage::Ready});assert(!o.m_returnToModesAfterQueue&&o.m_modeTransition.moving());
 o.resumeModeSelectionAfterQueue({RuntimeStage::Ready});assert(o.panel.actions.size()==3);o.step(1);assert(!o.m_modeTransition.selected());
 assert(o.m_modeTransition.select(RankedMode::SoloRanked));o.animateModeLobby(RankedMode::SoloRanked,true);o.step(1);
 o.m_returnToModesAfterQueue=true;o.resumeModeSelectionAfterQueue({RuntimeStage::Matched});
 assert(!o.m_returnToModesAfterQueue&&!o.m_modeTransition.moving()&&o.m_modeTransition.selected()==RankedMode::SoloRanked);
}
 `);
});

test('mode lobby owns Cocos actions and defers full UI replacement while moving',()=>{
 const popup=read('src/RankedPopup.cpp');
 const animate=method(popup,'    void animateModeLobby(');
 assert.match(animate,/CCEaseSineInOut::create\(CCMoveTo::create/);
 assert.match(animate,/opening \? layout.leftX : layout.rightX/);
 assert.match(animate,/CCEaseSineInOut::create\(CCScaleTo::create/);
 assert.match(animate,/opening \? layout.scale : layout.scale \* 0\.02f/);
 assert.match(animate,/ModeLobbyFade::create\(duration,opening \? 0 : 255/);
 assert.match(animate,/CCDelayTime::create\(duration\)/);
 assert.match(method(popup,'    bool prepareUiTransition('),/if \(m_modeTransition.moving\(\)\).*return false/);
 assert.match(method(popup,'    void render()'),/m_modeTransition.moving\(\)/);
 assert.match(method(popup,'    void onCloseLayer('),/m_modeTransition.back\(\).*animateModeLobby\(mode,false\)/);
 const card=method(popup,'    void renderModeCard(');
 assert.match(card,/if \(!enabled\)/);assert.match(card,/enter->setEnabled\(enabled/);
 assert.match(card,/survival-entry/);assert.match(card,/solo-ranked-entry/);
 const panel=method(popup,'    void renderModePanel(');
 assert.match(panel,/rankIcon_1_001\.png/);assert.doesNotMatch(panel,/GJ_highscoreBtn_001\.png/);
 assert.match(panel,/queue->setEnabled\(false\)/);
});
