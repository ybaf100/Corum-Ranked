import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const root=new URL('..',import.meta.url).pathname;
const read=p=>readFileSync(join(root,p),'utf8');
const method=(src,sig)=>{const at=src.indexOf(sig);assert(at>=0,sig);let depth=0,i=src.indexOf('{',at);for(;i<src.length;i++){if(src[i]==='{')depth++;else if(src[i]==='}'&&--depth===0)break;}return src.slice(at,i+1);};

test('seam sweep timeline: main menu at 0 ms, mode lobby at 780 ms, leaving plays it backwards',()=>{
 const dir=mkdtempSync(join(tmpdir(),'corum-mode-sweep-'));
 try{
  writeFileSync(join(dir,'test.cpp'),`#include "domain/ModeSweepTimeline.hpp"
#include "domain/RankedMode.hpp"
#include <cassert>
#include <cmath>
using namespace corum::ranked;
bool near(float a,float b,float e=1e-3f){return std::fabs(a-b)<e;}
int main(){
 // At rest on the main menu: both halves in place, nothing of the lobby shown.
 auto const menu=modeSweepFrame(0.0f);
 assert(near(menu.press,1)&&near(menu.otherOut,0)&&near(menu.otherAlpha,1)&&near(menu.travel,0)&&near(menu.selAlpha,1)&&near(menu.cardAlpha,0));
 assert(menu.menuTrophyHome&&!menu.lobbyTrophyHome&&!menu.flying&&near(menu.header,0)&&near(menu.heroAlpha,0)&&near(menu.heroOffset,1));
 // At rest in the lobby: the other half gone, the card and every panel row in place.
 auto const lobby=modeSweepFrame(ModeSweepMs);
 assert(near(ModeSweepMs,780.0f));
 assert(near(lobby.otherOut,1)&&near(lobby.otherAlpha,0)&&near(lobby.travel,1)&&near(lobby.selAlpha,0)&&near(lobby.cardAlpha,1));
 assert(!lobby.menuTrophyHome&&lobby.lobbyTrophyHome&&!lobby.flying&&near(lobby.header,1)&&near(lobby.heroAlpha,1)&&near(lobby.heroOffset,0)&&near(lobby.press,1));
 for(int count=1;count<=7;++count)for(int i=0;i<count;++i){
  auto const before=modeSweepBand(0.0f,i,count),after=modeSweepBand(ModeSweepMs,i,count);
  assert(near(before.alpha,0)&&near(before.offset,1)&&near(before.wipeAlpha,0));
  assert(near(after.alpha,1)&&near(after.offset,0)&&near(after.wipeAlpha,0)&&near(after.wipe,1));
  // Top to bottom, one after another, every row settled before the end.
  if(i)assert(modeSweepBandStart(i,count)>modeSweepBandStart(i-1,count));
  assert(modeSweepBandStart(i,count)+260.0f<=ModeSweepMs);
 }
 // The tap: the ENTER slab dips, then is back by 110 ms.
 assert(modeSweepFrame(55.0f).press<0.94f&&near(modeSweepFrame(110.0f).press,1));
 // The trophy flies on its own from 120 to 520 ms and lands on an arc.
 assert(!modeSweepFrame(119.0f).flying&&modeSweepFrame(120.0f).flying&&modeSweepFrame(519.0f).flying&&!modeSweepFrame(520.0f).flying);
 assert(modeSweepFrame(300.0f).lift>0.5f&&near(modeSweepFrame(120.0f).lift,0));
 // Each motion only moves forward with time (so backwards it only moves back).
 float last[5]={-1,-1,-1,-1,-1};
 for(float t=0;t<=ModeSweepMs;t+=5){auto const f=modeSweepFrame(t);float now[5]={f.otherOut,f.travel,f.cardAlpha,f.header,f.flight};
  for(int k=0;k<5;++k){assert(now[k]>=last[k]-1e-5f);last[k]=now[k];}}
 // Leaving the lobby: the same frames, from the end back to the start.
 assert(near(modeSweepTime(0.0f,false),ModeSweepMs)&&near(modeSweepTime(ModeSweepMs,false),0)&&near(modeSweepTime(200.0f,false),580.0f));
 assert(near(modeSweepTime(200.0f,true),200.0f)&&near(modeSweepTime(5000.0f,true),ModeSweepMs));
 // The transition stays held while the motion runs on the rebuilt screen.
 ModeLobbyTransition transition;assert(transition.select(RankedMode::SoloRanked)&&!transition.hold());
 transition.finish();assert(transition.hold()&&transition.moving()&&!transition.back()&&transition.selected());
 transition.finish();assert(transition.back()&&!transition.selected());
}
`);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
  execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }finally{rmSync(dir,{recursive:true,force:true});}
});

test('entering a mode sweeps the main menu into the lobby; Back plays the sweep backwards',()=>{
 const popup=read('src/RankedPopup.cpp');
 const animate=method(popup,'    void animateModeLobby(');
 // Both directions take the sweep first; the dissolve stays for other screens.
 assert(animate.indexOf('if (beginModeSweep(mode, opening)) return;')>0&&animate.indexOf('if (beginModeSweep(mode, opening)) return;')<animate.indexOf('beginCrossfade(0.32f)'));
 const sweep=method(popup,'    bool beginModeSweep(');
 // The outgoing screen is adopted before the new one is built, once; the transition is held until the motion ends.
 const adopt=sweep.indexOf('motion->adopt(selected)'),rebuild=sweep.indexOf('render();'),hold=sweep.indexOf('m_modeTransition.hold();');
 assert(adopt>0&&adopt<rebuild&&rebuild<hold);
 assert.match(sweep,/if \(opening\) \{ motion->adopt\(selected\); motion->adopt\(other\); \}\s*else \{ motion->adopt\(selected\); motion->adopt\(m_lobbyPanel\); \}/);
 assert.match(sweep,/self->m_modeTransition\.finish\(\);\s*self->syncLobbyInput\(\); self->refreshLobbyHover\(\);/);
 // A rebuild that is not the lobby releases the transition at once.
 assert.match(sweep,/auto const abandon = \[&\] \{\s*motion->finish\(\);\s*m_modeTransition\.finish\(\);/);
 // Input stays off on the rebuilt screen while it moves.
 assert(sweep.indexOf('setLobbyNodeInput(m_lobbyPanel,false);',hold)>hold);
 // The trophy flies between the two places: both carry the same ID; a tier hand-over keeps its own trophy.
 assert.equal([...popup.matchAll(/setID\("ranked-card-trophy"_spr\)/g)].length,2);
 assert.match(sweep,/if \(!selected->getChildByID\("ranked-lobby-tier-transition"_spr\)\) setup\.menuTrophy/);
 assert.match(popup,/motion->setID\("ranked-lobby-tier-transition"_spr\)/);
 assert.match(sweep,/setup\.trophyRatio = menuTrophySize\(size\.width\) \* 0\.82f \/ \(kLobbyTrophy \* 0\.8f\);/);
 assert.match(method(popup,'    void renderModeCard('),/auto const trophy = menuTrophySize\(size\.width\);/);
 const motion=read('src/ui/ModeSweepMotion.hpp');
 // Time runs backwards when leaving; the end frame is applied exactly.
 assert.match(motion,/apply\(modeSweepTime\(m_elapsed, m_setup\.opening\)\);/);
 assert.match(motion,/apply\(m_setup\.opening \? ModeSweepMs : 0\.0f\);/);
 // Surfaces fade from their own opacity (GD paints a CCLayerColor from it).
 assert.match(motion,/for \(auto const& \[layer, opacity\] : m_layers\) layer->setOpacity/);
 // The tap pulse is for opening only.
 assert.match(motion,/if \(s\.opening && s\.enter\) s\.enter->setScale\(m_enterScale \* f\.press\);/);
});

test('actual sweep motion: both screens move together, the trophy flies on its own, the end frame restores every piece',()=>{
 const strip=s=>s.replace(/^#include .*?$|^#pragma once$/gm,'');
 const dir=mkdtempSync(join(tmpdir(),'corum-mode-sweep-motion-'));
 try{
  writeFileSync(join(dir,'test.cpp'),`#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
using GLubyte=unsigned char;
namespace cocos2d {
struct CCPoint{float x=0,y=0;};
struct CCSize{float width=0,height=0;};
struct CCRect{CCPoint origin;CCSize size;};
struct ccColor3B{unsigned char r,g,b;};
struct ccColor4B{unsigned char r,g,b,a;};
struct CCNode{
 virtual ~CCNode()=default;
 std::vector<CCNode*> children;CCNode* parent=nullptr;CCPoint pos;float scale=1;bool visible=true;int z=0;std::string id;CCSize size;bool scheduled=false;
 std::vector<CCNode*>* getChildren(){return &children;}
 virtual bool init(){return true;}
 void autorelease(){}
 void addChild(CCNode* n,int zz=0){n->parent=this;n->z=zz;children.push_back(n);}
 void removeFromParentAndCleanup(bool){if(parent){auto& v=parent->children;v.erase(std::remove(v.begin(),v.end(),this),v.end());parent=nullptr;}}
 CCNode* getParent(){return parent;}
 int getZOrder(){return z;}
 CCPoint getPosition(){return pos;} void setPosition(CCPoint p){pos=p;} float getPositionX(){return pos.x;} float getPositionY(){return pos.y;}
 float getScale(){return scale;} void setScale(float s){scale=s;}
 void setVisible(bool v){visible=v;}
 void setID(std::string const& s){id=s;}
 CCPoint convertToWorldSpace(CCPoint p){CCPoint w{pos.x+p.x*scale,pos.y+p.y*scale};return parent?parent->convertToWorldSpace(w):w;}
 CCRect boundingBox(){return {pos,size};}
 CCSize getContentSize(){return size;} void setContentSize(CCSize s){size=s;}
 void scheduleUpdate(){scheduled=true;} void unscheduleUpdate(){scheduled=false;}
 virtual void update(float){}
};
struct CCRGBAProtocol{virtual ~CCRGBAProtocol()=default;virtual void updateDisplayedOpacity(GLubyte)=0;virtual bool isCascadeOpacityEnabled()=0;virtual GLubyte getDisplayedOpacity()=0;};
struct CCLayerColor:CCNode,CCRGBAProtocol{
 int real=255,displayed=255;bool cascade=false;
 static CCLayerColor* create(ccColor4B c,float w,float h){auto* l=new CCLayerColor;l->real=c.a;l->displayed=c.a;l->size={w,h};return l;}
 void setOpacity(GLubyte v){real=v;} GLubyte getOpacity(){return static_cast<GLubyte>(real);}
 void updateDisplayedOpacity(GLubyte v)override{displayed=static_cast<int>(std::lround(real*v/255.f));}
 bool isCascadeOpacityEnabled()override{return cascade;} GLubyte getDisplayedOpacity()override{return static_cast<GLubyte>(displayed);}
};
struct Label:CCNode,CCRGBAProtocol{int displayed=255;void updateDisplayedOpacity(GLubyte v)override{displayed=v;}bool isCascadeOpacityEnabled()override{return false;}GLubyte getDisplayedOpacity()override{return static_cast<GLubyte>(displayed);}};
}
namespace geode {
template<class T>struct Ref{T* p=nullptr;Ref()=default;Ref(T* v):p(v){}T* data()const{return p;}T* operator->()const{return p;}explicit operator bool()const{return p!=nullptr;}};
namespace cocos{template<class T>std::vector<cocos2d::CCNode*> const& CCArrayExt(std::vector<cocos2d::CCNode*>* v){return *v;}}
namespace cast{template<class T,class U>T typeinfo_cast(U* u){return dynamic_cast<T>(u);}}
}
inline std::string operator""_spr(char const* s,std::size_t n){return std::string(s,n);}
${strip(read('src/domain/ModeSweepTimeline.hpp'))}
namespace corum::ranked::ui{inline constexpr cocos2d::ccColor3B Accent{200,255,46};}
${strip(read('src/ui/ModeSweepMotion.hpp'))}
using namespace cocos2d;using namespace corum::ranked;using namespace corum::ranked::ui;
static CCLayerColor* layer(CCNode* parent,float w,float h,int alpha=255){auto* l=CCLayerColor::create({0,0,0,static_cast<unsigned char>(alpha)},w,h);parent->addChild(l);return l;}
struct Screen{
 CCNode root;CCLayerColor *sv,*so,*card,*svFill,*soFill,*cardFill,*menuTrophyCore,*lobbyTrophyCore,*row1,*row2,*heroFace,*surface;
 CCNode *menuTrophy,*lobbyTrophy,*enter,*hero;Label *menuTitle,*lobbyTitle,*caption;
 Screen(){
  sv=CCLayerColor::create({0,0,0,255},0,0);so=CCLayerColor::create({0,0,0,255},0,0);card=CCLayerColor::create({0,0,0,255},0,0);
  root.addChild(sv);root.addChild(so);root.addChild(card);
  svFill=layer(sv,298,246);soFill=layer(so,298,246);cardFill=layer(card,210,284,200);
  menuTrophy=new CCNode;menuTrophy->pos={208,228};sv->addChild(menuTrophy);menuTrophyCore=layer(menuTrophy,10,10);
  lobbyTrophy=new CCNode;lobbyTrophy->pos={94,218};card->addChild(lobbyTrophy);lobbyTrophyCore=layer(lobbyTrophy,10,10);
  enter=new CCNode;enter->pos={49,64};sv->addChild(enter);
  row1=layer(&root,300,20);row1->pos={222,250};row2=layer(&root,300,20);row2->pos={222,200};
  // A cascading surface with a caption: the caption takes the surface's faded opacity once.
  surface=layer(&root,100,20,180);surface->pos={222,150};surface->cascade=true;caption=new Label;surface->addChild(caption);
  hero=new CCNode;hero->pos={330,110};root.addChild(hero);heroFace=layer(hero,180,48);
  menuTitle=new Label;menuTitle->pos={46,302};root.addChild(menuTitle);lobbyTitle=new Label;lobbyTitle->pos={46,302};root.addChild(lobbyTitle);
 }
 ModeSweepMotion::Setup setup(bool opening,std::function<void()> done){
  ModeSweepMotion::Setup s;s.opening=opening;s.width=568;s.selectedShift=0;s.otherShift=330;
  s.menuSelected=sv;s.menuOther=so;s.card=card;s.enter=opening?enter:nullptr;s.menuTrophy=menuTrophy;s.lobbyTrophy=lobbyTrophy;s.trophyRatio=0.78f;
  s.bands={{row1},{row2},{surface}};s.hero=hero;s.menuTitles={menuTitle};s.lobbyTitles={lobbyTitle};s.done=std::move(done);return s;
 }
};
static void run(ModeSweepMotion* m,float ms){for(float t=0;t<ms;t+=20)m->update(0.02f);}
int main(){
 // Opening: the menu at rest first, then both screens in motion, then the lobby at rest.
 {Screen s;CCNode stage;auto* m=ModeSweepMotion::create();stage.addChild(m);bool done=false;
  m->start(s.setup(true,[&]{done=true;}));
  assert(m->id=="ranked-mode-sweep"&&m->scheduled);
  assert(s.cardFill->real==0&&s.svFill->real==255&&s.soFill->real==255&&s.row1->real==0&&s.heroFace->real==0);
  assert(s.menuTrophy->visible&&!s.lobbyTrophy->visible);
  run(m,300);
  // Mid-flight: the lobby trophy flies at full opacity while the card fades in around it.
  assert(s.lobbyTrophy->visible&&!s.menuTrophy->visible&&s.lobbyTrophyCore->real==255);
  assert(s.lobbyTrophy->pos.x>94&&s.lobbyTrophy->pos.x<208&&s.lobbyTrophy->scale<1.0f&&s.lobbyTrophy->scale>0.78f);
  assert(s.cardFill->real>0&&s.cardFill->real<200&&s.so->pos.x>0);
  // The cascading surface carries the fade once: its caption shows the surface's opacity.
  run(m,180);assert(s.surface->real>0&&s.surface->real<180&&s.caption->displayed==s.surface->real);
  run(m,600);
  assert(done&&!m->getParent()&&!m->scheduled);
  assert(s.cardFill->real==200&&s.row1->real==255&&s.row2->real==255&&s.surface->real==180&&s.heroFace->real==255);
  assert(s.row1->pos.x==222&&s.hero->pos.x==330&&s.card->pos.x==0&&s.lobbyTitle->pos.y==302&&s.lobbyTitle->displayed==255);
  assert(s.lobbyTrophy->visible&&s.lobbyTrophy->pos.x==94&&s.lobbyTrophy->scale==1.0f&&s.enter->scale==1.0f);
  assert(s.svFill->real==0&&s.soFill->real==0&&s.menuTitle->displayed==0);}
 // Leaving: the same frames backwards, from the lobby at rest to the menu at rest.
 {Screen s;CCNode stage;auto* m=ModeSweepMotion::create();stage.addChild(m);bool done=false;
  m->start(s.setup(false,[&]{done=true;}));
  assert(s.cardFill->real==200&&s.svFill->real==0&&s.soFill->real==0&&s.row1->real==255&&s.lobbyTrophy->visible&&!s.menuTrophy->visible);
  run(m,400);
  // Backwards the menu trophy is the one in flight, towards its menu place.
  assert(s.menuTrophy->visible&&!s.lobbyTrophy->visible&&s.menuTrophy->scale>1.0f);
  run(m,600);
  assert(done&&!m->getParent());
  assert(s.svFill->real==255&&s.soFill->real==255&&s.so->pos.x==0&&s.sv->pos.x==0&&s.menuTitle->pos.y==302&&s.menuTitle->displayed==255);
  assert(s.menuTrophy->visible&&s.menuTrophy->pos.x==208&&s.menuTrophy->scale==1.0f);
  assert(s.cardFill->real==0&&s.row1->real==0&&s.heroFace->real==0&&s.lobbyTitle->displayed==0);}
 // finish() on demand lands on the end frame at once.
 {Screen s;CCNode stage;auto* m=ModeSweepMotion::create();stage.addChild(m);int calls=0;
  m->start(s.setup(true,[&]{++calls;}));run(m,100);m->finish();m->finish();m->update(0.02f);
  assert(calls==1&&s.cardFill->real==200&&s.row1->real==255&&!m->getParent());}
 // Adopting keeps an outgoing node where it was on screen.
 {CCNode stage,root;root.pos={0,0};auto* group=new CCNode;group->pos={12,34};root.addChild(group,3);
  auto* m=ModeSweepMotion::create();stage.addChild(m);m->adopt(group);
  assert(group->getParent()==m&&group->pos.x==12&&group->pos.y==34&&group->z==3&&root.children.empty());}
}
`);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-Wno-unused-parameter',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
  execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
