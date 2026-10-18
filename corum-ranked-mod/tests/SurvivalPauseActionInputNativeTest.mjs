import test from 'node:test';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
const source=p=>read(p).replace(/^\s*#(?:include|pragma).*$/gm,'');

for(const debug of [false,true])test(`actual Survival Pause observer delivers transformed taps ahead of native blockers, respects modals and retires queued callbacks (Debug ${debug?'ON':'OFF'})`,()=>{
 const code=`
#include "${join(root,'tests/helpers/RankedInputNativeDouble.hpp')}"
#include <limits>
#include <utility>
using namespace cocos2d;
struct UILayer:CCLayer{};
struct PlayLayer:CCLayer{static inline PlayLayer* current=nullptr;UILayer* m_uiLayer=nullptr;static PlayLayer*get(){return current;}};
struct CCMenuItemSpriteExtra:CCMenuItem{void selected(){CCMenuItem::selected=true;}};
struct FLAlertLayer:CCLayer{};
namespace geode {
struct Popup:FLAlertLayer{};
namespace log{template<class...T>void info(T...){}}
template<class T>struct WeakRef{
 T* p=nullptr;std::weak_ptr<int> life;
 WeakRef()=default;explicit WeakRef(T* value):p(value){if(value)life=value->life;}
 struct Ref{T*p;T*data()const{return p;}T*operator->()const{return p;}explicit operator bool()const{return p;}};
 Ref lock()const{return {life.expired()?nullptr:p};}
};
std::vector<std::function<void()>> queued;
void queueInMainThread(std::function<void()>fn){queued.push_back(std::move(fn));}
void drain(){auto tasks=std::exchange(queued,{});for(auto&fn:tasks)fn();}
namespace prelude{using namespace cocos2d;using namespace geode;using geode::cast::typeinfo_cast;}
}
std::string operator""_spr(char const* value,std::size_t size){return {value,size};}
${source('src/ui/SurvivalPauseActionInput.hpp')}
${source('src/ui/SurvivalPauseActionInput.cpp')}
using namespace geode;
using corum::ranked::ui::SurvivalPauseActionInput;
struct Blocker:CCLayer{int began=0;bool ccTouchBegan(CCTouch*,CCEvent*)override{++began;return true;}};
int main(){
 auto* director=CCDirector::sharedDirector();auto& d=director->dispatcher;
 for(int priority:{-128,-502,-1502}){
  CCNode scene;PlayLayer play;UILayer ui;Blocker pause;CCMenu menu;CCMenuItemSpriteExtra surrender,force;
  director->runningScene=&scene;PlayLayer::current=&play;play.m_uiLayer=&ui;
  scene.addChild(&play);scene.addChild(&pause);play.addChild(&ui);pause.addChild(&menu);
  // World-space rectangles include both parent translation and scaling.
  pause.origin={30,20};pause.scale=1.5f;menu.origin={20,10};menu.scale=.8f;
  menu.addChild(&surrender);surrender.origin={10,20};surrender.size={96,36};
  menu.addChild(&force);force.origin={130,20};force.size={112,30};
  const CCPoint surrenderPoint{30+1.5f*(20+.8f*30),20+1.5f*(10+.8f*30)};
  const CCPoint forcePoint{30+1.5f*(20+.8f*150),20+1.5f*(10+.8f*30)};
  play.onEnter();ui.onEnter();menu.onEnter();pause.onEnter();d.setPriority(priority,&pause);
  int surrendered=0,cleared=0;bool allowed=true;
  surrender.callback=[&]{++surrendered;};menu.item=&surrender;
  // Reproduce the previous registration order: a visible/enabled menu item
  // cannot receive End when the native Pause claimed its Begin first.
  d.began(1,surrenderPoint);d.ended(1,surrenderPoint);assert(surrendered==0&&pause.began==1);
  auto* input=SurvivalPauseActionInput::create(&pause,&play,[&]{return allowed;});assert(input);pause.addChild(input);
  input->bind(&surrender,[&]{++surrendered;});
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
  input->bind(&force,[&]{++cleared;});
#endif
  input->onEnter();drain();assert(d.findHandler(input)->m_nPriority<priority);
  const int writes=d.priorityWrites;input->update(0);assert(d.priorityWrites==writes);
  d.began(2,surrenderPoint);assert(surrender.CCMenuItem::selected);d.ended(2,surrenderPoint);
  assert(surrendered==0&&!surrender.CCMenuItem::selected); // deferred, outside dispatch
  d.began(3,surrenderPoint);d.ended(3,surrenderPoint);drain();
  assert(surrendered==1); // no duplicate action while a callback is pending
  menu.item=nullptr;d.began(4,forcePoint);d.ended(4,forcePoint);drain();
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
  assert(cleared==1);
#else
  assert(cleared==0);
#endif
  auto tap=[&](int id){d.began(id,surrenderPoint);d.ended(id,surrenderPoint);drain();};
  surrender.enabled=false;tap(5);assert(!surrender.enabled&&surrendered==1);surrender.enabled=true;
  d.began(6,surrenderPoint);d.moved(6,{1,1});d.ended(6,surrenderPoint);drain();assert(surrendered==1);
  d.began(7,surrenderPoint);d.cancelled(7);d.ended(7,surrenderPoint);drain();assert(surrendered==1);
  menu.visible=false;tap(8);assert(surrendered==1);menu.visible=true;
  allowed=false;tap(9);assert(surrendered==1);allowed=true;
  // A confirmation belonging to any mod wins even with a later priority.
  Popup modal;scene.addChild(&modal);modal.onEnter();const int modalWrites=d.priorityWrites;
  input->update(0);tap(10);assert(surrendered==1&&d.priorityWrites==modalWrites);
  modal.visible=false;modal.onExit();tap(11);assert(surrendered==2);
  d.began(12,surrenderPoint);d.ended(12,surrenderPoint);director->nextScene=&scene;drain();assert(surrendered==2);director->nextScene=nullptr;
  d.began(13,surrenderPoint);d.ended(13,surrenderPoint);PlayLayer::current=nullptr;drain();assert(surrendered==2);PlayLayer::current=&play;
  d.began(14,surrenderPoint);d.ended(14,surrenderPoint);pause.parent=nullptr;drain();assert(surrendered==2);pause.parent=&scene;
  d.began(15,surrenderPoint);d.ended(15,surrenderPoint);input->suspend();drain();assert(surrendered==2);
  input->onExit();delete input;menu.onExit();pause.onExit();ui.onExit();play.onExit();drain();assert(d.handlers.empty());
 }
 // A scene can be retired before its onEnter synchronization callback runs.
 {CCNode scene;PlayLayer play;Blocker pause;scene.addChild(&play);scene.addChild(&pause);director->runningScene=&scene;PlayLayer::current=&play;
  play.onEnter();pause.onEnter();auto* input=SurvivalPauseActionInput::create(&pause,&play,[]{return true;});pause.addChild(input);input->onEnter();input->onExit();delete input;
  drain();pause.onExit();play.onExit();assert(d.handlers.empty());}
}
`;
 const dir=mkdtempSync(join(tmpdir(),'survival-pause-tap-'));try{
  writeFileSync(join(dir,'test.cpp'),code);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',...(debug?['-DCORUM_RANKED_DEBUG_BOT_MATCH=1']:[]),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
  execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
