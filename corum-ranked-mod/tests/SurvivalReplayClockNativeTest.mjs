import test from 'node:test';import{writeFileSync,mkdtempSync,rmSync}from'node:fs';import{execFileSync}from'node:child_process';import{join,resolve}from'node:path';import{tmpdir}from'node:os';
const root=resolve(new URL('..',import.meta.url).pathname);
test('production input clock replays the beta119 frame0 across three host sessions, checks unchanged writes and reports the failing physics field',()=>{
 const dir=mkdtempSync(join(tmpdir(),'replay-clock-'));try{
 const json=process.env.CORUM_MATJSON_SOURCE,result=process.env.CORUM_RESULT_INCLUDE;if(!json||!result)throw Error('Pinned dependencies required');
 writeFileSync(join(dir,'test.cpp'),`
#include "replay/SurvivalReplayPlayback.hpp"
#include <cassert>
#include <iostream>
using namespace corum::ranked::replay;
int main(){
 constexpr double sourceExit=70163.98234975;
 // Actual reported native clocks, not a fixed offset selected for the test.
 for(double host:{70189.84794037501,70193.55499645833,70197.469151875}){
  ReplayInputClock legacy;legacy.replay(nullptr,0,sourceExit);double live=0;auto frame=legacy.enter("FRAME",live);live=host;legacy.leave(frame,live);legacy.finish();if(!legacy.error().empty())std::cerr<<legacy.error()<<" "<<legacy.observations().dump()<<"\\n";assert(legacy.error().empty()&&live==sourceExit);
 }
 ReplayInputClock record;record.source();double source=0;auto f=record.enter("FRAME",source);auto d=record.enter("DELTA",source);record.leave(d,source);source=sourceExit;record.leave(f,source);auto trace=record.finish();assert(replayClockDataValid(trace));
 for(double speed:{.5,1.,2.,4.})for(double host:{70189.84794037501,70193.55499645833,70197.469151875}){
  (void)speed;ReplayInputClock clock;clock.replay(trace,0,sourceExit);double live=0;auto frame=clock.enter("FRAME",live);auto delta=clock.enter("DELTA",live);clock.leave(delta,live);live=host;clock.leave(frame,live);clock.finish();assert(clock.error().empty()&&live==sourceExit);
 }
 ReplayInputClock bad;bad.replay(trace,0,sourceExit);double live=0;auto frame=bad.enter("FRAME",live);(void)frame;auto delta=bad.enter("DELTA",live);live=1;bad.leave(delta,live);assert(bad.error()=="INPUT_CLOCK_UNEXPECTED_WRITE");
 auto wrong=trace;wrong["events"][1]["phase"]="STEP";ReplayInputClock order;order.replay(wrong,0,sourceExit);live=0;order.enter("FRAME",live);order.enter("DELTA",live);assert(order.error()=="INPUT_CLOCK_BOUNDARY_ORDER");
 Document doc;doc.complete=true;doc.metadata["nativeInputVersion"]=2;Json initial;initial["kind"]="initial";Json header;header["kind"]="frame";header["dt"]=1./240;Json pose;pose["kind"]="pose";pose["x"]=0;pose["y"]=105;pose["visible"]=true;pose["cameraX"]=15;pose["physics"]["timestamp"]=sourceExit;pose["physics"]["p1"]=Json::array();doc.records={initial,header,pose};Playback replay(doc);
 auto actual=pose;actual["visible"]=false;actual["cameraX"]=-165;actual["physics"]["timestamp"]=70189.84794037501;replay.completeFrame(actual);assert(replay.failure()["reason"].asString().unwrap()=="NATIVE_PHYSICS_STATE");assert(replay.failure()["firstDifferentField"].asString().unwrap()=="$.physics.timestamp");
 Playback visual(doc);actual["physics"]=pose["physics"];visual.completeFrame(actual);assert(!visual.desync()); // presentation is not a physical predicate
 Playback physical(doc);actual["x"]=20;physical.completeFrame(actual);assert(physical.desync());
}
`);
 execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-isystem',json+'/include','-isystem',result,'-I',join(root,'src'),join(dir,'test.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(p=>json+'/src/'+p+'.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
