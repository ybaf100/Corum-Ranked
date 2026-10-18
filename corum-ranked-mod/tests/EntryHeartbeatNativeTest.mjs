import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const source=readFileSync(process.env.CORUM_ENTRY_PLAY_SOURCE ?? join(root,'src/PlayLayer.cpp'),'utf8');
function method(signature){const start=source.indexOf(signature);assert(start>=0);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');end++;}return source.slice(start,end).replaceAll('log::info','geode::log::info').replaceAll('std::chrono::steady_clock::now()','pulseNow');}
test('production process pulse services Solo and Survival held entry even when scene-local updates report stopped',()=>{
 const cpp=`#include <chrono>
#include <cassert>
namespace geode::log{template<class...T>void info(T const&...){}}
struct Node{Node*parent=nullptr;bool running=true;Node*getParent(){return parent;}bool isRunning(){return running;}};
struct PlayLayer:Node{static inline PlayLayer*current=nullptr;bool m_isPaused=false;static PlayLayer*get(){return current;}};
struct CCDirector{Node*scene=nullptr,*next=nullptr;static CCDirector*sharedDirector(){static CCDirector d;return &d;}Node*getRunningScene(){return scene;}Node*getNextScene(){return next;}};
auto pulseNow=std::chrono::steady_clock::time_point{};
struct CorumRankedPlayLayer:PlayLayer{struct Fields{bool rankedInitialized=true,rankedLevel=true,survivalLevel=false,nativeEntryHeld=true,entryNativeCompletionInProgress=false,autoExitRequested=false,safetyRetired=false;int levelId=123,independentEntryLogCount=0;std::chrono::steady_clock::time_point nextIndependentEntryPulseAt{};}fields;Fields*m_fields=&fields;int pulses=0;void blockEntryGameplayUpdate(){++pulses;}};
namespace corum::ranked{void pumpHeldRankedEntry(PlayLayer*);}
${method('void corum::ranked::pumpHeldRankedEntry(PlayLayer* layer)')}
int main(){Node scene,other;auto*d=CCDirector::sharedDirector();d->scene=&scene;
 for(bool survival:{false,true}){CorumRankedPlayLayer p;p.fields.survivalLevel=survival;p.fields.rankedLevel=!survival;p.parent=&scene;p.running=false;PlayLayer::current=&p;
  corum::ranked::pumpHeldRankedEntry(&p);assert(p.pulses==1);
  corum::ranked::pumpHeldRankedEntry(&p);assert(p.pulses==1);pulseNow+=std::chrono::milliseconds(100);
  corum::ranked::pumpHeldRankedEntry(&p);assert(p.pulses==2);
  p.fields.nativeEntryHeld=false;pulseNow+=std::chrono::milliseconds(100);corum::ranked::pumpHeldRankedEntry(&p);assert(p.pulses==2);
 }
 for(int invalid=0;invalid<7;++invalid){CorumRankedPlayLayer p;p.parent=&scene;PlayLayer::current=&p;
  if(invalid==0){p.fields.rankedInitialized=false;}if(invalid==1){p.fields.rankedLevel=false;}if(invalid==2){p.fields.safetyRetired=true;}
  if(invalid==3){p.fields.autoExitRequested=true;}if(invalid==4){p.parent=&other;}if(invalid==5){d->next=&other;}if(invalid==6){PlayLayer::current=nullptr;}
  corum::ranked::pumpHeldRankedEntry(&p);assert(p.pulses==0);d->next=nullptr;
 }
}`;
 const dir=mkdtempSync(join(tmpdir(),'ranked-entry-pulse-'));try{writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
