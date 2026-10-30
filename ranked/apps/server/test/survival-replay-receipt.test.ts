import {describe,it,expect} from 'vitest';
import {DEFAULT_SURVIVAL_CONFIG,createSurvivalMatch,reduceSurvivalMatch,survivalCurrentRound,survivalMapRange,type SurvivalCommand} from '@corum-ranked/rules';
import {survivalReplayReceipt} from '../src/survival/survival-replay-receipt.js';
import {survivalHistoryProjection} from '../src/history/survival-history.js';
function rig(dm=false){
 let now=1000000;
 let match=createSurvivalMatch('receipt-match',Array.from({length:16},(_,i)=>({participantId:`p${i}`,playerId:`u${i}`,name:`Player ${i}`,kind:'HUMAN' as const,lp:150})),DEFAULT_SURVIVAL_CONFIG,'fixture',now,false,false,dm);
 const send=(command:SurvivalCommand)=>{match=reduceSurvivalMatch(match,command,now);};
 const current=()=>survivalCurrentRound(match),context=()=>({roundKey:current().key,mapGeneration:current().mapGeneration,levelId:40000000});
 const range=survivalMapRange(match.difficultyGroup,current().phase);
 const map={levelId:40000000,levelVersion:1,geometrySha256:'a'.repeat(64),geometryBytes:100,verifiedAt:new Date(now).toISOString(),automatic:false,platformer:false,stars:range.minimumStars??10,demon:range.minimumStars===null,demonDifficulty:range.minimumStars===null?range.demons[0]!:null};
 send({type:'MAP_SELECTED',roundKey:current().key,mapGeneration:current().mapGeneration,map});
 for(const player of current().players)send({type:'ASSETS_READY',...context(),participantId:player.participantId,certificate:map});
 now+=5000;for(const player of current().players)send({type:'ENTERED',...context(),participantId:player.participantId});
 now=current().startsAt!;send({type:'TICK'});
 return {get match(){return match;},current,send,context,timeout(){now=current().endsAt!;send({type:'TICK'});},attempt(id:string,p:number,end=true){send({type:'ATTEMPT_START',...context(),participantId:'p0',attemptId:id});send({type:end?'ATTEMPT_END':'PROGRESS',...context(),participantId:'p0',attemptId:id,progress:p});}};
}
describe('owner replay authority and History share an immutable official best identity',()=>{
 it('never turns a missing or another participant attempt into an accepted receipt',()=>{
  const r=rig();r.attempt('own',38);
  expect(survivalReplayReceipt(r.match,'p1','own')).toMatchObject({accepted:false});
  expect(survivalReplayReceipt(r.match,'p0','missing')).toMatchObject({accepted:false});
 });
 it.each([false,true])('keeps pending attempts distinct, freezes cutoff progress, and agrees with confirmed History (DM=%s)',dm=>{
  const r=rig(dm);r.attempt('cutoff',74.123,false);
  expect(survivalReplayReceipt(r.match,'p0','cutoff')).toMatchObject({accepted:true,ended:false,bestAttemptId:null});
  const context=r.context();r.timeout();const before=JSON.stringify(r.match);
  expect(survivalReplayReceipt(r.match,'p0','cutoff')).toMatchObject({ended:true,progress:74.123,bestAttemptId:'cutoff'});
  expect(survivalHistoryProjection(r.match,null,'p0').rounds[0]!.replayIdentity).toMatchObject({attemptId:'cutoff',officialBestProgress:74.123});
  const late=()=>r.send({type:'ATTEMPT_END',...context,participantId:'p0',attemptId:'cutoff',progress:90});
  if(dm)late();else expect(late).toThrow(); // a terminal match may idempotently ignore the retry
  expect(JSON.stringify(r.match)).toBe(before);
  expect(survivalReplayReceipt(JSON.parse(before),'p0','cutoff')).toEqual(survivalReplayReceipt(r.match,'p0','cutoff'));
 });
 it.each([false,true])('retains the first official equal-score attempt across receipt/history and serialization (DM=%s)',dm=>{
  const r=rig(dm);r.attempt('first',dm?74.1234:74.1);r.attempt('equal',dm?74.12349:74.9);r.timeout();
  const receipt=survivalReplayReceipt(r.match,'p0','equal');expect(receipt).toMatchObject({bestAttemptId:'first'});
  expect(survivalHistoryProjection(r.match,null,'p0').rounds[0]!.replayIdentity).toMatchObject({attemptId:'first'});
  expect(survivalReplayReceipt(JSON.parse(JSON.stringify(r.match)),'p0','equal')).toEqual(receipt);
 });
 it('prefers the actual Clear over 99.9995 rounding and preserves millipercent progress',()=>{
  const r=rig(true);r.attempt('rounded',99.9995);r.attempt('clear',100);r.timeout();
  expect(survivalReplayReceipt(r.match,'p0','rounded')).toMatchObject({bestAttemptId:'clear',cleared:false,score:100000});
  expect(survivalReplayReceipt(r.match,'p0','clear')).toMatchObject({bestAttemptId:'clear',cleared:true,progress:100});
  expect(survivalHistoryProjection(r.match,null,'p0').rounds[0]!.replayIdentity).toMatchObject({attemptId:'clear'});
 });
});
