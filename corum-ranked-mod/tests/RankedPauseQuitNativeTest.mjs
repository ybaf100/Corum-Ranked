import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const source=readFileSync(new URL('../src/RankedPauseLayer.cpp',import.meta.url),'utf8');
const play=readFileSync(new URL('../src/PlayLayer.cpp',import.meta.url),'utf8');
function body(name){const at=source.indexOf(name);assert(at>=0);let end=source.indexOf('{',at)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');++end;}return source.slice(at,end);}
test('every Ranked native quit prepares its exact Pause before cleanup; no new native hook or global flag reset',()=>{
 const at=play.indexOf('void onQuit() {');const cleanup=play.indexOf('m_fields->spectatorPresentationRetired',at);
 assert(at>=0&&cleanup>at);
 assert.match(play.slice(at,cleanup),/resumeRankedPauseBeforeQuit\(this\)/);
 assert.doesNotMatch(source,/m_gamePaused\s*=|m_isPaused\s*=/);
 assert.match(source,/m_fields->owner.emplace\(layer\)/);
 assert.match(body('void onResume(CCObject* sender)'),/Ref<PauseLayer> keepAlive\(this\)/);
 assert.match(body('void onQuit(CCObject* sender)'),/Ref<PauseLayer> keepAlive\(this\)/);
 assert.match(body('bool resumeRankedPauseBeforeQuit(PlayLayer* layer)'),/pause->onResume\(nullptr\)/);
 assert.doesNotMatch(body('bool resumeRankedPauseBeforeQuit(PlayLayer* layer)'),/rankedPause->onResume/);
 const concession=play.slice(play.indexOf('bool finishConcededGameplayHandoff()'),play.indexOf('void spectatorFrame(float dt)'));
 assert.match(concession,/layer->onQuit\(\)/);
 assert.doesNotMatch(concession,/ranked->onQuit\(\)/);
 assert.equal((source.match(/class \$modify/g)||[]).length,1);
});
test('actual quit preparation resumes only the owned Pause and rejects failed native Resume',()=>{
 // Real Corum helper + retirement callbacks; native Resume is an explicit
 // boundary double. This tests ordering/ownership, NOT GD's internal pause flag.
 const cpp=`
#include <cassert>
#include <optional>
#include <string>
struct CCObject{};
namespace geode::log {template<class... T>void info(T...) {}}
struct PlayLayer {inline static PlayLayer* current=nullptr;bool m_isPaused=true;static PlayLayer* get(){return current;}};
template<class T>struct Ref{T* p=nullptr;T* data()const{return p;}T* operator->()const{return p;}explicit operator bool()const{return p;}};
template<class T>struct WeakRef{T* p;explicit WeakRef(T* v):p(v){}Ref<T> lock()const{return {p};}};
struct PauseLayer{bool parent=true,refuseResume=false,dispatching=false;int resumes=0,hookEntries=0;void* getParent(){return parent?this:nullptr;}void onResume(CCObject*);void nativeResume(){++resumes;if(!refuseResume){PlayLayer::get()->m_isPaused=false;parent=false;}}};
struct Popup{int dismisses=0;void dismiss(){++dismisses;}};
struct RankedRuntime{int ends=0;static RankedRuntime& get(){static RankedRuntime r;return r;}void endMatchGameplayPause(std::string const& lease){ends+=!lease.empty();}};
std::optional<WeakRef<PauseLayer>> activeRankedPause;
#define schedule_selector(x) 0
struct CorumRankedPauseLayer:PauseLayer {struct Fields{std::optional<WeakRef<Popup>> popup;std::optional<WeakRef<PlayLayer>> owner;std::string leaseId="lease";bool nativeQuitInProgress=false,nativeQuitReachedPlay=false;}fields;Fields* m_fields=&fields;void unschedule(int){}
${body('void retireRankedPause()')}
${body('void releaseRankedPauseOwnership()')}
${body('void onResume(CCObject* sender)')}
};
// Model the binding/next-original distinction; no actual Tulip/GD runtime.
void PauseLayer::onResume(CCObject* sender){if(dispatching){nativeResume();return;}dispatching=true;++hookEntries;static_cast<CorumRankedPauseLayer*>(this)->onResume(sender);dispatching=false;}
${body('bool resumeRankedPauseBeforeQuit(PlayLayer* layer)')}
int main(){
 PlayLayer a,b;PlayLayer::current=&a;
 // Ordinary/Placement Pause is never claimed by the Ranked helper.
 assert(resumeRankedPauseBeforeQuit(&a)&&a.m_isPaused);
 assert(resumeRankedPauseBeforeQuit(nullptr));
 {CorumRankedPauseLayer pause;Popup dialog;pause.fields.owner.emplace(&a);pause.fields.popup.emplace(&dialog);activeRankedPause.emplace(&pause);
  // Another scene cannot resume, retire or release this Pause.
  PlayLayer::current=&b;assert(resumeRankedPauseBeforeQuit(&b));assert(pause.resumes==0&&activeRankedPause);
  assert(resumeRankedPauseBeforeQuit(&a));assert(pause.resumes==0);
  PlayLayer::current=&a;assert(resumeRankedPauseBeforeQuit(&a));
  assert(!a.m_isPaused&&pause.resumes==1&&pause.hookEntries==1&&dialog.dismisses==1&&!activeRankedPause&&!pause.fields.owner&&!pause.fields.popup&&pause.fields.leaseId.empty());
  assert(resumeRankedPauseBeforeQuit(&a)&&pause.resumes==1); // no duplicate Resume
 }
 {CorumRankedPauseLayer detached;detached.parent=false;detached.fields.owner.emplace(&a);activeRankedPause.emplace(&detached);
  assert(resumeRankedPauseBeforeQuit(&a)&&detached.resumes==0&&!activeRankedPause&&!detached.fields.owner&&detached.fields.leaseId.empty());}
 {CorumRankedPauseLayer refusal;a.m_isPaused=true;refusal.refuseResume=true;refusal.fields.owner.emplace(&a);activeRankedPause.emplace(&refusal);
  assert(!resumeRankedPauseBeforeQuit(&a)&&a.m_isPaused&&refusal.resumes==1); // no forced flags
  assert(!resumeRankedPauseBeforeQuit(&a)&&a.m_isPaused&&refusal.resumes==2); // repeated Quit cannot bypass refusal
  refusal.parent=false;assert(!resumeRankedPauseBeforeQuit(&a)&&refusal.resumes==2);
  assert(activeRankedPause&&refusal.fields.owner);
  a.m_isPaused=false;assert(resumeRankedPauseBeforeQuit(&a)&&refusal.resumes==2&&!activeRankedPause&&!refusal.fields.owner); // externally completed native recovery
 }
 // New match R1 owns a fresh Pause, independent of old scene/match lifetime.
 {CorumRankedPauseLayer next;PlayLayer::current=&b;next.fields.owner.emplace(&b);activeRankedPause.emplace(&next);
  assert(resumeRankedPauseBeforeQuit(&b)&&!b.m_isPaused&&next.resumes==1&&!activeRankedPause);}
}
`;
 const dir=mkdtempSync(join(tmpdir(),'corum-pause-quit-'));
 try{const file=join(dir,'test.cpp');writeFileSync(file,cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',file,'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}finally{rmSync(dir,{recursive:true,force:true});}
});

test('actual Resume callback keeps the removed Pause alive until ownership cleanup finishes',()=>{
 // Cocos retain/release model: native Resume drops the scene's reference.
 // No helper Ref is held, matching a native menu callback entry.
 const template=resume=>`
#include <cassert>
#include <optional>
#include <string>
#include <utility>
struct CCObject{};
struct Object {int refs=1;bool alive=true;void retain(){assert(alive);++refs;}void release(){assert(alive&&refs>0);if(--refs==0)alive=false;}};
struct PlayLayer:Object {bool m_isPaused=true;inline static PlayLayer* current=nullptr;static PlayLayer* get(){return current;}};
template<class T>struct Ref{T* p=nullptr;Ref()=default;explicit Ref(T* v):p(v){if(p)p->retain();}Ref(Ref const& v):Ref(v.p){}Ref(Ref&& v):p(std::exchange(v.p,nullptr)){}~Ref(){if(p)p->release();}T* data()const{return p;}T* operator->()const{assert(p&&p->alive);return p;}explicit operator bool()const{return p;}};
template<class T>struct WeakRef {T* p;explicit WeakRef(T* v):p(v){p->retain();}~WeakRef(){p->release();}Ref<T> lock()const{return Ref<T>(p);}};
struct PauseLayer:Object {void onResume(CCObject*){PlayLayer::get()->m_isPaused=false;release();}};
struct Popup:Object{void dismiss(){}};
struct RankedRuntime{static RankedRuntime& get(){static RankedRuntime r;return r;}void endMatchGameplayPause(std::string const&) {}};
std::optional<WeakRef<PauseLayer>> activeRankedPause;
#define schedule_selector(x) 0
struct CorumRankedPauseLayer:PauseLayer {
 struct Fields{std::optional<WeakRef<Popup>> popup;std::optional<WeakRef<PlayLayer>> owner;std::string leaseId="lease";}fields;
 struct CheckedFields {Fields* p;PauseLayer* owner;Fields* operator->(){assert(owner->alive);return p;}}m_fields{&fields,this};
 void unschedule(int){}
 ${body('void retireRankedPause()')}
 ${body('void releaseRankedPauseOwnership()')}
 ${resume}
};
int main(){PlayLayer play;PlayLayer::current=&play;CorumRankedPauseLayer pause;pause.fields.owner.emplace(&play);activeRankedPause.emplace(&pause);assert(pause.refs==2);
 pause.onResume(nullptr);assert(!play.m_isPaused&&!activeRankedPause&&!pause.fields.owner&&play.refs==1&&!pause.alive&&pause.refs==0);
}
`;
 const dir=mkdtempSync(join(tmpdir(),'corum-pause-retain-'));
 try{
  const actual=body('void onResume(CCObject* sender)');
  for(const [name,method] of [['without-retain',actual.replace('[[maybe_unused]] Ref<PauseLayer> keepAlive(this);','')],['with-retain',actual]]){
   const file=join(dir,name+'.cpp');writeFileSync(file,template(method));
   execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',file,'-o',join(dir,name)],{stdio:'pipe'});
   if(name==='without-retain')assert.throws(()=>execFileSync(join(dir,name),[],{stdio:'pipe'}));
   else execFileSync(join(dir,name),[],{stdio:'pipe'});
  }
 }finally{rmSync(dir,{recursive:true,force:true});}
});
