import test from 'node:test';
import {mkdtempSync,writeFileSync,rmSync,readFileSync} from 'node:fs';
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
 static_assert(int(SurvivalOutcomeChoice::Observe)==1&&int(SurvivalOutcomeChoice::PersonalResult)==2&&int(SurvivalOutcomeChoice::LiveSpectate)==3);
 assert(survivalFlowStage("PLAYING","",false,SurvivalOutcomeChoice::LiveSpectate)==SurvivalFlowStage::LiveSpectating);
 assert(survivalFlowStage("PLAYING","",true,SurvivalOutcomeChoice::LiveSpectate)==SurvivalFlowStage::EliminatedChoice);
 assert(survivalFlowStage("PLAYING","",true,SurvivalOutcomeChoice::Observe)==SurvivalFlowStage::Observing);
 assert(survivalFlowStage("FINAL_RESULT","",true,SurvivalOutcomeChoice::LiveSpectate)==SurvivalFlowStage::Ceremony);
 assert(survivalFlowStage("PLAYING","",true,SurvivalOutcomeChoice::Undecided)==SurvivalFlowStage::EliminatedChoice);
 assert(survivalFlowStage("PLAYING","",true,SurvivalOutcomeChoice::PersonalResult)==SurvivalFlowStage::EliminatedChoice);
 assert(survivalFlowStage("PLAYING","",true,SurvivalOutcomeChoice::Standings)==SurvivalFlowStage::RoundResult);
 assert(survivalFlowStage("PREPARING","",false,SurvivalOutcomeChoice::LiveSpectate)==SurvivalFlowStage::Preparing);
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

test('production outcome selection is scoped to match, round and map generation and never keeps R1 spectator/results over R2',()=>{
 const dir=mkdtempSync(join(tmpdir(),'survival-outcome-context-'));try{
 const source=readFileSync(join(root,'SurvivalRuntime.cpp'),'utf8'),header=readFileSync(join(root,'SurvivalRuntime.hpp'),'utf8');
 function method(signature){const a=source.indexOf(signature);if(a<0)throw Error(signature);let p=source.indexOf('{',a)+1,n=1;while(n){n+=(source[p]==='{')-(source[p]==='}');++p;}return source.slice(a,p);}
 const options=header.split('\n').find(line=>line.includes('std::string optionsContext()'));
 writeFileSync(join(dir,'test.cpp'),`
#include "domain/SurvivalFlowPolicy.hpp"
#include <map>
#include <variant>
#include <cassert>
struct Mod{std::map<std::string,std::variant<std::string,int>> saved;static Mod*get(){static Mod m;return &m;}
 template<class T>T getSavedValue(std::string const&key,T fallback=T{}){auto i=saved.find(key);return i!=saved.end()?std::get<T>(i->second):fallback;}
 template<class T>void setSavedValue(std::string const&key,T value){saved[key]=value;}void saveData(){}};
namespace corum::ranked{struct SurvivalRuntime{
 struct View{std::string id="match",roundKey="stage-1",state="PLAYING";int mapGeneration=1;bool ownEliminated=false;}m_view;bool cleared=true,free=false;int lock=0;
 bool active()const{return !m_view.id.empty();}bool officialRoundCleared()const{return cleared&&!m_view.ownEliminated&&m_view.state=="PLAYING";}bool freePlaying()const{return free;}int watchLockSeconds()const{return lock;}
 ${options}
 SurvivalOutcomeChoice outcomeChoice()const;void chooseOutcome(SurvivalOutcomeChoice);
};
${method('SurvivalOutcomeChoice SurvivalRuntime::outcomeChoice()')}
${method('void SurvivalRuntime::chooseOutcome(')}
}
int main(){using namespace corum::ranked;SurvivalRuntime r;r.chooseOutcome(SurvivalOutcomeChoice::LiveSpectate);assert(r.outcomeChoice()==SurvivalOutcomeChoice::LiveSpectate);
 r.m_view.roundKey="stage-2";r.m_view.state="PREPARING";assert(r.outcomeChoice()==SurvivalOutcomeChoice::Undecided);r.chooseOutcome(SurvivalOutcomeChoice::PersonalResult);assert(r.outcomeChoice()==SurvivalOutcomeChoice::Undecided);
 r.m_view.state="PLAYING";r.chooseOutcome(SurvivalOutcomeChoice::LiveSpectate);r.m_view.mapGeneration=2;assert(r.outcomeChoice()==SurvivalOutcomeChoice::Undecided);
 r.m_view.ownEliminated=true;r.chooseOutcome(SurvivalOutcomeChoice::Standings);assert(r.outcomeChoice()==SurvivalOutcomeChoice::Standings);r.lock=5;r.chooseOutcome(SurvivalOutcomeChoice::Observe);assert(r.outcomeChoice()==SurvivalOutcomeChoice::Standings);r.lock=0;r.chooseOutcome(SurvivalOutcomeChoice::Observe);assert(r.outcomeChoice()==SurvivalOutcomeChoice::Observe);
 r.m_view.roundKey="stage-3";assert(r.outcomeChoice()==SurvivalOutcomeChoice::Undecided);r.chooseOutcome(SurvivalOutcomeChoice::LiveSpectate);assert(r.outcomeChoice()==SurvivalOutcomeChoice::Undecided);
 r.m_view.state="FINAL_RESULT";r.chooseOutcome(SurvivalOutcomeChoice::PersonalResult);assert(r.outcomeChoice()==SurvivalOutcomeChoice::PersonalResult);
 // A legacy match-only saved choice must not capture a new round.
 Mod::get()->setSavedValue("survival-outcome-context",r.m_view.id);assert(r.outcomeChoice()==SurvivalOutcomeChoice::Undecided);
}
`);
 execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',root,join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
