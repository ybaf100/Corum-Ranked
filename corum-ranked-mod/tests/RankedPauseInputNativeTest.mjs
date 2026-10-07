import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
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
#include <cassert>
#include <memory>
#include <optional>
struct CCPoint {float x=0,y=0;};struct CCSize {float width=40,height=40;};
struct CCRect {float x,y,w,h;CCRect(float a,float b,float c,float d):x(a),y(b),w(c),h(d){}
 bool containsPoint(CCPoint p)const{return p.x>=x&&p.x<=x+w&&p.y>=y&&p.y<=y+h;}};
struct CCTouch {int id;CCPoint point;int getID()const{return id;}CCPoint getLocation()const{return point;}};struct CCEvent {};
struct CCNode {CCNode* parent=nullptr;bool visible=true;CCPoint origin;CCSize size;
 CCNode* getParent()const{return parent;}bool isVisible()const{return visible;}
 CCSize getContentSize()const{return size;}CCPoint convertToNodeSpace(CCPoint p)const{return {p.x-origin.x,p.y-origin.y};}
 void setVisible(bool v){visible=v;}};
struct Button:CCNode {bool enabled=true,selectedState=false;void setEnabled(bool v){enabled=v;}
 void selected(){selectedState=true;}void unselected(){selectedState=false;}};
struct PlayLayer;
struct UILayer:CCNode {Button pause;Button* m_pauseBtn=&pause;PlayLayer* owner=nullptr;
 int touchID=5,resets=0;void doPause();};
struct PlayLayer:CCNode {static inline PlayLayer* current=nullptr;bool m_isPaused=false,running=true,complete=false;
 std::shared_ptr<int> life=std::make_shared<int>(0);UILayer ui;UILayer* m_uiLayer=&ui;int pauseMenus=0,pauseCounter=0;
 PlayLayer(){current=this;ui.owner=this;ui.parent=this;ui.pause.parent=&ui;ui.pause.origin={100,100};}
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
class RankedEntryOverlay {public:
 CCNode heading,digit;CCNode* m_heading=&heading;CCNode* m_digit=&digit;
 WeakRef<PlayLayer> m_play;bool m_holding=true;int m_pauseTouchId=-1;unsigned int m_pauseDiagnostics=0;
 bool touchEnabled=true,keypad=true,removed=false;unsigned char opacity=165;
 void setOpacity(unsigned char v){opacity=v;}void setKeypadEnabled(bool v){keypad=v;}
 void setTouchEnabled(bool v){touchEnabled=v;}void removeFromParentAndCleanup(bool){removed=true;}
 void releaseToGameplay(PlayLayer*);bool pauseHit(PlayLayer*,CCTouch*)const;void cancelPauseTouch();
 bool ccTouchBegan(CCTouch*,CCEvent*);void ccTouchEnded(CCTouch*,CCEvent*);void ccTouchCancelled(CCTouch*,CCEvent*);void dismiss();
};
${['void RankedEntryOverlay::releaseToGameplay(','bool RankedEntryOverlay::pauseHit(','void RankedEntryOverlay::cancelPauseTouch()',
'bool RankedEntryOverlay::ccTouchBegan(','void RankedEntryOverlay::ccTouchEnded(','void RankedEntryOverlay::ccTouchCancelled(',
'void RankedEntryOverlay::dismiss()'].map(body).join('\n')}
int main(){PlayLayer play;RankedEntryOverlay input;CCTouch pause{1,{110,110}},other{2,{1,1}};
 assert(input.ccTouchBegan(&pause,nullptr));input.releaseToGameplay(&play);input.ccTouchEnded(&pause,nullptr);
 assert(play.pauseMenus==0&&!input.m_holding&&input.opacity==0&&!input.keypad&&!input.heading.visible&&!input.digit.visible);
 assert(!input.ccTouchBegan(&other,nullptr)); // ordinary gameplay passes through
 // Reproduce native CCMenu remaining disabled/TrackingTouch: our scene-owned
 // Pause route still creates the native menu without a second global detour.
 play.ui.pause.enabled=false;
 for(int round=0;round<3;++round){assert(input.ccTouchBegan(&pause,nullptr));
   assert(play.ui.pause.selectedState&&play.ui.pause.enabled);
   assert(!input.ccTouchBegan(&other,nullptr));input.ccTouchEnded(&pause,nullptr);
   assert(play.m_isPaused&&play.pauseMenus==round+1&&play.pauseCounter==round+1);
   assert(play.ui.touchID==-1&&!play.ui.pause.selectedState);
   // Native PauseLayer/modals receive input; the scene observer cannot swallow it.
   assert(!input.ccTouchBegan(&pause,nullptr));play.resume();}
 assert(input.ccTouchBegan(&pause,nullptr));input.ccTouchCancelled(&pause,nullptr);
 input.ccTouchEnded(&pause,nullptr);assert(play.pauseMenus==3&&!play.ui.pause.selectedState);
 assert(input.ccTouchBegan(&pause,nullptr));pause.point={1,1};input.ccTouchEnded(&pause,nullptr);assert(play.pauseMenus==3);
 pause.point={110,110};play.ui.visible=false;assert(!input.ccTouchBegan(&pause,nullptr));play.ui.visible=true;
 play.complete=true;assert(!input.ccTouchBegan(&pause,nullptr));play.complete=false;
 PlayLayer replacement;assert(!input.ccTouchBegan(&pause,nullptr));PlayLayer::current=&play;
 assert(input.ccTouchBegan(&pause,nullptr));input.dismiss();assert(!input.touchEnabled&&!input.keypad&&input.removed);
 assert(!play.ui.pause.selectedState);input.ccTouchEnded(&pause,nullptr);assert(play.pauseMenus==3);
 RankedEntryOverlay stale;{auto destroyed=std::make_unique<PlayLayer>();stale.releaseToGameplay(destroyed.get());}
 assert(!stale.ccTouchBegan(&pause,nullptr));
}
`;
const dir=mkdtempSync(join(tmpdir(),'corum-pause-input-'));try{writeFileSync(join(dir,'test.cpp'),code);
execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',...(debug?['-DCORUM_RANKED_DEBUG_SPECTATE=1']:[]),join(dir,'test.cpp'),'-o',join(dir,'test')]);
execFileSync(join(dir,'test'),[],{stdio:'pipe'});}finally{rmSync(dir,{recursive:true,force:true});}
});
