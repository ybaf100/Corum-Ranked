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
struct PlayLayer {inline static PlayLayer* current=nullptr;bool m_isPaused=true;static PlayLayer* get(){return current;}};
template<class T>struct Ref{T* p=nullptr;T* data()const{return p;}T* operator->()const{return p;}explicit operator bool()const{return p;}};
template<class T>struct WeakRef{T* p;explicit WeakRef(T* v):p(v){}Ref<T> lock()const{return {p};}};
struct PauseLayer{bool parent=true,refuseResume=false;int resumes=0;void* getParent(){return parent?this:nullptr;}void onResume(CCObject*){++resumes;if(!refuseResume){PlayLayer::get()->m_isPaused=false;parent=false;}}};
struct Popup{int dismisses=0;void dismiss(){++dismisses;}};
struct RankedRuntime{int ends=0;static RankedRuntime& get(){static RankedRuntime r;return r;}void endMatchGameplayPause(std::string const& lease){ends+=!lease.empty();}};
std::optional<WeakRef<PauseLayer>> activeRankedPause;
#define schedule_selector(x) 0
struct CorumRankedPauseLayer:PauseLayer {struct Fields{std::optional<WeakRef<Popup>> popup;std::optional<WeakRef<PlayLayer>> owner;std::string leaseId="lease";}fields;Fields* m_fields=&fields;void unschedule(int){}
${body('void retireRankedPause()')}
${body('void onResume(CCObject* sender)')}
};
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
  assert(!a.m_isPaused&&pause.resumes==1&&dialog.dismisses==1&&!activeRankedPause&&!pause.fields.owner&&!pause.fields.popup&&pause.fields.leaseId.empty());
  assert(resumeRankedPauseBeforeQuit(&a)&&pause.resumes==1); // no duplicate Resume
 }
 {CorumRankedPauseLayer detached;detached.parent=false;detached.fields.owner.emplace(&a);activeRankedPause.emplace(&detached);
  assert(resumeRankedPauseBeforeQuit(&a)&&detached.resumes==0);activeRankedPause.reset();}
 {CorumRankedPauseLayer refusal;a.m_isPaused=true;refusal.refuseResume=true;refusal.fields.owner.emplace(&a);activeRankedPause.emplace(&refusal);
  assert(!resumeRankedPauseBeforeQuit(&a)&&a.m_isPaused&&refusal.resumes==1); // no forced flags
 }
 // New match R1 owns a fresh Pause, independent of old scene/match lifetime.
 {CorumRankedPauseLayer next;PlayLayer::current=&b;next.fields.owner.emplace(&b);activeRankedPause.emplace(&next);
  assert(resumeRankedPauseBeforeQuit(&b)&&!b.m_isPaused&&next.resumes==1&&!activeRankedPause);}
}
`;
 const dir=mkdtempSync(join(tmpdir(),'corum-pause-quit-'));
 try{const file=join(dir,'test.cpp');writeFileSync(file,cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',file,'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}finally{rmSync(dir,{recursive:true,force:true});}
});
