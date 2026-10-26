import {describe,it,expect,vi} from 'vitest';
import {SpectatorLiveService,parseSpectatorKeyframe} from '../src/match/spectator-live.service.js';
import {parseSpectatorCosmetics} from '../src/match/spectator-cosmetics.js';
import {InMemoryMatchRuntimeState} from '../src/match/match-runtime-state.js';
import type {MatchService} from '../src/match/match.service.js';
import type {RankedSessionContext} from '../src/session/session.types.js';
const avatar=()=>({x:100,y:40,rotation:30,mode:'CUBE',mini:false,upsideDown:false,flipped:false,visible:true,dual:false,dead:false,finished:false,effects:0});
const cosmetics=(revision=1)=>({revision,players:[0,1].map(i=>({icons:[1,2,3,4,5,6,7,8],primary:[10+i,20,30],secondary:[90,80,70],glowColor:[255,100,20],glow:true,trail:1}))});
const pose=()=>({...avatar(),dual:true,player2:{...avatar(),x:200,y:800,mode:'SPIDER',mini:true,upsideDown:true,flipped:true},progress:12.5});
const packet=(sequence=0)=>({protocolVersion:2,matchId:'m',roundNumber:1,deathmatchSequence:null,attemptId:'a',generation:1,sequence,elapsedMs:sequence*50,levelId:123,inputEdges:[],keyframe:pose()});
const session={playerId:'publisher'} as RankedSessionContext;
const rig=()=>{let viewer=false;const state=vi.fn(async()=>({matchId:'m',state:'ROUND_SETTLING',side:viewer?'A':'B',opponentPractice:false,
 players:{A:{gdUsername:'viewer'},B:{gdUsername:'publisher'}},currentRound:{roundNumber:1,map:{playableLevelId:123}},deathmatch:null,spectator:{active:viewer,opponentName:'publisher'}}));
 const runtime=new InMemoryMatchRuntimeState();runtime.beginAttempt('m',1,'B','a',Date.now());
 return {service:new SpectatorLiveService({state} as unknown as MatchService,runtime),runtime,viewer:(v:boolean)=>{viewer=v;}};};
describe('Stage 3 production Dual and cosmetic relay',()=>{
 it('preserves all 64 mode pairs and rejects every incomplete P2',()=>{
  for(const a of ['CUBE','SHIP','BALL','UFO','WAVE','ROBOT','SPIDER','SWING'])for(const b of ['CUBE','SHIP','BALL','UFO','WAVE','ROBOT','SPIDER','SWING']){
   const p=pose();p.mode=a;p.player2.mode=b;expect(parseSpectatorKeyframe(p)).toMatchObject({mode:a,player2:{mode:b,y:800,mini:true,upsideDown:true,flipped:true}});}
  for(const field of ['x','y','rotation','mode','mini','upsideDown','flipped','visible','dead','finished']){
   const p=pose() as unknown as {player2:Record<string,unknown>};delete p.player2[field];expect(()=>parseSpectatorKeyframe(p)).toThrow();}
  expect(()=>parseSpectatorKeyframe({...pose(),player2:undefined})).toThrow('INCOMPLETE_DUAL_POSE');
  expect(()=>parseSpectatorKeyframe({...pose(),dual:false})).toThrow('UNEXPECTED_DUAL_POSE');
 });
 it('rejects NaN, infinite/unsafe coordinates, recursive bodies, invalid modes and visual flags',()=>{
  for(const patch of [{x:NaN},{y:Infinity},{rotation:100001},{x:10000001},{mode:'INVALID'},{mini:1},{dual:true},{player2:{}},{dead:true,finished:true}])
   expect(()=>parseSpectatorKeyframe({...pose(),player2:{...pose().player2,...patch}})).toThrow();
  for(const effects of [-1,32768,Infinity,1.5,'2'])expect(()=>parseSpectatorKeyframe({...pose(),effects})).toThrow();
  for(const progress of [NaN,-1,100.1,'10'])expect(()=>parseSpectatorKeyframe({...pose(),progress})).toThrow();
  expect(parseSpectatorKeyframe(avatar()).dual).toBe(false);
 });
 it('validates descriptor resource IDs and RGB bytes without coercion',()=>{
  expect(parseSpectatorCosmetics(cosmetics()).players[1]!.icons).toHaveLength(8);
  for(const revision of [0,Infinity,1000001])expect(()=>parseSpectatorCosmetics({...cosmetics(),revision})).toThrow();
  for(const id of [0,-1,10001,NaN,1.5,'1']){const c=cosmetics() as unknown as {players:{icons:unknown[]}[]};c.players[0]!.icons[0]=id;expect(()=>parseSpectatorCosmetics(c)).toThrow();}
  for(const field of ['primary','secondary','glowColor'])for(const bytes of [[256,0,0],[0,-1,0],[0,0,NaN],[1,2],['1',2,3]]){
   const c=cosmetics() as unknown as {players:Record<string,unknown>[]};c.players[0]![field]=bytes;expect(()=>parseSpectatorCosmetics(c)).toThrow();}
 });
 it('caches descriptor revisions, delivers late changes and leaves Attempt authority unchanged',async()=>{
  const r=rig(),before=JSON.stringify(r.runtime.progress('m',1,'B'));
  await r.service.publish('m','token',session,{...packet(),cosmetics:cosmetics()});await r.service.publish('m','token',session,packet(1));r.viewer(true);
  const initial=await r.service.subscribe('m','token',session,'2',undefined,undefined,undefined,'1','0');
  expect(initial.stream!.cosmetics?.revision).toBe(1);expect(initial.packets[0]!.cosmetics?.revision).toBe(1);expect(initial.packets[1]!.cosmetics).toBeUndefined();
  expect(initial.packets[1]!.keyframe.player2?.y).toBe(800);
  expect((await r.service.subscribe('m','token',session,'2','a','1','1','1','1')).stream!.cosmetics).toBeUndefined();
  r.viewer(false);await r.service.publish('m','token',session,{...packet(2),cosmetics:cosmetics(2)});r.viewer(true);
  expect((await r.service.subscribe('m','token',session,'2','a','1','1','1','1')).stream!.cosmetics?.revision).toBe(2);
  expect(JSON.stringify(r.runtime.progress('m',1,'B'))).toBe(before);
 });
 it('keeps retries immutable and rejects a stale descriptor atomically',async()=>{
  const r=rig(),batch={packets:[{...packet(),cosmetics:cosmetics(2)},packet(1)]};
  await r.service.publish('m','token',session,batch);await r.service.publish('m','token',session,batch);
  await expect(r.service.publish('m','token',session,{packets:[packet(2),{...packet(3),cosmetics:cosmetics(1)}]})).rejects.toThrow('STALE_COSMETIC_DESCRIPTOR');
  r.viewer(true);const read=await r.service.subscribe('m','token',session,'2');expect(read.stream!.lastSequence).toBe(1);expect(read.stream!.cosmetics?.revision).toBe(2);
 });
 it('clears prior-generation cosmetics and rejects foreign Attempt data',async()=>{
  const r=rig();await r.service.publish('m','token',session,{...packet(),cosmetics:cosmetics()});await r.service.publish('m','token',session,{...packet(),generation:2});r.viewer(true);
  const reset=await r.service.subscribe('m','token',session,'2','a','1','0','1','1');expect(reset.reset).toBe(true);expect(reset.stream!.cosmetics).toBeUndefined();
  r.viewer(false);await expect(r.service.publish('m','token',session,{...packet(1),generation:2,attemptId:'foreign'})).rejects.toThrow('SPECTATOR_ATTEMPT_MISMATCH');
 });
 it('keeps P2 snap and terminal data without reviving a cleared Dual stream',async()=>{
  const r=rig();await r.service.publish('m','token',session,packet());
  await r.service.publish('m','token',session,{...packet(1),keyframe:{...pose(),player2:{...pose().player2,x:900,
   discontinuity:{type:'TELEPORT',fromX:200,fromY:800,toX:900,toY:800}}}});
  await r.service.publish('m','token',session,{...packet(2),keyframe:{...pose(),finished:true,player2:{...pose().player2,finished:true}}});
  await expect(r.service.publish('m','token',session,packet(3))).rejects.toThrow('SPECTATOR_TERMINAL_STREAM');r.viewer(true);
  expect((await r.service.subscribe('m','token',session,'2')).packets.at(-1)!.keyframe.player2!.finished).toBe(true);
 });
 it('measures actual JSON packet overhead separately from device frame rate',()=>{
  const bytes=(v:unknown)=>Buffer.byteLength(JSON.stringify(v));const solo=bytes({...packet(),keyframe:avatar()}),dual=bytes(packet()),withDescriptor=bytes({...packet(),cosmetics:cosmetics()});
  console.info('Stage 3 representative wire bytes',{solo,dual,withDescriptor});expect(dual-solo).toBeGreaterThan(0);expect(dual-solo).toBeLessThan(300);expect(withDescriptor).toBeLessThan(2000);
 });
});
