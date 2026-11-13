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

test('L1 <-> L2/L3 are separate pages: fade out, rebuild for the selection, fade in, input locked meanwhile',()=>{
 const popup=read('src/RankedPopup.cpp');
 const animate=method(popup,'    void animateModeLobby(');
 assert.match(animate,/setLobbyNodeInput\(m_lobbySurvival,false\); setLobbyNodeInput\(m_lobbySolo,false\); setLobbyNodeInput\(m_lobbyPanel,false\);/);
 assert.match(animate,/ModeLobbyFade::create\(out, 0, corum::ranked::ui::ModeLobbyEase::SineIn\)/);
 assert.match(animate,/self->m_modeTransition\.finish\(\);\s*self->m_renderedUiKey\.clear\(\);\s*self->render\(\);/);
 assert.match(animate,/ModeLobbyFade::create\(0\.22f, 255, corum::ranked::ui::ModeLobbyEase::SineOut\)/);
 assert.match(animate,/self->syncLobbyInput\(\); self->refreshLobbyHover\(\);/);
 assert.doesNotMatch(animate,/ModeLobbySlide/);
 assert.match(method(popup,'    bool prepareUiTransition('),/if \(m_modeTransition.moving\(\) \|\| \(!matched && m_queueAnimating\)\).*return false/);
 assert.match(method(popup,'    void render()'),/m_modeTransition.moving\(\)/);
 assert.match(method(popup,'    void onCloseLayer('),/m_modeTransition.back\(\).*animateModeLobby\(mode,false\)/);
 const close=method(popup,'    void onCloseLayer(');assert(close.indexOf('if (m_page != Page::Live) { stepBack(); return; }')>0&&close.indexOf('stepBack()')<close.indexOf('leaveRanked()'),'sub-screens step back instead of leaving Ranked');
 assert.match(popup,/hitTop = y1 - kModeCardHitTopInset/);
 assert.match(method(popup,'    void drawQueueArena('),/radar\.maximumRange <= 0 \? inner \+ 1\.5f/,'solo radar starts at the smallest ring');
 assert.match(method(popup,'    void animateQueue('),/m_queueContents->removeFromParentAndCleanup\(true\);\s*self->forgetQueueContents\(\);/,'the queue close animation drops every queue pointer');assert.match(popup,/kModeCardHitTopInset = 1[0-9]\.0f/);
 const card=method(popup,'    void renderModeCard(');
 // L1 halves: whole-half hit areas; no hit area on the L2/L3 left section.
 assert.match(card,/select->setEnabled\(source.stage == RuntimeStage::Ready && !selected\)/);
 assert.match(card,/survival-card-select/);assert.match(card,/solo-ranked-card-select/);
 assert.match(card,/addSlantedFill\(m_root, x0, x1, y0, y1, survival \? px\(64\.0f\) : 0\.0f, survival \? 0\.0f : px\(64\.0f\)/);
 assert.match(card,/fontScale\(64\.0f\)/);assert.match(card,/px\(190\.0f\)/);
 const panel=method(popup,'    void renderModePanel(');
 assert.match(panel,/heroSlab\(heroSize, playLive/);
 assert.match(panel,/queue->setEnabled\(online\)/);
 assert.match(panel,/auto const online = view\.survivalEnabled && view\.survival\.onlineAvailable/);
 assert.match(panel,/CorumRankedLayer::onSurvivalQueue/);
});

test('surface chrome fades its own GD opacity through plain node containers',()=>run(`
#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>
using GLubyte=unsigned char;
namespace cocos2d {
struct CCNode;
struct CCActionInterval{CCNode*target=nullptr;int tag=-1;virtual ~CCActionInterval()=default;bool initWithDuration(float){return true;}
 void autorelease(){}void setTag(int t){tag=t;}virtual void update(float)=0;};
struct CCSize{float width=0,height=0;};
struct CCNode{virtual ~CCNode()=default;CCSize size;std::vector<CCNode*>children;std::vector<CCActionInterval*>actions;
 auto*getChildren(){return &children;}CCSize getContentSize(){return size;}
 void runAction(CCActionInterval*a){actions.push_back(a);a->target=this;}
 void stopActionByTag(int t){for(auto it=actions.begin();it!=actions.end();++it)if((*it)->tag==t){delete *it;actions.erase(it);return;}}
 void step(float t){for(auto*a:actions)a->update(t);if(t==1){for(auto*a:actions)delete a;actions.clear();}}};
struct CCLayerColor:CCNode{GLubyte opacity=255;GLubyte getOpacity(){return opacity;}void setOpacity(GLubyte a){opacity=a;}};
}
namespace geode{template<class T>struct Ref{T*p=nullptr;Ref(T*v):p(v){}T*operator->()const{return p;}explicit operator bool()const{return p;}};}
namespace geode::cocos{template<class T>auto const&CCArrayExt(std::vector<cocos2d::CCNode*>*v){return *v;}}
${strip(read('src/ui/ModeLobbySurfaceFade.hpp'))}
int main(){using namespace cocos2d;
 CCLayerColor group,fill,frame;CCNode surface,menu,button;group.opacity=0;
 fill.size={86,86};fill.opacity=190;frame.size={30,1};frame.opacity=250;
 group.children={&surface,&menu};surface.children={&fill};menu.children={&button};button.children={&frame};
 corum::ranked::ui::fadeModeLobbySurfaces(&group,.18f,true);
 // One tween on the root owns every surface; no per-layer ease/inner pair.
 assert(group.opacity==0 && group.actions.size()==1);assert(fill.actions.empty() && frame.actions.empty());
 assert(fill.opacity==0 && frame.opacity==0);
 group.step(.5f);assert(fill.opacity>0 && fill.opacity<190 && frame.opacity>0 && frame.opacity<250);
 // A new fade replaces the running one instead of fighting it.
 corum::ranked::ui::fadeModeLobbySurfaces(&group,.18f,false);assert(group.actions.size()==1);
 auto const half=fill.opacity;group.step(.5f);assert(fill.opacity>0 && fill.opacity<half);
 group.step(1);assert(fill.opacity==0 && frame.opacity==0 && group.actions.empty());
 fill.opacity=190;frame.opacity=250;
 corum::ranked::ui::fadeModeLobbySurfaces(&group,.18f,true);group.step(1);assert(fill.opacity==190 && frame.opacity==250);
}
`));
