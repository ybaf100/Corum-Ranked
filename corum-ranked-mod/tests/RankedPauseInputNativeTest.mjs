import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,mkdirSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const source=readFileSync(join(root,'src/ui/RankedEntryOverlay.cpp'),'utf8');
function body(signature){const a=source.indexOf(signature);assert(a>=0);let b=source.indexOf('{',a)+1,d=1;
 while(d){if(source[b]==='{')++d;if(source[b]==='}')--d;++b;}return source.slice(a,b);}
for(const debug of [false,true])test(`scene-owned Pause dispatch creates native menu and releases other UI (Debug ${debug?'ON':'OFF'})`,()=>{
const code=`
#include "${join(root,'src/ui/RankedInput.hpp')}"
#include <memory>
#include <optional>
using namespace cocos2d;
using namespace corum::ranked::ui;
using geode::cast::typeinfo_cast;
struct Button:CCMenuItem {void selected(){CCMenuItem::selected=true;}};
struct PlayLayer;
struct UILayer:CCLayer {Button* m_pauseBtn=nullptr;PlayLayer* owner=nullptr;
 int touchID=5,resets=0,hits=0;void doPause();
 bool ccTouchBegan(CCTouch*,CCEvent*) override {++hits;return true;}};
struct PlayLayer:CCNode {static inline PlayLayer* current=nullptr;bool m_isPaused=false,running=true,complete=false;
 std::shared_ptr<int> life=std::make_shared<int>(0);UILayer ui;UILayer* m_uiLayer=&ui;Button pause;CCMenu menu;
 int pauseMenus=0,pauseCounter=0;
 PlayLayer(){current=this;ui.owner=this;ui.parent=this;ui.m_pauseBtn=&pause;menu.parent=&ui;
   menu.item=&pause;pause.parent=&menu;pause.origin={100,100};}
 ~PlayLayer(){if(current==this)current=nullptr;}static PlayLayer* get(){return current;}
 bool isRunning()const{return running;}bool canPauseGame()const{return !complete;}
 void pauseGame(bool){if(m_isPaused||!canPauseGame())return;m_isPaused=true;++pauseMenus;}
 void resume(){m_isPaused=false;}};
void UILayer::doPause(){touchID=-1;++resets;owner->pauseGame(false);++owner->pauseCounter;}
namespace geode {namespace log {template<class...T>void info(T...) {}}
 template<class T>struct WeakRef {T* p=nullptr;std::weak_ptr<int> life;
 WeakRef& operator=(T* v){p=v;if(v)life=v->life;else life.reset();return *this;}
 struct Ref {T* p;T* data()const{return p;}explicit operator bool()const{return p;}T* operator->()const{return p;}};
 Ref lock()const{return {life.expired()?nullptr:p};}};}
using namespace geode;
class RankedEntryOverlay:public CCLayer {public:
 CCNode heading,digit;CCNode* m_heading=&heading;CCNode* m_digit=&digit;
 WeakRef<PlayLayer> m_play;bool m_holding=true;int m_pauseTouchId=-1;unsigned int m_pauseDiagnostics=0;
 bool keypad=true,removed=false;unsigned char opacity=165;
 void setOpacity(unsigned char v){opacity=v;}void setKeypadEnabled(bool v){keypad=v;}
 void removeFromParentAndCleanup(bool){removed=true;onExit();}
 void registerWithTouchDispatcher() override;
 void synchronizeTouchPriority(PlayLayer*);
 void releaseToGameplay(PlayLayer*);bool pauseHit(PlayLayer*,CCTouch*)const;void cancelPauseTouch();
 bool ccTouchBegan(CCTouch*,CCEvent*) override;void ccTouchEnded(CCTouch*,CCEvent*) override;
 void ccTouchCancelled(CCTouch*,CCEvent*);void dismiss();
};
${['void RankedEntryOverlay::registerWithTouchDispatcher()', 'void RankedEntryOverlay::synchronizeTouchPriority(', 'void RankedEntryOverlay::releaseToGameplay(','bool RankedEntryOverlay::pauseHit(','void RankedEntryOverlay::cancelPauseTouch()',
'bool RankedEntryOverlay::ccTouchBegan(','void RankedEntryOverlay::ccTouchEnded(','void RankedEntryOverlay::ccTouchCancelled(',
'void RankedEntryOverlay::dismiss()'].map(body).join('\n')}
int main(){
 auto& dispatcher=CCDirector::sharedDirector()->dispatcher;
 for(int round=1;round<=3;++round){PlayLayer play;RankedEntryOverlay input;
   play.ui.onEnter();play.menu.onEnter();input.onEnter();
   // Exact device priorities: R1 0/-128, R2 -502/-503; include a still
   // stronger R3 to prove this is scoped ordering, not another fixed constant.
   const int uiPriority=round==1?0:round==2?-502:-1502;
   dispatcher.setPriority(uiPriority,&play.ui);
   dispatcher.setPriority(round==1?-128:uiPriority-1,&play.menu);
   CCTouch pause{1,{110,110}},other{2,{1,1}};
   if(round>1){input.m_holding=false;input.m_play=&play;
     dispatcher.began(90);dispatcher.ended(90);
     // beta38 -500 is behind the native menu; its callback can consume the
     // touch without opening Pause. Direct callback tests missed this ordering.
     assert(play.pauseMenus==0 && play.pause.activations==1);
     input.m_holding=true;}
   assert(input.ccTouchBegan(&pause,nullptr));input.releaseToGameplay(&play);input.ccTouchEnded(&pause,nullptr);
   assert(play.pauseMenus==0&&!input.m_holding&&input.opacity==0&&!input.keypad&&!input.heading.visible&&!input.digit.visible);
   assert(dispatcher.findHandler(&input)->m_nPriority < dispatcher.findHandler(&play.ui)->m_nPriority);
   assert(dispatcher.findHandler(&input)->m_nPriority < dispatcher.findHandler(&play.menu)->m_nPriority);
   auto const writes=dispatcher.priorityWrites;input.synchronizeTouchPriority(&play);
   assert(writes==dispatcher.priorityWrites); // no registration/priority churn
   assert(!input.ccTouchBegan(&other,nullptr));
   play.pause.enabled=false;play.menu.m_eState=kCCMenuStateTrackingTouch;
   for(int resume=0;resume<3;++resume){dispatcher.began(10+resume);dispatcher.ended(10+resume);
     assert(play.m_isPaused&&play.pauseMenus==resume+1&&play.pauseCounter==resume+1);
     assert(play.ui.touchID==-1&&!play.pause.CCMenuItem::selected);
     // The observer returns false while paused, even if an external modal has
     // a numerically later priority. It cannot steal native/menu dialog clicks.
     assert(!input.ccTouchBegan(&pause,nullptr));play.resume();}
   assert(input.ccTouchBegan(&pause,nullptr));input.ccTouchCancelled(&pause,nullptr);
   input.ccTouchEnded(&pause,nullptr);assert(play.pauseMenus==3&&!play.pause.CCMenuItem::selected);
   assert(input.ccTouchBegan(&pause,nullptr));pause.point={1,1};input.ccTouchEnded(&pause,nullptr);assert(play.pauseMenus==3);
   pause.point={110,110};play.ui.visible=false;assert(!input.ccTouchBegan(&pause,nullptr));play.ui.visible=true;
   play.complete=true;assert(!input.ccTouchBegan(&pause,nullptr));play.complete=false;
   {PlayLayer replacement;assert(!input.ccTouchBegan(&pause,nullptr));}PlayLayer::current=&play;
   assert(input.ccTouchBegan(&pause,nullptr));input.dismiss();assert(!input.touch&&!input.keypad&&input.removed);
   assert(!play.pause.CCMenuItem::selected);input.ccTouchEnded(&pause,nullptr);assert(play.pauseMenus==3);
   assert(!dispatcher.findHandler(&input));play.menu.onExit();play.ui.onExit();assert(dispatcher.handlers.empty());
 }
 RankedEntryOverlay stale;{auto destroyed=std::make_unique<PlayLayer>();stale.releaseToGameplay(destroyed.get());}
 CCTouch pause{1,{110,110}};assert(!stale.ccTouchBegan(&pause,nullptr));
}
`;
const dir=mkdtempSync(join(tmpdir(),'corum-pause-input-'));try{writeFileSync(join(dir,'test.cpp'),code);
 mkdirSync(join(dir,'Geode'));writeFileSync(join(dir,'Geode/Geode.hpp'),readFileSync(join(root,'tests/helpers/RankedInputNativeDouble.hpp'),'utf8'));
execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',dir,...(debug?['-DCORUM_RANKED_DEBUG_SPECTATE=1']:[]),join(dir,'test.cpp'),'-o',join(dir,'test')]);
execFileSync(join(dir,'test'),[],{stdio:'pipe'});}finally{rmSync(dir,{recursive:true,force:true});}
});
