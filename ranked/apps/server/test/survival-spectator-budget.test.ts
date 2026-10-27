import {describe,it,expect} from 'vitest';
import {createSurvivalMatch,DEFAULT_SURVIVAL_CONFIG,survivalConfigForPlayerCount,survivalCurrentRound,type SurvivalMatch} from '@corum-ranked/rules';
import {SurvivalSpectatorLiveService} from '../src/survival/survival-spectator-live.service.js';
import type {DatabasePort} from '../src/database/database.port.js';
import type {RankedSessionContext} from '../src/session/session.types.js';
function fixture(count:16|64){
 let now=6000,queries=0;const matches=new Map<string,SurvivalMatch>();
 for(let n=0;n<3;++n){const id='match'+n;const m=createSurvivalMatch(id,Array.from({length:count},(_,i)=>({participantId:'p'+i,playerId:'p'+i,name:'Human '+i,kind:'HUMAN' as const,lp:150})),survivalConfigForPlayerCount({...DEFAULT_SURVIVAL_CONFIG,enabled:true},count),'test',0,true);
 m.state='PLAYING';const r=survivalCurrentRound(m);r.startsAt=0;r.endsAt=180000;r.map={levelId:40000000+n,levelVersion:1,stars:4,demon:false,demonDifficulty:null,automatic:false,platformer:false,geometrySha256:'a'.repeat(64),geometryBytes:64,verifiedAt:new Date(0).toISOString()};
 for(let i=0;i<6;++i){m.players[i]!.eliminationStage=1;m.players[i]!.eliminatedAt=0;}
 for(let i=6;i<count;++i)r.players[i]!.attempts=[{id:'a'+i,sequence:1,startedAt:0,endedAt:null,highestProgress:0}];matches.set(id,m);}
 const db={query:async(sql:string,values:unknown[])=>{expect(sql.trimStart().startsWith('SELECT')).toBe(true);++queries;return {rows:[{snapshot:matches.get(String(values[0])),participant_id:values[1],enabled:true}],rowCount:1};}} as unknown as DatabasePort;
 const live=new SurvivalSpectatorLiveService(db,{now:()=>new Date(now)});
 const session=(id:string,suffix='')=>({sessionId:'s'+id+suffix,playerId:id}) as RankedSessionContext;
 const query=(id:string,target:string)=>{const r=survivalCurrentRound(matches.get(id)!);return {protocolVersion:'2',targetId:target,roundKey:r.key,mapGeneration:String(r.mapGeneration),levelId:String(r.map!.levelId),worldHistory:'1'};};
 const packet=(id:string,target:number,sequence=0)=>{const r=survivalCurrentRound(matches.get(id)!);return {protocolVersion:2,matchId:id,roundNumber:r.ordinal,levelId:r.map!.levelId,attemptId:'a'+target,generation:1,sequence,elapsedMs:sequence*50,inputEdges:[],keyframe:{x:100+sequence,y:40,rotation:0,mode:'CUBE',mini:false,upsideDown:false,flipped:false,dual:false,dead:false,finished:false}};};
 const publish=(id:string,target:number,packets=[packet(id,target)])=>{const r=survivalCurrentRound(matches.get(id)!);return live.publish(id,session('p'+target),{roundKey:r.key,mapGeneration:r.mapGeneration,levelId:r.map!.levelId,packets});};
 return {live,session,query,publish,packet,advance:(ms:number)=>{now+=ms;},queries:()=>queries};
}
describe('production Survival relay bounded demand and storage',()=>{
 for(const count of [16,64] as const)it(`${count} players share buffers; four per match/eight total, expiry and idle perform no DB polling`,async()=>{
  const f=fixture(count);for(let m=0;m<2;++m)for(let i=0;i<4;++i){await f.live.subscribe('match'+m,f.session('p'+i,String(m)),f.query('match'+m,'p'+(i+6)));await f.publish('match'+m,i+6);}
  expect(f.live.diagnostics().streams).toBe(8);
  await expect(f.live.subscribe('match0',f.session('p4'),f.query('match0','p10'))).rejects.toThrow('SURVIVAL_STREAM_LIMIT');
  await expect(f.live.subscribe('match2',f.session('p5'),f.query('match2','p6'))).rejects.toThrow('SURVIVAL_STREAM_LIMIT');
  const before=f.queries();for(let i=0;i<1000;++i){f.live.demand('match0','p6','round1',1);f.live.diagnostics();}expect(f.queries()).toBe(before);
  f.advance(8001);expect(f.live.diagnostics()).toMatchObject({streams:0,viewers:0,serializedBytes:0});expect(f.queries()).toBe(before);
 });
 it('keeps history bounded during long capture, refuses unsupported complete restoration and atomically rejects a bad batch',async()=>{
  const f=fixture(64),viewer=f.session('p0');let count=0;
  for(let i=0;i<152;++i){f.advance(1000);await f.live.subscribe('match0',viewer,f.query('match0','p6'));const packets=Array.from({length:16},()=>f.packet('match0',6,count++));await f.publish('match0',6,packets);}
  const response=await f.live.subscribe('match0',viewer,f.query('match0','p6'));
  expect(response.restoration).toMatchObject({available:false,reason:'HISTORY_WINDOW_EXPIRED'});
  expect(response.packets.length).toBeLessThanOrEqual(72);expect(f.live.diagnostics().serializedBytes).toBeLessThan(4*1024*1024);
  const before=f.live.diagnostics().serializedBytes;
  await expect(f.publish('match0',6,[f.packet('match0',6,count),{...f.packet('match0',6,count+1),levelId:1}])).rejects.toThrow('SURVIVAL_PACKET_CONTEXT');
  expect(f.live.diagnostics().serializedBytes).toBe(before);
  await f.publish('match0',6,[f.packet('match0',6,count)]);
 });
 it('bounds authorization rate bookkeeping independently of stream count and retires expired buckets',async()=>{
  const f=fixture(16);
  for(let i=0;i<2048;++i)await f.live.targets('match0',f.session('p0',String(i)));
  await expect(f.live.targets('match0',f.session('p0','overflow'))).rejects.toThrow('SURVIVAL_SPECTATOR_RATE_CAPACITY');
  f.advance(10001);await expect(f.live.targets('match0',f.session('p0','overflow'))).resolves.toHaveProperty('targets');
 });

});
