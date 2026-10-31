import test from 'node:test';
import {writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
const root=resolve(new URL('..',import.meta.url).pathname);
test('production frame-boundary input preserves pending commands, timestamps and P1/P2; playback owns exact steps at every speed, pause, seek, restart and death',()=>{
 const json=process.env.CORUM_MATJSON_SOURCE,result=process.env.CORUM_RESULT_INCLUDE;
 if(!json||!result)throw Error('Pinned JSON dependencies required');
 const dir=mkdtempSync(join(tmpdir(),'replay-input-v2-'));
 try{
 writeFileSync(join(dir,'test.cpp'),`
#include "replay/SurvivalReplayInputBoundary.hpp"
#include "replay/SurvivalReplayPlayback.hpp"
#include <cassert>
using namespace corum::ranked::replay;
struct Command{int m_button=1;bool m_isPush=false,m_isPlayer2=false;int m_step=0;double m_timestamp=0;};
Json queue(std::initializer_list<Command> c){return encodeReplayButtons(std::vector<Command>(c));}
Document tape(){Document d;d.complete=true;d.metadata["nativeInputVersion"]=2;Json init;init["kind"]="initial";init["queued"]=queue({});d.records.push_back(init);
 for(int i=0;i<40;++i){Json f;f["kind"]="frame";f["dt"]=.01;f["queued"]=queue({});f["preInputs"]=Json::array();d.records.push_back(f);
 Json step;step["kind"]="step";step["tick"]=i;step["dt"]=.01;step["half"]=false;step["last"]=true;step["nativeStep"]=364+i;d.records.push_back(step);
 Json dispatch;dispatch["kind"]="dispatch";dispatch["button"]=1;dispatch["down"]=(i<15||i%2==0);dispatch["player2"]=(i%3==0);dispatch["nativeStep"]=364+i;dispatch["timestamp"]=i*.01;dispatch["before"]=Json::array();dispatch["after"]=Json::array();dispatch["before"].push(false);dispatch["after"].push(dispatch["down"]);d.records.push_back(dispatch);
 Json pose;pose["kind"]="pose";pose["x"]=i;pose["y"]=100;pose["mode"]=i<20?"CUBE":"SHIP";pose["rotation"]=i<20?2.703:-2.703;pose["dead"]=i==39;pose["physics"]["pendingButtons"]=queue({});pose["physics"]["playerUpdates"]=1;pose["physics"]["held"]=dispatch["down"];d.records.push_back(pose);
 }return d;}
void advance(Playback& p,int& step){auto* f=p.current();p.nativeStep(.01,false,true,step++);p.nativeDispatch(f->dispatches[0]);p.completeFrame(f->poses[0]);}
int main(){
 Command future{1,false,true,999,.12345},press{1,true,false,364,.001},release{1,false,false,365,.005},extra{2,true,true,99,.2};
 std::vector<Command> actual{future,extra};auto carry=queue({future}),source=queue({future,press,press,release});
 assert(prepareReplayInputBoundary(actual,source,carry).empty());assert(encodeReplayButtons(actual)==source);assert(actual[0].m_timestamp==.12345&&actual[0].m_isPlayer2);
 auto saved=actual;assert(prepareReplayInputBoundary(actual,queue({press}),carry)=="SOURCE_PENDING_INPUT_LOST");assert(encodeReplayButtons(actual)==encodeReplayButtons(saved));
 actual={press};saved=actual;assert(prepareReplayInputBoundary(actual,source,carry)=="NATIVE_PENDING_INPUT_LOST");assert(encodeReplayButtons(actual)==encodeReplayButtons(saved));
 for(double speed:{.5,1.,2.,4.}){Playback p(tape());p.speed(speed);int step=364,total=0;
  p.pause(true);p.accumulate(.1);assert(!p.due());p.pause(false);
  while(!p.ended()){p.accumulate(.01);while(p.due()){advance(p,step);++total;}assert(!p.desync());}
  assert(total==40&&step==404);p.seek(.1);assert(p.takeReset());p.restart();step=364;while(p.due())advance(p,step);assert(!p.desync()&&step>=374&&step<=375);
 }
 Playback missing(tape());missing.nativeStep(.01,false,true,364);missing.completeFrame(missing.current()->poses[0]);assert(missing.desync()&&str(missing.failure(),"reason")=="UNCONSUMED_NATIVE_EVENTS");
 {// P0-C: bounded first-divergence report names the exact unconsumed Source evidence.
  auto report=missing.diagnosticsReport();assert(str(missing.failure(),"category")=="NATIVE_SCHEDULE");
  assert(report["schema"].asInt().unwrapOr(0)==1&&report["failure"]["expected"].isObject()&&report["sourceAfter"].asArray().unwrap().size()>=1);
  assert(report["stageCounts"]["recordedFrames"].asInt().unwrapOr(0)>=1&&report["verifiedBefore"].asArray().unwrap().size()<=Playback::kTrailBefore);
  assert(report.dump().size()<256*1024&&!report.dump().contains("token"));}
 Playback wrong(tape());auto event=wrong.current()->dispatches[0];event["after"][0]=false;wrong.nativeDispatch(event);assert(wrong.desync()&&str(wrong.failure(),"reason")=="HANDLE_BUTTON_after");
 Playback physics(tape());physics.nativeStep(.01,false,true,364);physics.nativeDispatch(physics.current()->dispatches[0]);auto pose=physics.current()->poses[0];pose["physics"]["held"]=false;physics.completeFrame(pose);assert(physics.desync()&&str(physics.failure(),"reason")=="NATIVE_PHYSICS_STATE");
 Json env;env["schema"]=2;env["cbf"]["loaded"]=false;assert(replayPhysicsEnvironmentProblem(env).empty());env["cbf"]["loaded"]=true;assert(replayPhysicsEnvironmentProblem(env)=="CBF_VERSION_UNVERIFIED");env["cbf"]["version"]="v1.5.0";env["cbf"]["disabled"]=true;env["cbf"]["clickOnSteps"]=false;env["cbf"]["physicsBypass"]=false;assert(replayPhysicsEnvironmentProblem(env).empty());env["cbf"]["disabled"]=false;assert(replayPhysicsEnvironmentProblem(env)=="CBF_NATIVE_BOUNDARIES_UNAVAILABLE");env["cbf"]["disabled"]=true;env["cbf"]["clickOnSteps"]=true;assert(replayPhysicsEnvironmentProblem(env)=="CBF_COS_PIPELINE_UNSUPPORTED");env["cbf"]["physicsBypass"]=true;assert(replayPhysicsEnvironmentProblem(env)=="CBF_PHYSICS_BYPASS_UNSUPPORTED");
}
`);
 execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-isystem',json+'/include','-isystem',result,'-I',join(root,'src'),join(dir,'test.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(p=>json+'/src/'+p+'.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
 execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
