import { describe,expect,it } from 'vitest';
import { survivalHistoryProjection } from '../src/history/survival-history.js';
import { recordedLevelId,soloHistoryDetails } from '../src/history/solo-history.js';
import type { DatabasePort } from '../src/database/database.port.js';
const player=(id:string,progress:number,rank:number,qualified:boolean)=>({participantId:id,highestProgress:progress,rank,qualified,forfeited:false,attempts:[{highestProgress:progress,endedAt:100},{highestProgress:99,endedAt:null}]});
const fixture=()=>({id:'match',state:'PLAYING',online:true,createdAt:100,config:{password:'private'},players:[{participantId:'me',name:'Owner',kind:'HUMAN',finalRank:8},{participantId:'other',name:'Peer',kind:'HUMAN',finalRank:null}],
 rounds:[{key:'stage-1',ordinal:1,phase:'ROUND_1',target:1,finishedAt:200,cutoffScore:82,achievementCheckpoint:{private:true},map:{levelId:123,name:'Exact Map',stars:7,demon:false,length:3},players:[player('me',82.9,1,true),player('other',82.1,1,true)]},
 {key:'stage-2',ordinal:2,phase:'ROUND_2',target:1,finishedAt:null,cutoffScore:40,map:{levelId:456},players:[player('other',99,1,true)]}]});
describe('confirmed history projection',()=>{
 it('distinguishes an eliminated round from later non-participation in summary and detail, including forfeits',()=>{
  const match=fixture();match.rounds[0]!.players[0]!.qualified=false;
  match.rounds[1]!.finishedAt=300;
  for(const summary of [false,true]){
   const history=survivalHistoryProjection(match,null,'me',summary);
   expect(history.rounds[0]).toMatchObject({completed:true,ownQualified:false});
   expect(history.rounds[1]).toMatchObject({completed:true,ownQualified:null});
   match.rounds[0]!.players[0]!.qualified=true;match.rounds[0]!.players[0]!.forfeited=true;
   expect(survivalHistoryProjection(match,null,'me',summary).rounds[0]!.ownQualified).toBe(false);
   match.rounds[0]!.players[0]!.forfeited=false;
   expect(survivalHistoryProjection(match,null,'me',summary).rounds[0]!.ownQualified).toBe(true);
   match.rounds[0]!.finishedAt=null;
   expect(survivalHistoryProjection(match,null,'me',summary).rounds[0]!.ownQualified).toBeNull();
   match.rounds[0]!.finishedAt=200;match.rounds[0]!.players[0]!.qualified=false;
  }
 });
 it('preserves every demon subtype in history without a live GD metadata cache',()=>{
  for(const subtype of ['EASY','MEDIUM','HARD','INSANE','EXTREME']){
   const match=fixture();Object.assign(match.rounds[0]!.map,{stars:10,demon:true,demonDifficulty:subtype});
   expect(survivalHistoryProjection(match,null,'me').rounds[0]!.map).toMatchObject({difficulty:subtype+' DEMON',levelId:123});
  }
 });
 it('uses recorded exact identities for player-info and never substitutes a real account for bots',()=>{
  const match=fixture();Object.assign(match.players[0]!,{gdAccountId:'123',displayedTier:'SILVER'});Object.assign(match.players[1]!,{kind:'BOT',gdAccountId:'999'});
  const h=survivalHistoryProjection(match,null,'me');
  expect(h.rounds[0]!.standings.find(p=>p.participantId==='me')).toMatchObject({gdAccountId:'123',displayedTier:'SILVER',isBot:false});
  expect(h.rounds[0]!.standings.find(p=>p.participantId==='other')).toMatchObject({gdAccountId:null,isBot:true});
  expect(h.rounds[1]!.standings).toEqual([]);expect(survivalHistoryProjection(match,null,'me',true).rounds[0]!.standings).toEqual([]);
 });

 it('publishes ended rounds, ties, boundary and ended attempt counts while redacting all provisional ranks and early settlement',()=>{
  const history=survivalHistoryProjection(fixture(),{rank:8,actualDelta:-55},'me');
  expect(history).toMatchObject({inProgress:true,finalRank:null,result:null});
  expect(history.rounds[0]).toMatchObject({completed:true,qualifiedCount:2,target:1,ownQualified:true,map:{levelId:123,title:'Exact Map',length:'LONG'}});
  expect(history.rounds[0]!.standings).toHaveLength(2);expect(history.rounds[0]!.standings[0]).toMatchObject({rank:1,tiedPlayers:2,cutoffBoundary:true,attemptCount:1,own:true});
  expect(history.rounds[1]).toMatchObject({completed:false,cutoffScore:null,standings:[]});
  const json=JSON.stringify(history);expect(json).not.toContain('private');expect(json).not.toContain('achievementCheckpoint');expect(json).not.toContain('endedAt');
 });
 it('keeps summary counts without copying standings or attempts, and never invents future rounds',()=>{
  const history=survivalHistoryProjection(fixture(),null,'me',true);
  expect(history.rounds).toHaveLength(2);expect(history.rounds[0]).toMatchObject({qualifiedCount:2,standings:[],ownQualified:true});
  expect(history.detailsLoaded).toBe(false);expect(history.snapshot.players).toEqual([]);
 });
 it('keeps tiebreak millipercent precision and releases final result only after the whole match finishes',()=>{
  const match=fixture();match.state='FINAL_RESULT';match.rounds=[{...match.rounds[0]!,phase:'TIEBREAK',cutoffScore:82345,players:[player('me',82.345,1,true),player('other',82.344,2,false)]}];
  const h=survivalHistoryProjection(match,{rank:8,actualDelta:-55,profileAfter:{lp:900,tier:'SILVER'}},'me');
  expect(h).toMatchObject({completed:true,inProgress:false,finalRank:8,result:{rank:8,actualDelta:-55}});
  expect(h.rounds[0]!.cutoffScore).toBe(82.345);expect(h.rounds[0]!.standings[0]!.cutoffBoundary).toBe(true);
 });
 it('distinguishes a finished match awaiting payout without inventing LP and keeps legacy final rosters sanitized',()=>{
  const match=fixture();match.state='FINAL_RESULT';
  expect(survivalHistoryProjection(match,null,'me')).toMatchObject({completed:true,inProgress:false,settlementPending:true,result:{actualDelta:null}});
  const h=survivalHistoryProjection(match,{actualDelta:10,finalStandings:true,standings:[{participantId:'me',name:'Owner',rank:1,delta:10,password:'private'}]},'me');
  expect(h.settlementPending).toBe(false);expect(h.result?.standings[0]).toMatchObject({name:'Owner',rank:8,delta:10});expect(JSON.stringify(h)).not.toContain('password');
 });
 it('rejects absent, nonnumeric and unsafe level IDs without substituting another identifier',()=>{
  for(const id of [undefined,null,0,-1,'name',1.5,2147483648])expect(recordedLevelId(id)).toBeNull();
  expect(recordedLevelId('123456789')).toBe(123456789);
 });
 it('keeps cumulative Solo scores separate from valid ended Attempt progress, clear state and exact playable ID',async()=>{
  const db={query:async(sql:string)=>({rows:sql.includes('FROM ranked_rounds WHERE')?[{id:'round',match_id:'m',round_number:1,title:'Title',playable_level_id:'123',level_id:'999',score_a:273,score_b:801,settled_at:new Date(),clears_a:0,clears_b:2,result:'B'}]:
   sql.includes('FROM ranked_deathmatches WHERE')?[{id:'dm1',match_id:'m',sequence:1,map_snapshot:{playableLevelId:'456',title:'DM'},score_a:82.345,score_b:82.345,finished_at:new Date(),winner_id:null},{id:'dm2',match_id:'m',sequence:2,map_snapshot:{levelId:'999'},score_a:null,score_b:null,finished_at:null,winner_id:null}]:
   sql.includes('FROM ranked_attempts a')?[{round_id:'round',player_id:'a',best:96.7,cleared:false,attempts:3},{round_id:'round',player_id:'b',best:100,cleared:true,attempts:8}]:[{round_id:'dm1',player_id:'a',best:82.345,cleared:false,attempts:3}],rowCount:1}),ping:async()=>true} as unknown as DatabasePort;
  const h=(await soloHistoryDetails(db,[{id:'m',player_a_id:'a',player_b_id:'b'}])).get('m')!;
  expect(h.rounds[0]).toMatchObject({scoreA:273,bestProgressA:96.7,clearedA:false,bestProgressB:100,clearedB:true,levelId:123});
  expect(h.deathmatches[0]).toMatchObject({levelId:456,bestProgressA:82.345,winnerSide:null,nextDeathmatchSequence:2});
  expect(h.deathmatches[1]).toMatchObject({levelId:null,bestProgressA:null,completed:false});
 });
});
