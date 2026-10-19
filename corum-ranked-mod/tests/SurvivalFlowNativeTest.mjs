import test from 'node:test';
import {mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('../src',import.meta.url));
test('Survival server-clock choice, independent result state, countdown boundaries',()=>{
 const dir=mkdtempSync(join(tmpdir(),'survival-flow-'));try{
 writeFileSync(join(dir,'test.cpp'),String.raw`
#include "domain/SurvivalFlowPolicy.hpp"
#include "domain/SurvivalFreePlayPolicy.hpp"
#include <cassert>
int main(){using namespace corum::ranked;
 assert(survivalWatchLock(1000,1000)==5);assert(survivalWatchLock(5000,1000)==1);assert(survivalWatchLock(6000,1000)==0);
 assert(survivalWatchLock(999999,1000)==0); // re-entry never restarts lock
 assert(survivalFlowStage("PLAYING","",true,SurvivalOutcomeChoice::Undecided)==SurvivalFlowStage::EliminatedChoice);
 assert(survivalFlowStage("FINAL_RESULT","",true,SurvivalOutcomeChoice::PersonalResult)==SurvivalFlowStage::PersonalResult);
 assert(survivalFlowStage("FINAL_RESULT","",true,SurvivalOutcomeChoice::Observe)==SurvivalFlowStage::Ceremony);
 for(int digit=1;digit<=5;++digit){auto f=survivalReadyFrame(digit*1000-200);assert(f.digit==digit&&f.opacity>0&&f.scale>=1&&f.scale<=1.2);}
 assert(survivalReadyFrame({}).digit==0);assert(survivalReadyFrame(0).digit==0);assert(survivalReadyFrame(6000).digit==0);
 SurvivalFreePlayState observer;observer.begin({"m","r",1},"",true);
 for(int i=0;i<500;++i)assert(!observer.needsReturn({"m","r",1},"PLAYING","",true));
 assert(observer.needsReturn({"m","r2",1},"PREPARING","r",true));
 observer.finish();assert(!observer.active&&!observer.observer);
 SurvivalFreePlayState normal;normal.begin({"m","r",1},"");assert(normal.needsReturn({"m","r",1},"PLAYING","",true));
}`);
 execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',root,join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
