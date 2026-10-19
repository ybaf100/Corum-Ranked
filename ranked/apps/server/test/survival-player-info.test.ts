import 'reflect-metadata';
import {describe,it,expect,vi} from 'vitest';
import {SurvivalPlayerInfoController} from '../src/survival/survival-player-info.controller.js';
import type {SurvivalEngineService} from '../src/survival/survival-engine.service.js';
import type {SurvivalRateService} from '../src/survival/survival-rate.service.js';
import type {ForumRecordsService} from '../src/forum/forum-records.service.js';
import type {RankedSessionContext} from '../src/session/session.types.js';
import {createSurvivalMatch,forceSurvivalDebugRank,DEFAULT_SURVIVAL_CONFIG} from '@corum-ranked/rules';
import {survivalResultRoster} from '../src/survival/survival-result-roster.js';
function fixture(){return createSurvivalMatch('match',Array.from({length:16},(_,i)=>({participantId:`p${i}`,playerId:i?null:'owner',name:`P${i}`,kind:i?'BOT' as const:'HUMAN' as const,gdAccountId:i?'999':'123',lp:150})),DEFAULT_SURVIVAL_CONFIG,'v1',1000);}
describe('Survival custom player information and settled podium data',()=>{
 function rig(){const match=fixture(),read=vi.fn(async(account:string)=>({gdAccountId:account,records:[{provider:'GDKF',rank:9}]})),check=vi.fn();
  const controller=new SurvivalPlayerInfoController({get:async()=>match} as unknown as SurvivalEngineService,{check} as unknown as SurvivalRateService,{read} as unknown as ForumRecordsService);
  const session={playerId:'owner'} as RankedSessionContext;return {controller,read,check,session};}
 it('reads the existing forum cache for the match-authorized exact GD account',async()=>{
  const r=rig();expect(await r.controller.read('match','p0',r.session)).toMatchObject({participantId:'p0',gdAccountId:'123',records:[{provider:'GDKF',rank:9}]});
  expect(r.read).toHaveBeenCalledExactlyOnceWith('123');expect(r.check).toHaveBeenCalledExactlyOnceWith('owner','state');
 });
 it('never looks up a bot reference even when a malformed bot carries an account ID',async()=>{
  const r=rig();expect(await r.controller.read('match','p1',r.session)).toMatchObject({bot:true,gdAccountId:null,records:[]});expect(r.read).not.toHaveBeenCalled();
 });
 it('rejects foreign requesters and targets outside this match without any forum query',async()=>{
  const r=rig();await expect(r.controller.read('match','p0',{playerId:'foreign'} as RankedSessionContext)).rejects.toMatchObject({status:403});
  await expect(r.controller.read('match','foreign',r.session)).rejects.toMatchObject({status:404});expect(r.read).not.toHaveBeenCalled();
 });
 it('reports accepted human post-LP, explicit pending values, and virtual bot post-LP without inventing settlement',()=>{
  const match=forceSurvivalDebugRank(fixture(),'p0',1,2000);
  const pending=survivalResultRoster(match,new Map()).find(p=>p.participantId==='p0')!;
  expect(pending).toMatchObject({tierAfter:null,lpAfter:null,settled:false});
  const paid=survivalResultRoster(match,new Map([['owner',{profileAfter:{tier:'GOLD',lp:505},actualDelta:50}]]));
  expect(paid.find(p=>p.participantId==='p0')).toMatchObject({tierAfter:'GOLD',lpAfter:505,delta:50,settled:true,virtualRating:false});
  for(const bot of paid.filter(p=>p.kind==='BOT')){expect(bot.lpAfter).toBe(Math.max(0,150+bot.delta!));expect(bot.virtualRating).toBe(true);}
 });
});
