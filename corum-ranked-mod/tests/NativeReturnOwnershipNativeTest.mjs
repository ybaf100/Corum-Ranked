import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const source=readFileSync(new URL('../src/RankedRoundPreparation.cpp',import.meta.url),'utf8');
function method(name){const start=source.indexOf(name);assert(start>=0);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');++end;}return source.slice(start,end).replaceAll('RankedRoundPreparation::','');}

test('actual deferred native return probes the weak pool AFTER scene exit, retires the GD owner and never pops another scene',()=>{
 const code=`
#include <cassert>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
struct Node{int refs=1;bool running=false,alive=true;std::vector<Node*>children;virtual ~Node()=default;
 void retain(){assert(alive);++refs;}void release(){assert(alive&&refs>0);if(--refs==0){alive=false;for(auto*c:children)c->release();retired();}}
 virtual void retired(){}bool isRunning(){return running;}};
struct CCScene:Node{};struct GJGameLevel{};
struct AppDelegate{bool m_gamePaused=false;static AppDelegate*get(){static AppDelegate a;return &a;}};
struct PlayLayer:Node{static inline PlayLayer* current=nullptr;void retired()override{if(current==this)current=nullptr;}};
namespace geode{
 template<class T>struct Ref{T*p=nullptr;Ref()=default;Ref(T*v):p(v){if(p)p->retain();}Ref(Ref const&r):Ref(r.p){}Ref(Ref&&r):p(std::exchange(r.p,nullptr)){}~Ref(){if(p)p->release();}T*data()const{return p;}T*operator->()const{return p;}explicit operator bool()const{return p;}};
 struct Control{Node*p;};inline std::map<Node*,std::shared_ptr<Control>> pool;
 inline void check(std::shared_ptr<Control>const&c){if(c&&c->p&&c->p->refs==1){auto*p=c->p;pool.erase(p);c->p=nullptr;p->release();}}
 // Like Geode 5.11.2, all WeakRefs share ONE pool-owned native retain.
 template<class T>struct WeakRef{std::shared_ptr<Control>c;WeakRef()=default;explicit WeakRef(T*p){if(p){auto[it,added]=pool.try_emplace(p);if(added){p->retain();it->second=std::make_shared<Control>(Control{p});}c=it->second;}}WeakRef(WeakRef const&)=default;WeakRef(WeakRef&&)=default;WeakRef&operator=(WeakRef const&)=default;WeakRef&operator=(WeakRef&&)=default;~WeakRef(){check(c);}
  Ref<T>lock()const{check(c);return Ref<T>(c?static_cast<T*>(c->p):nullptr);}};
}
namespace geode::log{template<class...T>void info(T...){}}
using namespace geode;
std::vector<std::function<void()>>queued;void queueInMainThread(std::function<void()>fn){queued.push_back(std::move(fn));}
void drain(){auto tasks=std::exchange(queued,{});for(auto&fn:tasks)fn();}
struct CCDirector{std::vector<CCScene*>stack;CCScene*next=nullptr;int pops=0;static CCDirector*sharedDirector(){static CCDirector d;return &d;}CCScene*getRunningScene(){return stack.back();}CCScene*getNextScene(){return next;}
 int levelForSceneInStack(CCScene*s){auto it=std::find(stack.begin(),stack.end(),s);return it==stack.end()?-1:static_cast<int>(it-stack.begin());}
 void popScene(){assert(stack.size()>1);auto*s=stack.back();stack.pop_back();s->running=false;s->release();stack.back()->running=true;++pops;}};
struct Prep{bool m_returnQueued=false,m_returnPopped=false,m_quitting=true;int m_launchGeneration=1;struct LevelRef{GJGameLevel*p=nullptr;LevelRef&operator=(GJGameLevel*v){p=v;return *this;}GJGameLevel*data(){return p;}explicit operator bool()const{return p;}}m_launchedLevel;
 std::optional<geode::WeakRef<CCScene>>m_nativePauseRestoreOrigin;geode::WeakRef<PlayLayer>m_play;geode::WeakRef<CCScene>m_origin,m_returnScene;
 ${method('bool RankedRoundPreparation::returnFromNativeQuit(')}
 ${method('void RankedRoundPreparation::drainNativeReturn(')}
};
int main(){auto*d=CCDirector::sharedDirector();CCScene origin,scene;PlayLayer play;GJGameLevel level;
 scene.children={&play};scene.running=true;d->stack={&origin,&scene};PlayLayer::current=&play;
 {Prep p;p.m_launchedLevel=&level;p.m_play=geode::WeakRef<PlayLayer>(&play);p.m_origin=geode::WeakRef<CCScene>(&origin);
  assert(p.returnFromNativeQuit(&level)&&p.returnFromNativeQuit(&level)&&queued.size()==1);
  p.drainNativeReturn();assert(play.alive&&d->pops==0);p.m_quitting=false;drain();
  // At pop's return the queue still held the old scene. No global pointer is
  // cleared manually; the next heartbeat must release Scene, then PlayLayer.
  assert(d->pops==1&&PlayLayer::current==&play&&play.alive);
  p.drainNativeReturn();assert(!scene.alive&&!play.alive&&PlayLayer::current==nullptr&&!p.m_returnQueued);
  p.drainNativeReturn();assert(d->pops==1);
 }
 CCScene foreign,old;PlayLayer second;old.children={&second};old.running=true;d->stack={&origin,&old};PlayLayer::current=&second;
 {Prep p;p.m_launchedLevel=&level;p.m_play=geode::WeakRef<PlayLayer>(&second);p.m_origin=geode::WeakRef<CCScene>(&origin);assert(p.returnFromNativeQuit(&level));p.m_quitting=false;
  // A foreign scene replaces the exact owner before the deferred callback.
  d->stack.push_back(&foreign);foreign.running=true;drain();p.drainNativeReturn();assert(d->pops==1&&second.alive&&p.m_returnQueued);
  d->stack.pop_back();d->next=&foreign;p.drainNativeReturn();assert(second.alive);d->next=nullptr;
  old.running=false;d->stack.pop_back();old.release();p.drainNativeReturn();assert(!second.alive&&!p.m_returnQueued&&d->pops==1);
 }
}
`;
 const dir=mkdtempSync(join(tmpdir(),'ranked-return-ownership-'));try{writeFileSync(join(dir,'test.cpp'),code);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
