import 'reflect-metadata';
import {readFile,readdir} from 'node:fs/promises';
import {randomUUID} from 'node:crypto';
import {PGlite} from '@electric-sql/pglite';
import {Test} from '@nestjs/testing';
import {type INestApplication,ValidationPipe} from '@nestjs/common';
import {afterAll,beforeAll,beforeEach,describe,expect,it} from 'vitest';
import {PROGRESSIVE_ACHIEVEMENTS,ONE_SHOT_ACHIEVEMENTS} from '@corum-ranked/rules';
import {AdminController} from '../src/admin/admin.module.js';
import {AdminService} from '../src/admin/admin.service.js';
import {AdminActionDto} from '../src/admin/admin.dto.js';
import {AchievementService} from '../src/achievement/achievement.service.js';
import {MatchService} from '../src/match/match.service.js';
import {TokenService} from '../src/common/token.service.js';
import {DATABASE} from '../src/database/database.port.js';
import {SERVER_CLOCK} from '../src/common/runtime.module.js';
import {SessionGuard} from '../src/session/session.guard.js';
import {REQUIRED_RANKED_CLIENT_VERSION} from '../src/session/client-version.js';
import type {RankedSessionContext} from '../src/session/session.types.js';
import {PgliteDatabase} from './pglite-database.js';
import {environmentFixture,configDocumentFixture} from './fixtures.js';
import {RankedConfigService} from '../src/config/ranked-config.service.js';
import {RankedConfigController} from '../src/config/ranked-config.controller.js';
import {SessionService} from '../src/session/session.service.js';
const id=(n:number)=>`00000000-0000-4000-8000-${String(n).padStart(12,'0')}`;
const now=new Date('2026-10-09T15:00:00Z'),clock={now:()=>now};
const env=environmentFixture(),debugEnv={...env,debugBot:{password:'test-debug-password'}} as typeof env;
let document=configDocumentFixture();
const tokens=new TokenService(env),config={getSnapshot:()=>structuredClone(document)} as RankedConfigService;
let pg:PGlite,db:PgliteDatabase,service:AdminService,app:INestApplication,base:string;
const context:RankedSessionContext={sessionId:id(101),playerId:id(1),gdAccountId:'1',gdUsername:'hwanhee1',displayedTier:'RED',hiddenMmr:500,placementGames:5};
const body=(action:AdminActionDto['action'],values:Partial<AdminActionDto>={})=>({password:'test-debug-password',action,...values});
async function player(n:number,name:string,bot=false){
 await db.query('INSERT INTO ranked_players (id,gd_account_id,gd_username,is_bot) VALUES ($1,$2,$3,$4)',[id(n),bot?-n:n,name,bot]);
 await db.query("INSERT INTO ranked_profiles (player_id,hidden_mmr,displayed_tier,visible_ranked_score,placement_games,initial_csmp_tier,initial_seed_mmr,seed_applied_at) VALUES ($1,500,'RED',100,5,'RED',500,CURRENT_TIMESTAMP)",[id(n)]);
}
async function match(n:number,a=1,b=2,state='MATCH_RESULT',at=now){
 await db.query(`INSERT INTO ranked_matches (id,player_a_id,player_b_id,config_snapshot_id,mmr_a_before,mmr_b_before,
 effective_rating_average,effective_tier,candidate_maps_snapshot,series_state,state,rules_version,finished_at)
 VALUES ($1,$2,$3,$4,500,500,500,'RED','[]','{}',$5,'test',$6)`,[id(n),id(a),id(b),id(90),state,state==='MATCH_RESULT'?at.toISOString():null]);
}
async function profile(n=1){return(await db.query<Record<string,unknown>>('SELECT * FROM ranked_profiles WHERE player_id=$1',[id(n)])).rows[0]!;}
function history(n:number){
 // Execute the actual history SQL and child projections against PostgreSQL.
 const reader=Object.assign(Object.create(MatchService.prototype),{database:db,clock}) as MatchService;
 return reader.history({...context,playerId:id(n)});
}
beforeAll(async()=>{
 pg=new PGlite();db=new PgliteDatabase(pg);const dir=new URL('../../../migrations/',import.meta.url);
 for(const f of(await readdir(dir)).filter(f=>f.endsWith('.sql')).sort())await pg.exec(await readFile(new URL(f,dir),'utf8'));
 service=new AdminService(db,debugEnv,config,clock);
 const module=await Test.createTestingModule({controllers:[AdminController,RankedConfigController],providers:[SessionGuard,
 {provide:RankedConfigService,useValue:config},{provide:AdminService,useValue:service},{provide:DATABASE,useValue:db},{provide:SERVER_CLOCK,useValue:clock},{provide:TokenService,useValue:tokens}]}).compile();
 // Preserve the production tsc DTO metadata omitted by the test transpiler.
 Reflect.defineMetadata('design:paramtypes',[Object,AdminActionDto],AdminController.prototype,'execute');
 app=module.createNestApplication();app.useLogger(false);app.useGlobalPipes(new ValidationPipe({whitelist:true,forbidNonWhitelisted:true,transform:true}));
 await app.listen(0,'127.0.0.1');base=await app.getUrl();
},60000);
afterAll(async()=>{await app?.close();await pg.close();});
beforeEach(async()=>{
 document={...configDocumentFixture(),client:{audio:{enabled:true,fadeInSeconds:.25,fadeOutSeconds:.25,resources:[
   {key:'menu',label:'Lobby',songId:123,startSeconds:42,loop:true},
   {key:'result_win',label:'Win',songId:123,startSeconds:3,loop:false},
   {key:'result_lose',label:'Lose',songId:456,startSeconds:5,loop:false},
 ]},ui:{fadeInSeconds:.24,fadeOutSeconds:.18}}};
 await pg.exec('TRUNCATE ranked_audio_volumes');
 await pg.exec('TRUNCATE ranked_config_snapshots,ranked_players,ranked_outbox_events CASCADE');await player(1,'hwanhee1');await player(2,'Target');
 await db.query("INSERT INTO ranked_config_snapshots (id,generation,rules_version,source_payload,fetched_at) VALUES ($1,'admin','test','{}',$2)",[id(90),now]);
 for(const n of[1,2])await db.query(`INSERT INTO ranked_sessions (id,player_id,token_hash,client_version,environment_snapshot,expires_at,last_heartbeat_at)
 VALUES ($1,$2,$3,$4,'{}',$5,$6)`,[id(100+n),id(n),tokens.hash(`session-${n}`),REQUIRED_RANKED_CLIENT_VERSION,new Date(now.getTime()+3600000),now]);
});
async function post(data:unknown,token='session-1'){
 const r=await fetch(base+'/api/ranked/debug/admin',{method:'POST',headers:{'Content-Type':'application/json',Authorization:`Bearer ${token}`},body:JSON.stringify(data)});
 return{status:r.status,json:await r.json() as any};
}
describe('Admin authenticated mutations',()=>{
 it('deduplicates shared songs and persists common gain in public server configuration without modifying the sheet snapshot',async()=>{
 const loaded=await post(body('GET_AUDIO_VOLUMES'));
 expect(loaded.status).toBe(201);expect(loaded.json.songs).toHaveLength(2);
 expect(loaded.json.songs[0]).toMatchObject({songId:123,volumePercent:100,keys:['menu','result_win']});
 const saved=await post(body('SET_AUDIO_VOLUME',{songId:123,volumePercent:37}));
 expect(saved.status).toBe(201);expect(saved.json.audioVolumes).toContainEqual({songId:123,volumePercent:37});
 const response=await fetch(base+'/api/ranked/config');expect(response.status).toBe(200);expect(response.headers.get('cache-control')).toBe('no-store');
 const publicConfig=await response.json() as any;
 expect(publicConfig.client.audio.resources.map((r:any)=>r.volumePercent)).toEqual([37,37,100]);
 expect(publicConfig.client.audio.resources[0]).toMatchObject({songId:123,startSeconds:42,loop:true});
 expect(document.client!.audio.resources[0]!.volumePercent).toBeUndefined();
 expect(await profile()).toMatchObject({visible_ranked_score:100,hidden_mmr:500});
 // A fresh reader/server instance and sheet refresh retain the same DB setting.
 document={...document,generation:'later',client:{...document.client!,audio:{...document.client!.audio,fadeInSeconds:.1}}};
 const reader=new RankedConfigController(config,db);
 expect(await reader.getClientConfig()).toMatchObject({client:{audio:{fadeInSeconds:.1,resources:[
   {key:'menu',volumePercent:37},{key:'result_win',volumePercent:37},{key:'result_lose',volumePercent:100},
 ]}}});
 expect((await db.query('SELECT song_id,volume_percent,updated_by_account_id FROM ranked_audio_volumes')).rows).toEqual([
   {song_id:123,volume_percent:37,updated_by_account_id:1},
 ]);
 });
 it('delivers updates and zero/default gains to both connected players through their existing session heartbeats',async()=>{
 const sessions=new SessionService(db,{fetchCurrentTier:async()=>"BRONZE"},clock,{next:()=>randomUUID()},config,tokens);
 await service.execute(context,body('SET_AUDIO_VOLUME',{songId:123,volumePercent:0}));
 const other={...context,sessionId:id(102),playerId:id(2),gdAccountId:'2',gdUsername:'Target'};
 for(const session of[context,other])expect(await sessions.heartbeat(session)).toMatchObject({sessionLeaseSeconds:90,
   audioVolumes:[{songId:123,volumePercent:0},{songId:456,volumePercent:100}]});
 await service.execute(context,body('SET_AUDIO_VOLUME',{songId:123,volumePercent:100}));
 expect((await sessions.heartbeat(other)).audioVolumes[0]).toEqual({songId:123,volumePercent:100});
 });
 it('authorizes music reads/writes server-side and rejects unknown IDs and malformed gains without any write',async()=>{
 for(const action of['GET_AUDIO_VOLUMES','SET_AUDIO_VOLUME'] as const){
   expect((await post(body(action,{songId:123,volumePercent:25}),'session-2')).status).toBe(403);
   expect((await post(body(action,{password:'wrong',songId:123,volumePercent:25}))).status).toBe(403);
 }
 for(const volumePercent of[-1,101,12.5,'50',undefined]){
   expect((await post({...body('SET_AUDIO_VOLUME',{songId:123}),...(volumePercent===undefined?{}:{volumePercent})})).status).toBe(400);
 }
 for(const songId of[0,789,1.5,2147483648,undefined])expect((await post(body('SET_AUDIO_VOLUME',{...(songId===undefined?{}:{songId}),volumePercent:25}))).status).toBe(400);
 expect((await db.query('SELECT * FROM ranked_audio_volumes')).rows).toHaveLength(0);
 await db.query('UPDATE ranked_sessions SET revoked_at=$1 WHERE id=$2',[now,id(101)]);
 await expect(service.execute(context,body('SET_AUDIO_VOLUME',{songId:123,volumePercent:25}))).rejects.toMatchObject({status:403});
 expect((await db.query('SELECT * FROM ranked_audio_volumes')).rows).toHaveLength(0);
 });
 it('requires the persisted caller name AND password; a target/payload cannot impersonate the caller',async()=>{
 expect((await post(body('SET_LP',{lp:300}),'session-2')).status).toBe(403);
 expect((await post(body('SET_LP',{lp:300,password:'incorrect'}))).status).toBe(403);
 expect((await post(body('SET_LP',{lp:300,username:'hwanhee1'}),'missing')).status).toBe(401);
 expect((await post({...body('SET_LP',{lp:300}),gdUsername:'hwanhee1'})).status).toBe(400);
 expect((await profile()).visible_ranked_score).toBe(100);
 });
 it('rechecks revocation inside the transaction, even after an earlier HTTP guard pass',async()=>{
 await db.query('UPDATE ranked_sessions SET revoked_at=$1 WHERE id=$2',[now,id(101)]);
 await expect(service.execute(context,body('DELETE_BOTS'))).rejects.toMatchObject({status:403});
 expect((await post(body('DELETE_BOTS'))).status).toBe(401);
 });
 it('requires server debug opt-in',async()=>{
 await expect(new AdminService(db,env,config,clock).execute(context,body('DELETE_BOTS'))).rejects.toMatchObject({status:404});
 });
 it('sets LP at each configured cutoff, clearing promotion/warning and completing placement without changing MMR',async()=>{
 for(const band of config.getSnapshot().operational.tierBands){
 expect((await post(body('SET_LP',{username:'Target',lp:band.minInclusive}))).status).toBe(201);
 expect(await profile(2)).toMatchObject({displayed_tier:band.tier,visible_ranked_score:band.minInclusive,hidden_mmr:500,placement_games:5,demotion_warning:false,promotion_target_tier:null});
 }expect((await profile()).visible_ranked_score).toBe(100);
 });
 it('supports MMR-only/combined changes and rejects absent, negative, fractional or overflowing values atomically',async()=>{
 expect((await post(body('SET_MMR',{mmr:-77}))).status).toBe(201);
 expect(await profile()).toMatchObject({displayed_tier:'RED',visible_ranked_score:100,hidden_mmr:-77});
 expect((await post(body('SET_BOTH',{lp:4000,mmr:3000}))).status).toBe(201);
 expect(await profile()).toMatchObject({displayed_tier:'GOLD',visible_ranked_score:4000,hidden_mmr:3000});
 for(const lp of[-1,1.1,2147483648,undefined])expect((await post(body('SET_BOTH',{...(lp===undefined?{}:{lp}),mmr:5}))).status).toBe(400);
 expect((await profile()).hidden_mmr).toBe(3000);
 });
 it('unlocks each reward once, exposes selectable titles, returns one batch and leaves metrics/AP claims intact',async()=>{
 const total=PROGRESSIVE_ACHIEVEMENTS.reduce((s,d)=>s+d.levels.length,0)+ONE_SHOT_ACHIEVEMENTS.length;
 expect(await service.execute(context,body('UNLOCK_ALL'))).toMatchObject({count:total,batchUnlock:true});
 expect(await service.execute(context,body('UNLOCK_ALL'))).toMatchObject({count:0,batchUnlock:true});
 const achievements=new AchievementService(db,clock,{next:()=>randomUUID()});const state=await achievements.state(id(1));
 expect(state.totalAp).toBe(0);expect(state.unclaimedRewardCount).toBe(total);
 expect(state.achievements.find(a=>a.id==='ranked_wins')).toMatchObject({progress:{level:5,maxed:true,metricValue:0},claimableRewards:5});
 await expect(achievements.equipTitle(id(1),'ranked_wins',5)).resolves.toBeDefined();
 expect((await achievements.claim(id(1),'ranked_wins')).claimedRewards).toBe(5);
 expect((await db.query('SELECT * FROM ranked_outbox_events')).rows).toHaveLength(0);
 });
 it('resets the whole target profile without deleting the opponent history or restoring erased achievements',async()=>{
 for(let n=200;n<235;++n)await match(n,1,2,'MATCH_RESULT',new Date(now.getTime()-(235-n)*1000));
 expect((await history(1)).matches).toHaveLength(30);expect((await history(1)).matches[0]?.matchId).toBe(id(234));
 await service.execute(context,body('UNLOCK_ALL',{username:'Target'}));
 await service.execute(context,body('UNLOCK_ALL'));
 const achievements=new AchievementService(db,clock,{next:()=>randomUUID()});
 await achievements.equipTitle(id(2),'ranked_wins',5);await achievements.claim(id(2),'ranked_wins');
 await db.query(`UPDATE ranked_profiles SET displayed_tier='GOLD',visible_ranked_score=2000,hidden_mmr=2400,
   wins=8,losses=2,match_draws=1,result_streak=8,demotion_warning=TRUE,placement_results='["WIN"]' WHERE player_id=$1`,[id(2)]);
 await db.query("UPDATE ranked_matches SET result_applied_at=$1,match_type='RANKED_PVP',winner_id=$2",[now,id(2)]);
 await db.query(`INSERT INTO ranked_placement_runs(id,player_id,config_snapshot_id,config_generation,state,starting_pool,last_heartbeat_at,
   finalized_at,placement_rating,final_hidden_mmr,final_visible_ranked_score,final_displayed_tier,completion_kind)
   VALUES ($1,$2,$3,'test','PLACEMENT_RESULT',6,$4,$4,5000,5000,1500,'GOLD','COMPLETED')`,[id(401),id(2),id(90),now]);
 await db.query(`INSERT INTO ranked_placement_maps(id,run_id,stage_index,map_index,level_id,canonical_level_id,playable_level_id,title,creator,difficulty,pool,qualifying_percent,state)
   VALUES($1,$2,1,1,'123','123','123','Map','Creator','Extreme Demon',6,50,'MAP_RESULT')`,[id(402),id(401)]);
 await db.query(`INSERT INTO ranked_placement_attempts(id,run_id,map_id,player_id,attempt_sequence,domain_attempt_id,client_start_event_id,played_level_id,server_accepted_start_at,ended_at)
   VALUES($1,$2,$3,$4,1,'attempt','start','123',$5,$5)`,[id(403),id(401),id(402),id(2),now]);
 await db.query("INSERT INTO ranked_match_tokens(match_id,player_id,token_hash,expires_at) VALUES($1,$2,'target-match-token',$3)",[id(234),id(2),new Date(now.getTime()+3600000)]);
 expect(await service.execute(context,body('RESET_HISTORY',{username:'Target'}))).toMatchObject({count:35,resetPlayer:true,own:false});
 expect((await history(2)).matches).toHaveLength(0);expect((await history(1)).matches).toHaveLength(30);
 expect(await profile(2)).toMatchObject({displayed_tier:'UNRANKED',visible_ranked_score:null,hidden_mmr:null,placement_games:0,
   wins:0,losses:0,match_draws:0,result_streak:0,demotion_warning:false,total_ap:0,equipped_achievement_id:null,
   promotion_target_tier:null,initial_seed_mmr:null,seed_applied_at:null,placement_results:[],promotion_results:[]});
 for(const table of['ranked_placement_runs','ranked_placement_attempts','ranked_achievement_unlocks','ranked_achievement_events','ranked_ap_ledger','ranked_match_tokens']) {
   expect((await db.query(`SELECT * FROM ${table} WHERE player_id=$1`,[id(2)])).rows).toHaveLength(0);
 }
 expect((await db.query('SELECT * FROM ranked_placement_maps WHERE run_id=$1',[id(401)])).rows).toHaveLength(0);
 const resetState=await achievements.state(id(2));
 expect(resetState).toMatchObject({totalAp:0,unclaimedRewardCount:0,equippedTitle:null});
 expect(resetState.achievements.every(a=>a.claimableRewards===0)).toBe(true);
 expect((await achievements.state(id(1))).unclaimedRewardCount).toBeGreaterThan(0);
 expect((await profile(1)).visible_ranked_score).toBe(100);
 expect((await post(body('SET_LP',{lp:300}),'session-2')).status).toBe(401);
 await match(236,1,2,'MATCH_RESULT',new Date(now.getTime()+1000));expect((await history(2)).matches).toHaveLength(1);
 });
 it('blocks a whole-player reset while an active match or Placement could still write results',async()=>{
 await match(300,1,2,'ROUND_PLAYING');
 await expect(service.execute(context,body('RESET_HISTORY',{username:'Target'}))).rejects.toMatchObject({status:409});
 await db.query("UPDATE ranked_matches SET state='MATCH_RESULT' WHERE id=$1",[id(300)]);
 await db.query(`INSERT INTO ranked_placement_runs(id,player_id,config_snapshot_id,config_generation,state,starting_pool,last_heartbeat_at)
   VALUES($1,$2,$3,'test','MAP_PREPARE',1,$4)`,[id(401),id(2),id(90),now]);
 await expect(service.execute(context,body('RESET_HISTORY',{username:'Target'}))).rejects.toMatchObject({status:409});
 expect((await profile(2)).visible_ranked_score).toBe(100);
 expect((await db.query('SELECT * FROM ranked_sessions WHERE player_id=$1 AND revoked_at IS NULL',[id(2)])).rows).toHaveLength(1);
 });
 it('blocks a rating edit while a match is active',async()=>{
 await match(300,1,2,'ROUND_PLAYING');await expect(service.execute(context,body('SET_LP',{lp:999}))).rejects.toMatchObject({status:409});
 expect((await profile()).visible_ranked_score).toBe(100);
 });
 it('deletes all bot profiles and their FK-linked matches/assignments, preserving unrelated human history',async()=>{
 await player(3,'BOT NORMAL',true);await player(4,'BOT NORMAL',true);await match(301,1,3);await match(302,2,4);await match(303,1,2);
 await db.query(`INSERT INTO ranked_queue_entries (player_id,session_id,hidden_mmr_snapshot,joined_at,last_heartbeat_at,status,matched_match_id)
 VALUES ($1,$2,500,$3,$3,'MATCHED',$4)`,[id(1),id(101),now,id(301)]);
 expect(await service.execute(context,body('DELETE_BOTS'))).toMatchObject({count:2});
 expect((await db.query('SELECT * FROM ranked_players')).rows).toHaveLength(2);
 expect((await db.query('SELECT * FROM ranked_matches')).rows).toHaveLength(1);
 expect((await db.query('SELECT * FROM ranked_queue_entries')).rows).toHaveLength(0);
 expect((await profile()).visible_ranked_score).toBe(100);
 });
 it('requires reset confirmation, retains configuration/runtime flags and invalidates the former session',async()=>{
 await service.execute(context,body('SET_AUDIO_VOLUME',{songId:456,volumePercent:65}));
 await match(301);expect((await post(body('RESET_DB'))).status).toBe(400);
 expect((await db.query('SELECT * FROM ranked_players')).rows).toHaveLength(2);
 const flags=(await db.query('SELECT * FROM ranked_runtime_features')).rows;
 expect((await post(body('RESET_DB',{confirmation:'RESET'}))).status).toBe(201);
 expect((await db.query('SELECT * FROM ranked_players')).rows).toHaveLength(0);
 expect((await db.query('SELECT * FROM ranked_matches')).rows).toHaveLength(0);
 expect((await db.query('SELECT * FROM ranked_config_snapshots')).rows).toHaveLength(1);
 expect((await db.query('SELECT * FROM ranked_runtime_features')).rows).toEqual(flags);
 expect((await db.query('SELECT song_id,volume_percent FROM ranked_audio_volumes')).rows).toEqual([{song_id:456,volume_percent:65}]);
 expect((await post(body('DELETE_BOTS'))).status).toBe(401);
 });
});
