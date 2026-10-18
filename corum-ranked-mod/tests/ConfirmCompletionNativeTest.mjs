import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const source=readFileSync(process.env.CORUM_CONFIRM_SOURCE ?? new URL('../src/ui/RankedConfirmPopup.hpp',import.meta.url),'utf8').split('class RankedConfirmPopup final')[1];
function method(name){const start=source.indexOf(name);assert(start>=0);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');++end;}return source.slice(start,end).replaceAll('log::','geode::log::');}
for(const scenario of ['paused','cancel','scene'])test(`production confirm completion validates attachment and cancellation (${scenario})`,()=>{
const cpp=`
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
struct CCObject{};
struct CCNode{std::shared_ptr<int>life=std::make_shared<int>(0);CCNode*parent=nullptr;bool running=true;CCNode*getParent(){return parent;}bool isRunning(){return running;}virtual~CCNode()=default;};
struct CCDirector{CCNode*scene=nullptr;CCNode*next=nullptr;static CCDirector*sharedDirector(){static CCDirector d;return &d;}CCNode*getRunningScene(){return scene;}CCNode*getNextScene(){return next;}};
template<class T>struct Ref{T*p;explicit Ref(T*v):p(v){}T*data()const{return p;}T*operator->()const{return p;}};
template<class T>struct WeakRef{T*p;std::weak_ptr<int>life;explicit WeakRef(T*v):p(v){if(v)life=v->life;}struct Locked{T*p;T*data()const{return p;}T*operator->()const{return p;}explicit operator bool()const{return p;}};auto lock()const{return Locked{life.expired()?nullptr:p};}};
namespace geode::log{template<class...T>void info(T...){} }
std::vector<std::function<void()>>queued;void queueInMainThread(std::function<void()>fn){queued.push_back(std::move(fn));}void drain(){auto work=std::exchange(queued,{});for(auto&fn:work)fn();}
struct CCTouchDispatcher{static CCTouchDispatcher*get(){static CCTouchDispatcher d;return &d;}void unregisterForcePrio(CCNode*){}};
struct Popup:CCNode{int closes=0;void onClose(CCObject*){++closes;parent=nullptr;onExit();}virtual void onExit(){}};
struct RankedConfirmPopup:Popup{bool m_closed=false,m_exited=false;std::function<void(bool)>m_done;
 ${method('void finish(bool accepted)')}
 ${method('void cancel()')}
 ${method('void onExit()')}
};
int main(){CCNode scene,other;auto*d=CCDirector::sharedDirector();d->scene=&scene;RankedConfirmPopup popup;popup.parent=&scene;
 int submitted=0;popup.m_done=[&](bool accepted){if(accepted)++submitted;};
 std::string scenario="${scenario}";if(scenario=="paused")popup.running=false;
 popup.finish(true);popup.finish(true);assert(queued.size()==1&&submitted==0);
 if(scenario=="cancel")popup.cancel();
 if(scenario=="scene")d->scene=&other;
 drain();assert(submitted==(scenario=="paused"?1:0));
}
`;
const dir=mkdtempSync(join(tmpdir(),'confirm-completion-'));try{writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
