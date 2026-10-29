import {SurvivalController} from "../src/survival/survival.controller.js";
import {SurvivalBadgesService} from "../src/survival/survival-badges.service.js";
import {SurvivalReplayReceiptController} from "../src/survival/survival-replay-receipt.controller.js";
import {SurvivalSpectatorLiveService} from "../src/survival/survival-spectator-live.service.js";
import {SurvivalSpectatorLiveController} from "../src/survival/survival-spectator-live.controller.js";
import "reflect-metadata";
import { readFile,readdir } from "node:fs/promises";
import { Pool } from "pg";
import type { DatabasePort,SqlExecutor } from "../src/database/database.port.js";
import { PGlite } from "@electric-sql/pglite";
import { Test } from "@nestjs/testing";
import { ValidationPipe,type INestApplication } from "@nestjs/common";
import { beforeAll,beforeEach,afterEach,afterAll,describe,it,expect,vi } from "vitest";
import { DEFAULT_SURVIVAL_CONFIG,survivalInitialPhase,survivalCurrentRound,survivalMapRange,type SurvivalMatch } from "@corum-ranked/rules";
import { PgliteDatabase } from "./pglite-database.js";
import { configDocumentFixture,environmentFixture } from "./fixtures.js";
import { SurvivalConfigService,type SurvivalConfigDocument } from "../src/config/survival-config.service.js";
import { SurvivalEngineService } from "../src/survival/survival-engine.service.js";
import { SurvivalOnlineService } from "../src/survival/survival-online.service.js";
import { SurvivalQueueService } from "../src/survival/survival-queue.service.js";
import { SurvivalRateService } from "../src/survival/survival-rate.service.js";
import { SurvivalService } from "../src/survival/survival.service.js";
import { SurvivalQueueController } from "../src/survival/survival-queue.controller.js";
import { SurvivalOnlineController } from "../src/survival/survival-online.controller.js";
import { HistoryController } from "../src/history/history.controller.js";
import { HistoryService } from "../src/history/history.service.js";
import type { MatchService } from "../src/match/match.service.js";
import { AchievementController } from "../src/achievement/achievement.controller.js";
import { AchievementService } from "../src/achievement/achievement.service.js";
import { AdminService } from "../src/admin/admin.service.js";
import { SessionService } from "../src/session/session.service.js";
import { SessionGuard } from "../src/session/session.guard.js";
import { AdmissionVersionGuard } from "../src/session/admission-version.guard.js";
import { TokenService } from "../src/common/token.service.js";
import { REQUIRED_RANKED_CLIENT_VERSION } from "../src/session/client-version.js";
import { DATABASE } from "../src/database/database.port.js";
import { SERVER_CLOCK } from "../src/common/runtime.module.js";
import type { RankedConfigService } from "../src/config/ranked-config.service.js";
import type { RankedSessionContext } from "../src/session/session.types.js";
vi.setConfig({testTimeout:30000});
const uuid=(n:number)=>`00000000-0000-4000-8000-${String(n).padStart(12,"0")}`;
const testSchema="survival_m5_"+process.pid+"_"+Date.now();
let pg:PGlite,pool:Pool|null=null,db:DatabasePort,app:INestApplication,base:string,now:number,sequence:number,mapId:number;
let config:SurvivalConfigService,document:SurvivalConfigDocument,engine:SurvivalEngineService,online:SurvivalOnlineService,queue:SurvivalQueueService,sessions:SessionService,survival:SurvivalService;
const clock={now:()=>new Date(now)},ids={next:()=>uuid(++sequence)};
const env={...environmentFixture(),debugBot:{password:"fixture-password"}} as ReturnType<typeof environmentFixture>;
const tokens=new TokenService(env);
const mods=[{id:"hwanhee1.corum_ranked",version:REQUIRED_RANKED_CLIENT_VERSION,enabled:true,loaded:true,internal:false,system:false},
 {id:"geode.node-ids",version:"v1.23.3",enabled:true,loaded:true,internal:false,system:false},
 {id:"syzzi.click_between_frames",version:"v1.5.0",enabled:true,loaded:true,internal:false,system:false,settings:{"soft-toggle":false,"click-on-steps":false,"physics-bypass":false}}];
interface Client {session:RankedSessionContext;token:string;account:string;channel:"release"|"debug";}
const clients:Client[]=[];
let spectator:SurvivalSpectatorLiveService;
let soloDoc:ReturnType<typeof configDocumentFixture>;
beforeAll(async()=>{
 if(process.env["SURVIVAL_TEST_DATABASE_URL"]){
  pool=new Pool({connectionString:process.env["SURVIVAL_TEST_DATABASE_URL"],max:24,options:"-c search_path="+testSchema});
  await pool.query(`CREATE SCHEMA "${testSchema}"`);
  db={query:async <T>(sql:string,values:readonly unknown[]=[])=>{const r=await pool!.query(sql,[...values]);return {rows:r.rows as T[],rowCount:r.rowCount??0};},
   transaction:async <T>(operation:(tx:SqlExecutor)=>Promise<T>)=>{const client=await pool!.connect();try{await client.query("BEGIN");const result=await operation({query:async <R>(sql:string,values:readonly unknown[]=[])=>{const r=await client.query(sql,[...values]);return {rows:r.rows as R[],rowCount:r.rowCount??0};}});await client.query("COMMIT");return result;}catch(e){await client.query("ROLLBACK");throw e;}finally{client.release();}},ping:async()=>true};
 }else {pg=new PGlite();db=new PgliteDatabase(pg);}
 const dir=new URL("../../../migrations/",import.meta.url);
 for(const f of (await readdir(dir)).filter(f=>f.endsWith(".sql")).sort())pool?await pool.query(await readFile(new URL(f,dir),"utf8")):await pg.exec(await readFile(new URL(f,dir),"utf8"));},60000);
afterAll(async()=>{if(pool){await pool.query(`DROP SCHEMA "${testSchema}" CASCADE`);await pool.end();}else await pg.close();});
afterEach(async()=>{if(app)await app.close();});
beforeEach(async()=>{
 await db.query("UPDATE ranked_achievement_versions SET activated_at='2000-01-01',debug_enabled_at='2000-01-01',overhaul_activated_at='2000-01-01'");
 await db.query("TRUNCATE ranked_players,sur_matches,sur_mode_state,sur_resource_quarantine CASCADE");now=Date.parse("2026-10-13T00:00:00Z");sequence=1000;mapId=40000000;clients.length=0;
 document={generation:"online-1",generatedAt:new Date(now).toISOString(),config:{...DEFAULT_SURVIVAL_CONFIG,enabled:true}};
 config=new SurvivalConfigService({fetchConfig:async()=>document},env,db);await config.refresh();
 soloDoc=configDocumentFixture();const solo={getSnapshot:()=>({...soloDoc,operational:{...soloDoc.operational,enabled:false}})} as RankedConfigService;
 engine=new SurvivalEngineService(db,clock,ids,config,{select:async(request)=>{const range=survivalMapRange(request.group,request.phase);
  return {levelId:mapId++,levelVersion:1,stars:range.minimumStars??10,demon:range.minimumStars===null,demonDifficulty:range.minimumStars===null?(range.demons[0]??null):null,
    automatic:false,platformer:false,geometrySha256:"a".repeat(64),geometryBytes:64,songIds:[123],sfxIds:[],verifiedAt:new Date(now).toISOString()};}});
 vi.spyOn(engine,"ensureMapSelection").mockImplementation(()=>{});
 const achievements=new AchievementService(db,clock,ids);survival=new SurvivalService(db,clock,ids,config,achievements);
 sessions=new SessionService(db,{fetchCurrentTier:async()=>"NONE"},clock,ids,solo,tokens);
 spectator=new SurvivalSpectatorLiveService(db,clock);
 online=new SurvivalOnlineService(db,clock,sessions,engine,survival,config,spectator);
 queue=new SurvivalQueueService(db,clock,{next:()=>42/0x100000000},env,config,sessions,engine,survival);
 const rates=new SurvivalRateService(db,clock);
 // Deterministic server clock tests explicitly tick production workers. This
 // leaves real HTTP handlers, auth, transactions, reducer and payment intact.
 vi.spyOn(online,"onApplicationBootstrap").mockImplementation(async()=>{});vi.spyOn(queue,"onApplicationBootstrap").mockImplementation(async()=>{});
 const module=await Test.createTestingModule({controllers:[SurvivalController,SurvivalReplayReceiptController,SurvivalSpectatorLiveController,SurvivalQueueController,SurvivalOnlineController,AchievementController,HistoryController],providers:[SessionGuard,AdmissionVersionGuard,SurvivalBadgesService,{provide:SurvivalService,useValue:survival},
  {provide:SurvivalSpectatorLiveService,useValue:spectator},{provide:DATABASE,useValue:db},{provide:SERVER_CLOCK,useValue:clock},{provide:TokenService,useValue:tokens},
  {provide:HistoryService,useValue:new HistoryService({history:async()=>({matches:[]})} as unknown as MatchService,survival,clock)},
  {provide:AchievementService,useValue:achievements},{provide:SurvivalQueueService,useValue:queue},{provide:SurvivalOnlineService,useValue:online},{provide:SurvivalRateService,useValue:rates}]}).compile();
 app=module.createNestApplication();app.useGlobalPipes(new ValidationPipe({whitelist:true,forbidNonWhitelisted:true,transform:true}));await app.listen(0,"127.0.0.1");base=await app.getUrl();
});
async function client(channel:Client["channel"]="release",account=String(5000+clients.length),takeOver=false){
 const created=await sessions.create({gdAccountId:account,gdUsername:`Player${account}`,clientVersion:REQUIRED_RANKED_CLIENT_VERSION,clientBuildChannel:channel,clientInstanceId:uuid(sequence+1),takeOver,installedMods:mods});
 const row=(await db.query<{id:string;player_id:string}>("SELECT id,player_id FROM ranked_sessions WHERE token_hash=$1",[tokens.hash(created.sessionToken)])).rows[0]!;
 const c:Client={token:created.sessionToken,account,channel,session:{sessionId:row.id,playerId:row.player_id,gdAccountId:account,gdUsername:`Player${account}`,displayedTier:"UNRANKED",hiddenMmr:0,placementGames:0,clientVersion:REQUIRED_RANKED_CLIENT_VERSION,clientBuildChannel:channel}};
 clients.push(c);return c;
}
async function api(c:Client,path:string,body?:unknown){const res=await fetch(base+"/api/ranked/survival"+path,{method:body===undefined?"GET":"POST",headers:{Authorization:"Bearer "+c.token,"Content-Type":"application/json"},...(body===undefined?{}:{body:JSON.stringify(body)})});return {status:res.status,body:await res.json() as any};}
async function join(c:Client){const r=await api(c,"/queue/join",{installedMods:mods});expect(r.status).toBe(201);return r.body;}
async function touch(list=clients){await db.query("UPDATE ranked_sessions SET last_heartbeat_at=$1 WHERE id=ANY($2::uuid[]) AND revoked_at IS NULL",[new Date(now).toISOString(),list.map(c=>c.session.sessionId)]);
 await db.query("UPDATE sur_queue_entries SET last_heartbeat_at=$1 WHERE session_id=ANY($2::uuid[]) AND status='QUEUED'",[new Date(now).toISOString(),list.map(c=>c.session.sessionId)]);}
async function game(count:number,fill=false){const group:Client[]=[];for(let i=0;i<count;i++)group.push(await client(i===0&&fill?"debug":"release"));await Promise.all(group.map(join));
 if(fill){const r=await api(group[0]!,"/queue/fill-bots",{password:"fixture-password",requestId:ids.next(),installedMods:mods});expect(r.status).toBe(201);return {group,id:r.body.matchId as string};}
 if(count<16){now+=180000;await touch(group);await queue.tickOnce();}
 const state=await api(group[0]!,"/queue/status");expect(state.body.status).toBe("MATCHED");return {group,id:state.body.matchId as string};}
async function event(c:Client,m:SurvivalMatch,type:string,fields:Record<string,unknown>={}){const r=survivalCurrentRound(m);const response=await api(c,`/online/matches/${m.id}/events`,{eventId:ids.next(),type,roundKey:r.key,mapGeneration:r.mapGeneration,levelId:r.map?.levelId??0,...(["ASSETS_READY","ENTERED","ATTEMPT_START"].includes(type)?{installedMods:mods}:{}),...fields});expect(response.status, `Survival ${type} rejected: ${JSON.stringify(response.body)}`).toBe(201);return response.body;}
async function prepare(id:string,group:Client[]){let m=await engine.get(id);now+=(await engine.get(id)).presentationGate==="INTRO"?6000:5000;await touch(group);await online.tickOnce();await engine.selectMap(id);m=await engine.get(id);
 for(const c of group)await event(c,m,"ASSETS_READY",{certificate:survivalCurrentRound(m).map});await online.tickOnce();m=await engine.get(id);
 now=survivalCurrentRound(m).entryRequestedAt!+5000;await touch(group);
 for(const c of group)await event(c,m,"ENTERED");m=await engine.get(id);
 // A server tick or presentation transition may already have advanced updatedAt.
 // Never rewind the mocked authoritative clock to an earlier startsAt.
 now=Math.max(now,survivalCurrentRound(m).startsAt!,m.updatedAt);
 await touch(group);await online.tickOnce();m=await engine.get(id);
 now=Math.max(now,m.updatedAt);return m;}
async function record(c:Client,m:SurvivalMatch,progress:number){const attemptId=ids.next();await event(c,m,"ATTEMPT_START",{attemptId});await event(c,m,"ATTEMPT_END",{attemptId,progress});}

describe("Survival M5 public HTTP and persistent online game",()=>{
 it("serves authenticated permanent badge collections from official results, preserving zero policy slots and history-reset totals",async()=>{
  expect((await fetch(base+"/api/ranked/survival/badges")).status).toBe(401);
  const {group,id}=await game(2),a=group[0]!,b=group[1]!;let m=await prepare(id,group);
  const empty=await api(a,"/badges");expect(empty.status).toBe(200);expect(empty.body.totals.ELIMINATED).toBe(0);
  expect(empty.body.difficulties.map((d:any)=>d.id)).toEqual(["HARD","HARDER","INSANE","EASY_DEMON","MEDIUM_DEMON","HARD_DEMON","INSANE_DEMON"]);
  await record(a,m,10);await record(b,m,20);now=survivalCurrentRound(m).endsAt!;await touch(group);await online.tickOnce();
  const counts=await api(a,"/badges");expect(counts.status).toBe(200);expect(counts.body.totals).toMatchObject({MYTHIC:0,LEGENDARY:0,ELIMINATED:1});
  expect((await api(b,"/badges")).body.totals).toMatchObject({MYTHIC:1,ELIMINATED:0});
  const history=(await api(a,"/history")).body;expect(history.matches[0].rounds[0].roundBadge).toMatchObject({grade:"ELIMINATED",rank:2,roundLabel:"FINAL"});
  const rows=(await db.query("SELECT * FROM sur_round_badges ORDER BY participant_id")).rows;expect(rows).toHaveLength(2);
  for(let i=0;i<3;i++)expect((await api(a,"/badges")).body).toEqual(counts.body);
  await new SurvivalBadgesService(db).onApplicationBootstrap();expect((await db.query("SELECT * FROM sur_round_badges ORDER BY participant_id")).rows).toEqual(rows);
  await db.query("UPDATE ranked_players SET gd_username='hwanhee1' WHERE id=$1",[a.session.playerId]);
  await new AdminService(db,env,{} as RankedConfigService,clock,config).execute({...a.session,gdUsername:"hwanhee1"},{action:"RESET_HISTORY",scope:"SURVIVAL",confirmation:"RESET",password:"fixture-password"});
  expect((await api(a,"/history")).body.matches).toHaveLength(0);expect((await api(a,"/badges")).body).toEqual(counts.body);
  expect((await api(a,"/badges",{grade:"MYTHIC",rank:1})).status).toBe(404);
 });

 it("holds the ready room for exactly 5s, accepts surrender during the wait and preserves its deadline",async()=>{
  const {group,id}=await game(8),owner=group[0]!,peers=group.slice(1);
  now+=(await engine.get(id)).presentationGate==="INTRO"?6000:5000;await touch(group);await online.tickOnce();await engine.selectMap(id);let m=await engine.get(id);
  for(const c of group)await event(c,m,"ASSETS_READY",{certificate:survivalCurrentRound(m).map});
  m=await engine.get(id);const readyAt=now,round=survivalCurrentRound(m);
  const view=(await api(owner,`/online/matches/${id}`)).body;
  expect(view.state).toBe("ENTERING");expect(Date.parse(view.entryOpensAt)).toBe(readyAt+5000);
  expect(Date.parse(view.entryDeadlineAt)).toBe(readyAt+10000);expect(view.startsAt).toBeNull();
  const early=()=>api(peers[0]!,`/online/matches/${id}/events`,{eventId:ids.next(),type:"ENTERED",roundKey:round.key,mapGeneration:round.mapGeneration,levelId:round.map!.levelId,installedMods:mods});
  expect((await early()).status).toBe(409);
  const surrendered=await event(owner,m,"FORFEIT");
  expect(surrendered.result).toMatchObject({rank:8,requestedDelta:-55});
  expect(surrendered.preparation.find((p:any)=>p.participantId===owner.session.playerId).forfeited).toBe(true);
  expect(surrendered.entryOpensAt).toBe(view.entryOpensAt);
  now=readyAt+4999;expect((await early()).status).toBe(409);
  now++;await touch(peers);for(const c of peers)await event(c,m,"ENTERED");
  m=await engine.get(id);expect(m.state).toBe("COUNTDOWN");
  expect(survivalCurrentRound(m).startsAt).toBe(readyAt+8000);
  expect(survivalCurrentRound(m).endsAt!-survivalCurrentRound(m).startsAt!).toBe(180000);
 });
 it("switches generic mod checks over HTTP without disconnecting the queued player",async()=>{
  const c=await client();const installedMods=[mods[0]!,{id:"other.mod",version:"1",enabled:true,loaded:true,internal:false,system:false}];
  expect((await api(c,"/queue/join",{installedMods})).status).toBe(403);
  Object.assign(soloDoc.operational,{modRestrictionEnabled:false});
  expect((await api(c,"/queue/join",{installedMods})).status).toBe(201);
  Object.assign(soloDoc.operational,{modRestrictionEnabled:true});
  expect((await api(c,"/queue/join",{installedMods})).status).toBe(403);
  expect((await api(c,"/queue/status")).body.status).toBe("QUEUED");
  Object.assign(soloDoc.operational,{modRestrictionEnabled:false});
  expect((await api(c,"/queue/join",{installedMods})).status).toBe(201);
 });
 it.each([2,16])("projects BEST/NOW and terminal 100%% over HTTP for %i players without paying or advancing the round",async count=>{
  const {group,id}=await game(count),m=await prepare(id,group),owner=group[0]!,attemptId=ids.next();
  await record(owner,m,87.9);await event(owner,m,"ATTEMPT_START",{attemptId});
  await event(owner,m,"PROGRESS",{attemptId,progress:63.9,currentProgress:40.2});
  const response=await api(group[1]!,`/online/matches/${id}`);
  expect(response.status).toBe(200);expect(response.body.roundPlayers).toHaveLength(count);
  expect(response.body.roundPlayers.find((p:any)=>p.participantId===owner.session.playerId)).toMatchObject({bestProgress:87.9,currentProgress:40.2,attemptActive:true,cleared:false});
  await event(owner,m,"ATTEMPT_END",{attemptId,progress:100});
  const cleared=await api(group[1]!,`/online/matches/${id}`);
  expect(cleared.body.state).toBe("PLAYING");
  expect(cleared.body.roundPlayers.find((p:any)=>p.participantId===owner.session.playerId)).toMatchObject({bestProgress:100,currentProgress:0,attemptActive:false,cleared:true});
  expect((await db.query("SELECT * FROM sur_lp_ledger")).rows).toHaveLength(0);
  const restarted=new SurvivalEngineService(db,clock,ids,config,{select:async()=>{throw new Error("No replacement map expected");}});
  const restored=survivalCurrentRound(await restarted.get(id)).players.find(p=>p.participantId===owner.session.playerId)!;
  expect(restored.attempts).toHaveLength(2);expect(restored.highestProgress).toBe(100);expect(restored.attempts.every(a=>a.endedAt!==null)).toBe(true);
 });
 it("Online late progress and end return conflicts without changing frozen scores or preventing the next round",async()=>{
  const {group,id}=await game(8);let m=await prepare(id,group);const owner=group[0]!,r=survivalCurrentRound(m);
  await record(owner,m,100);const open=ids.next();await event(owner,m,"ATTEMPT_START",{attemptId:open});
  await event(owner,m,"PROGRESS",{attemptId:open,progress:37});now=r.endsAt!;await touch(group);await online.tickOnce();
  m=await engine.get(id);expect(m.state).toBe("ROUND_RESULT");
  for(const type of ["PROGRESS","ATTEMPT_END"]){
   const response=await api(owner,`/online/matches/${id}/events`,{eventId:ids.next(),type,roundKey:r.key,mapGeneration:r.mapGeneration,levelId:r.map!.levelId,attemptId:open,progress:100});
   expect(response.status).toBe(409);expect(response.body.code).toBe("INVALID_MATCH_TRANSITION");
  }
  expect(survivalCurrentRound(await engine.get(id)).players.find(p=>p.participantId===owner.session.playerId)!.attempts.find(a=>a.id===open)!.highestProgress).toBe(37);
  now+=5000;await touch(group);await online.tickOnce();m=await engine.get(id);expect(m.state).toBe("MAP_SELECTION");
  expect(m.rounds).toHaveLength(2);expect(m.rounds[0]!.players.find(p=>p.participantId===owner.session.playerId)!.highestProgress).toBe(100);
 });
 it("Common Admin reset clears a finished online match, every mode profile/session and allows a genuinely fresh login",async()=>{
  const {group,id}=await game(2);const m=await prepare(id,group);await record(group[0]!,m,90);await record(group[1]!,m,20);
  now=survivalCurrentRound(m).endsAt!;await touch(group);await online.tickOnce();expect((await engine.get(id)).state).toBe("FINAL_RESULT");
  await db.query("UPDATE ranked_players SET gd_username='hwanhee1' WHERE id=$1",[group[0]!.session.playerId]);
  const admin=new AdminService(db,env,{getSnapshot:()=>configDocumentFixture()} as RankedConfigService,clock,config);
  const wiped=await admin.execute({...group[0]!.session,gdUsername:'hwanhee1'},{password:'fixture-password',action:'RESET_DB',scope:'COMMON',confirmation:'RESET'});
  expect(wiped).toMatchObject({reset:true,accountsDeleted:true});
  for(const table of ['ranked_players','ranked_sessions','ranked_profiles','sur_profiles','sur_matches','sur_lp_ledger','sur_online_players'])
   expect((await db.query(`SELECT * FROM ${table}`)).rows).toHaveLength(0);
  expect((await api(group[0]!,"/online/active")).status).toBe(401);
  const fresh=await client('release',group[0]!.account);expect(fresh.session.playerId).not.toBe(group[0]!.session.playerId);
  expect((await api(fresh,"/queue/status")).status).toBe(200);
 });
 it.each([8,12,15,16])("matches %i actual sessions, same assignment, and independent Solo OFF admission",async n=>{
  const {group,id}=await game(n);const m=await engine.get(id);expect(m.players).toHaveLength(n);expect(m.online).toBe(true);
  const states=await Promise.all(group.map(c=>api(c,"/queue/status")));expect(states.every(s=>s.body.matchId===id)).toBe(true);
  expect((await db.query("SELECT id FROM ranked_matches")).rows).toHaveLength(0);
 });
 it.each([2,3,4,7])("starts %i humans after 180 seconds with the agreed short phases",async n=>{
   const {id}=await game(n);expect(survivalCurrentRound(await engine.get(id)).phase).toBe(survivalInitialPhase(n));
 });
 it("restarts one player's waiting cycle instead of creating a one-person game",async()=>{
   const c=await client();await join(c);now+=180000;await touch();await queue.tickOnce();const s=await api(c,"/queue/status");
   expect(s.body).toMatchObject({status:"QUEUED",players:1,waitingSeconds:0,remainingSeconds:180});expect((await db.query("SELECT id FROM sur_matches")).rows).toHaveLength(0);
 });
 it.each([1,3,8,15])("fills %i actual participants to 16; release peers share the normal engine and all receive LP",async n=>{
   const {group,id}=await game(n,true);const m=await engine.get(id);expect(m.players.filter(p=>p.kind==="BOT")).toHaveLength(16-n);
   expect((await db.query("SELECT player_id FROM sur_profiles")).rows).toHaveLength(n);
   for(let stage=0;stage<8;stage++){
     const snapshot=await engine.get(id);if(snapshot.state==="FINAL_RESULT")break;
     const playing=group.filter(c=>snapshot.players.find(p=>p.playerId===c.session.playerId)?.eliminationStage===null);
     const live=await prepare(id,playing);for(const c of playing)await record(c,live,0);
     now=survivalCurrentRound(live).endsAt!;await touch(group);await online.tickOnce();
     if((await engine.get(id)).state==="FINAL_RESULT")break;
   }
   const done=await engine.get(id);expect(done.state).toBe("FINAL_RESULT");
   expect((await db.query("SELECT * FROM sur_lp_ledger")).rows).toHaveLength(n);expect(done.players.filter(p=>p.kind==="BOT").every(p=>p.playerId===null)).toBe(true);
 });
 it("rejects release/incorrect-password/nonqueued fill requests and cannot exceed one assignment under simultaneous fills",async()=>{
   const owner=await client("debug"),peer=await client("release"),another=await client("debug");await Promise.all([owner,peer,another].map(join));
   expect((await api(peer,"/queue/fill-bots",{password:"fixture-password",requestId:ids.next(),installedMods:mods})).status).toBe(403);
   expect((await api(owner,"/queue/fill-bots",{password:"wrong",requestId:ids.next(),installedMods:mods})).status).toBe(403);
   const request={password:"fixture-password",requestId:ids.next(),installedMods:mods};const responses=await Promise.all([api(owner,"/queue/fill-bots",request),api(another,"/queue/fill-bots",{...request,requestId:ids.next()})]);
   expect(responses.every(r=>r.status===201)).toBe(true);expect(new Set(responses.map(r=>r.body.matchId)).size).toBe(1);
   expect((await db.query("SELECT id FROM sur_matches")).rows).toHaveLength(1);expect((await api(owner,"/queue/fill-bots",request)).body.matchId).toBe(responses[0]!.body.matchId);
 });
 it("accepts waiting-room surrender before map selection, settles once and lets the player acknowledge",async()=>{
   const {group,id}=await game(8),owner=group[0]!;const m=await engine.get(id),round=survivalCurrentRound(m);
   expect(m.state).toBe("MAP_SELECTION");expect(round.map).toBeNull();
   const solo=(await db.query("SELECT * FROM ranked_profiles ORDER BY player_id")).rows;
   const body={eventId:ids.next(),type:"FORFEIT",roundKey:round.key,mapGeneration:round.mapGeneration,levelId:0};
   const accepted=await api(owner,`/online/matches/${id}/events`,body);
   expect(accepted.status).toBe(201);expect(accepted.body.result).toMatchObject({rank:8,requestedDelta:-55});
   const duplicate=await api(owner,`/online/matches/${id}/events`,body);expect(duplicate.status).toBe(201);
   expect((await db.query("SELECT * FROM sur_lp_ledger")).rows).toHaveLength(1);
   expect((await db.query("SELECT * FROM ranked_profiles ORDER BY player_id")).rows).toEqual(solo);
   expect((await api(owner,`/online/matches/${id}/acknowledge`,{})).status).toBe(201);
   expect((await engine.get(id)).players.find(p=>p.playerId===owner.session.playerId)?.eliminationStage).not.toBeNull();
 });
 it("immediately settles multiple surrenderers at the fixed round floor, lets peers continue and never pays twice",async()=>{
   const {group,id}=await game(8);let m=await prepare(id,group);const solo=(await db.query("SELECT * FROM ranked_profiles ORDER BY player_id")).rows;
   for(const c of group.slice(0,2)){const result=await event(c,m,"FORFEIT");expect(result.result).toMatchObject({rank:8,requestedDelta:-55});await api(c,`/online/matches/${id}/acknowledge`,{});}
   m=await engine.get(id);expect(m.state).toBe("PLAYING");await Promise.all([online.tickOnce(),online.tickOnce()]);
   expect((await db.query("SELECT * FROM sur_lp_ledger")).rows).toHaveLength(2);expect((await db.query("SELECT * FROM ranked_profiles ORDER BY player_id")).rows).toEqual(solo);
   const history=await survival.history(group[0]!.session.playerId);expect(history.matches[0]).toMatchObject({inProgress:true,finalRank:null,result:null});
 });
 it("opens owned in-progress history after elimination with finalized ties only, and enforces detail authorization",async()=>{
   const {group,id}=await game(8);let m=await prepare(id,group);
   await record(group[0]!,m,82.9);await record(group[1]!,m,82.1);
   const eliminated=group.at(-1)!;await event(eliminated,m,"FORFEIT");
   now=survivalCurrentRound(m).endsAt!+1;await touch(group);await online.tickOnce();m=await engine.get(id);
   expect(m.rounds[0]!.finishedAt).not.toBeNull();
   const owner=group[0]!;const history=(await survival.history(owner.session.playerId,{summary:true})).matches[0]!;
   expect(history.inProgress).toBe(true);expect(history.result).toBeNull();expect(history.finishedAt).toBeNull();
   const read=(match:string,token=owner.token)=>fetch(base+"/api/ranked/history/SURVIVAL/"+match,{headers:{Authorization:"Bearer "+token}});
   const response=await read(id);expect(response.status).toBe(200);const payload=await response.json() as any;
   expect(payload.match.rounds[0].completed).toBe(true);expect(payload.match.rounds[0].standings.filter((p:any)=>p.rank===1)).toHaveLength(2);
   expect(JSON.stringify(payload)).not.toContain("achievementCheckpoint");expect(JSON.stringify(payload)).not.toContain("driver_state");
   const outsider=await client();expect((await read(id,outsider.token)).status).toBe(404);
   expect((await read("bad-id")).status).toBe(400);expect((await read(id,"invalid-session")).status).toBe(401);
   await online.acknowledge(id,eliminated.session);
   expect((await survival.history(eliminated.session.playerId,{matchId:id})).matches[0]).toMatchObject({inProgress:true,result:null});
 });
 it("disabling Survival clears waiting players before publication and preserves assigned matches",async()=>{
   const {group,id}=await game(16);const waiting=await client();await join(waiting);
   document={...document,generation:"off",config:{...document.config,enabled:false}};expect(await config.refresh()).toBe(true);
   expect((await api(waiting,"/queue/status")).body.status).toBe("DISABLED");expect((await api(waiting,"/queue/join",{installedMods:mods})).status).toBe(503);
   expect((await api(group[0]!,`/online/matches/${id}`)).status).toBe(200);expect((await engine.get(id)).players).toHaveLength(16);
 });
 it("executes four rounds over real HTTP for sixteen users, pays eliminations early, determines one winner and recovers results",async()=>{
   const {group,id}=await game(16);let active=[...group];const eliminated:Client[]=[];
   for(let stage=0;stage<4;stage++){
     let m=await prepare(id,active);for(let i=0;i<active.length;i++)await record(active[i]!,m,100-i);
     now=survivalCurrentRound(m).endsAt!;await touch(active);await online.tickOnce();m=await engine.get(id);
     const newly=active.filter(c=>m.players.find(p=>p.playerId===c.session.playerId)!.eliminationStage!==null);
     eliminated.push(...newly);active=active.filter(c=>!newly.includes(c));
     for(const c of newly){const view=await api(c,`/online/matches/${id}`);expect(view.body.result).not.toBeNull();}
   }
   const final=await engine.get(id);expect(final.state).toBe("FINAL_RESULT");expect(final.players.filter(p=>p.finalRank===1)).toHaveLength(1);
   expect((await db.query("SELECT * FROM sur_lp_ledger")).rows).toHaveLength(16);expect(eliminated).toHaveLength(15);
   await touch(group);const winner=group[0]!;const recovered=await api(winner,"/online/active");expect(recovered.body.match.result.rank).toBe(1);
   await Promise.all(group.map(c=>api(c,`/online/matches/${id}`)));expect((await db.query("SELECT * FROM sur_lp_ledger")).rows).toHaveLength(16);
 },30000);
 it("limits a concurrent 19-user burst to one 16-player match and three waiting sessions",async()=>{
   const group:Client[]=[];for(let i=0;i<19;i++)group.push(await client());
   await Promise.all(group.map(join));await queue.tickOnce();
   const matches=(await db.query<{snapshot:SurvivalMatch}>("SELECT snapshot FROM sur_matches")).rows;
   expect(matches).toHaveLength(1);expect(matches[0]!.snapshot.players).toHaveLength(16);
   expect((await db.query("SELECT player_id FROM sur_queue_entries WHERE status='QUEUED'")).rows).toHaveLength(3);
   expect(new Set(matches[0]!.snapshot.players.map(p=>p.playerId)).size).toBe(16);
 });
 it("persists queue cycles across worker restart and makes Join/Leave idempotent",async()=>{
   const a=await client(),b=await client();await join(a);const entry=(await db.query("SELECT entry_id FROM sur_queue_entries WHERE player_id=$1",[a.session.playerId])).rows[0];
   now+=2000;await join(a);expect((await db.query("SELECT entry_id FROM sur_queue_entries WHERE player_id=$1",[a.session.playerId])).rows[0]).toEqual(entry);
   await join(b);await api(b,"/queue/leave",{});await api(b,"/queue/leave",{});expect((await api(b,"/queue/status")).body.status).toBe("LEFT");await join(b);
   now+=180000;await touch();const restarted=new SurvivalQueueService(db,clock,{next:()=>.1},env,config,sessions,engine,survival);
   await Promise.all([restarted.tickOnce(),queue.tickOnce()]);expect((await db.query("SELECT id FROM sur_matches")).rows).toHaveLength(1);
 });
 it("rotates queue ownership to a new app session without resetting waiting time and rejects the old token",async()=>{
   const a=await client();await join(a);now+=20000;const fresh=await client(a.channel,a.account,true);
   expect((await api(fresh,"/queue/status")).body).toMatchObject({status:"QUEUED",waitingSeconds:20});
   expect((await api(a,"/queue/leave",{})).status).toBe(401);
   expect((await api(fresh,"/queue/heartbeat",{})).status).toBe(201);
 });
 it("excludes an absent download from readiness, then accepts certified mid-round native re-entry and progress",async()=>{
   const {group,id}=await game(8),offline=group[0]!,peers=group.slice(1);
   now+=(await engine.get(id)).presentationGate==="INTRO"?6000:5000;await touch(peers);await online.tickOnce();await engine.selectMap(id);let m=await engine.get(id);
   for(const c of peers)await event(c,m,"ASSETS_READY",{certificate:survivalCurrentRound(m).map});
   now=survivalCurrentRound(m).preparationStartedAt!+15000;await touch(peers);await online.tickOnce();m=await engine.get(id);
   expect(m.state).toBe("ENTERING");expect(survivalCurrentRound(m).players.find(p=>p.participantId===offline.session.playerId)).toMatchObject({absent:true,assetsReadyAt:null,enteredAt:null});
   now=survivalCurrentRound(m).entryRequestedAt!+5000;await touch(peers);
   for(const c of peers)await event(c,m,"ENTERED");m=await engine.get(id);now=survivalCurrentRound(m).startsAt!;await touch(peers);await online.tickOnce();m=await engine.get(id);
   const end=survivalCurrentRound(m).endsAt;const fresh=await client(offline.channel,offline.account,true);
   expect((await api(fresh,"/online/active")).body.match.matchId).toBe(id);
   await event(fresh,m,"ASSETS_READY",{certificate:survivalCurrentRound(m).map});await event(fresh,m,"ENTERED");await record(fresh,m,93.456);
   m=await engine.get(id);expect(survivalCurrentRound(m).endsAt).toBe(end);expect(survivalCurrentRound(m).players.find(p=>p.participantId===offline.session.playerId)?.highestProgress).toBe(93.456);
   expect((await api(offline,`/online/matches/${id}`)).status).toBe(401);
 });
 it("keeps all-disconnected 0% rounds and repeated finals running, without false native readiness or cancellation",async()=>{
   const {group,id}=await game(8);
   for(let stage=0;stage<5;stage++){
     now+=(await engine.get(id)).presentationGate==="INTRO"?6000:5000;await online.tickOnce();await engine.selectMap(id);let m=await engine.get(id);
     now=survivalCurrentRound(m).preparationStartedAt!+15000;await online.tickOnce();m=await engine.get(id);
     expect(m.state).toBe("COUNTDOWN");expect(survivalCurrentRound(m).players.every(p=>p.assetsReadyAt===null&&p.enteredAt===null)).toBe(true);
     now=survivalCurrentRound(m).endsAt!;await online.tickOnce();m=await engine.get(id);
     expect(m.state).toBe("ROUND_RESULT");expect(m.cancellationReason).toBeNull();expect(m.winnerId).toBeNull();
   }
   expect((await db.query("SELECT * FROM sur_lp_ledger")).rows).toHaveLength(0);
   now+=(await engine.get(id)).presentationGate==="INTRO"?6000:5000;await online.tickOnce();await engine.selectMap(id);let m=await engine.get(id);expect(survivalCurrentRound(m).phase).toBe("TIEBREAK");
   now=survivalCurrentRound(m).preparationStartedAt!+15000;await online.tickOnce();m=await engine.get(id);now=survivalCurrentRound(m).startsAt!;await online.tickOnce();m=await engine.get(id);
   const fresh=await client(group[0]!.channel,group[0]!.account,true);await event(fresh,m,"ASSETS_READY",{certificate:survivalCurrentRound(m).map});await event(fresh,m,"ENTERED");
   await record(fresh,m,94.371);now=survivalCurrentRound(m).endsAt!;await online.tickOnce();m=await engine.get(id);
   expect(m.state).toBe("FINAL_RESULT");expect(m.winnerId).toBe(fresh.session.playerId);expect((await db.query("SELECT * FROM sur_lp_ledger")).rows).toHaveLength(8);
 });
 it("uses all real entrants' shared countdown, keeps playing peers when another disconnects and recovers an open attempt",async()=>{
   const {group,id}=await game(8);let m=await prepare(id,group),owner=group[0]!;
   const attemptId=ids.next();await event(owner,m,"ATTEMPT_START",{attemptId});await event(owner,m,"PROGRESS",{attemptId,progress:51.25});
   now+=15000;await touch(group.slice(1));await online.tickOnce();const fresh=await client(owner.channel,owner.account,true);
   const view=(await api(fresh,"/online/active")).body.match;expect(view).toMatchObject({state:"PLAYING",myActiveAttemptId:attemptId,myActiveAttemptProgress:51.25});
   expect(view.remainingSeconds).toBe(165);await event(fresh,m,"ATTEMPT_END",{attemptId,progress:51.25});await record(fresh,m,62);
   expect(survivalCurrentRound(await engine.get(id)).players[0]!.highestProgress).toBe(62);
 });
 it("waits for six DM attempts, rounds only DM to .001%, repeats exact ties and pays a unique winner once",async()=>{
   const {group,id}=await game(2);let m=await prepare(id,group);
   for(const c of group)await record(c,m,100);now=survivalCurrentRound(m).endsAt!;await touch(group);await online.tickOnce();
   m=await prepare(id,group);expect(survivalCurrentRound(m).phase).toBe("TIEBREAK");
   for(const c of group)for(const score of [20,30,40,50,60,94.3704])await record(c,m,score);
   m=await engine.get(id);expect(m.state).toBe("ROUND_RESULT");expect((await db.query("SELECT * FROM sur_lp_ledger")).rows).toHaveLength(0);
   m=await prepare(id,group);for(const score of [20,30,40,50,60,94.3704])await record(group[0]!,m,score);
   expect((await engine.get(id)).state).toBe("PLAYING");
   for(const score of [20,30,40,50,60,94.3714])await record(group[1]!,m,score);
   m=await engine.get(id);expect(m.state).toBe("FINAL_RESULT");expect(m.winnerId).toBe(group[1]!.session.playerId);
   const result=(await api(group[1]!,`/online/matches/${id}`)).body.result;expect(result).toMatchObject({rank:1,requestedDelta:50});
   expect((await db.query("SELECT * FROM sur_lp_ledger")).rows).toHaveLength(2);
 });
 it("refuses foreign score writes, changed duplicate payloads, arbitrary maps, out-of-range progress and stale sessions",async()=>{
   const {group,id}=await game(8);const stranger=await client();let m=await prepare(id,group);const c=group[0]!,r=survivalCurrentRound(m);
   expect((await api(stranger,`/online/matches/${id}`)).status).toBe(403);
   const start={eventId:ids.next(),type:"ATTEMPT_START",roundKey:r.key,mapGeneration:r.mapGeneration,levelId:r.map!.levelId,attemptId:ids.next(),installedMods:mods};
   expect((await api(c,`/online/matches/${id}/events`,{...start,participantId:group[1]!.session.playerId})).status).toBe(400);
   expect((await api(c,`/online/matches/${id}/events`,start)).status).toBe(201);expect((await api(c,`/online/matches/${id}/events`,start)).status).toBe(201);
   expect((await api(c,`/online/matches/${id}/events`,{...start,attemptId:ids.next()})).status).toBe(409);
   const score={eventId:ids.next(),type:"PROGRESS",roundKey:r.key,mapGeneration:r.mapGeneration,levelId:r.map!.levelId,attemptId:start.attemptId,progress:70};
   expect((await api(group[1]!,`/online/matches/${id}/events`,score)).status).toBe(409);
   expect((await api(c,`/online/matches/${id}/events`,{...score,progress:101})).status).toBe(400);
   expect((await api(c,`/online/matches/${id}/events`,{...score,levelId:50000000})).status).toBe(409);
   await event(c,m,"FORFEIT");expect((await api(c,`/online/matches/${id}/events`,score)).status).toBe(409);
 });
 it("catches preparation failure without blaming successful peers and rejects a mismatched native certificate",async()=>{
   const {group,id}=await game(8);now+=(await engine.get(id)).presentationGate==="INTRO"?6000:5000;await touch();await online.tickOnce();await engine.selectMap(id);let m=await engine.get(id);
   const bad=await api(group[0]!,`/online/matches/${id}/events`,{eventId:ids.next(),type:"ASSETS_READY",roundKey:survivalCurrentRound(m).key,mapGeneration:1,levelId:survivalCurrentRound(m).map!.levelId,installedMods:mods,certificate:{...survivalCurrentRound(m).map,geometrySha256:"b".repeat(64)}});
   expect(bad.status).toBe(409);
   for(const c of group)await event(c,m,"PREPARATION_FAILED");m=await engine.get(id);expect(m.state).toBe("MAP_SELECTION");expect(m.players.every(p=>p.eliminationStage===null)).toBe(true);
 });
 it("preserves mixed matches while another human survives and settles only humans when bots alone remain",async()=>{
   const {group,id}=await game(3,true);let m=await prepare(id,group);await record(group[0]!,m,100);
   now=survivalCurrentRound(m).endsAt!;await touch();await online.tickOnce();m=await engine.get(id);expect(m.state).toBe("ROUND_RESULT");
   expect(m.players.find(p=>p.playerId===group[0]!.session.playerId)?.eliminationStage).toBeNull();
   const remaining=group.filter(c=>m.players.find(p=>p.playerId===c.session.playerId)!.eliminationStage===null);
   m=await prepare(id,remaining);await record(remaining[0]!,m,99);
   for(const c of remaining)await event(c,m,"FORFEIT");m=await engine.get(id);expect(m.state).toBe("PLAYING");
   expect(m.players.filter(p=>p.kind==="HUMAN").every(p=>p.eliminationStage!==null)).toBe(true);
   expect(m.winnerId).toBeNull();
   expect((await db.query("SELECT * FROM sur_lp_ledger")).rows).toHaveLength(3);expect((await db.query("SELECT id FROM ranked_players WHERE is_bot")).rows).toHaveLength(0);
 });
 it("recreates the engine and bot adapter from persisted seeds and never reruns scheduled draws",async()=>{
   const {group,id}=await game(3,true);let m=await prepare(id,group);now+=50000;await touch();await online.tickOnce();m=await engine.get(id);
   const row=(await db.query("SELECT driver_state FROM sur_online_matches WHERE match_id=$1",[id])).rows[0];
   const restarted=new SurvivalEngineService(db,clock,ids,config,{select:async()=>{throw new Error("unexpected map selection");}});
   vi.spyOn(restarted,"ensureMapSelection").mockImplementation(()=>{});const driver=new SurvivalOnlineService(db,clock,sessions,restarted,survival,config);
   await Promise.all([driver.tickOnce(),online.tickOnce()]);expect((await restarted.get(id)).revision).toBe(m.revision);
   expect((await db.query("SELECT driver_state FROM sur_online_matches WHERE match_id=$1",[id])).rows[0]).toEqual(row);
 });
 it("applies online GOLD promotion once and keeps Solo metrics/MMR untouched",async()=>{
   const a=await client(),b=await client();await survival.profile(a.session.playerId);await survival.profile(b.session.playerId);
   await db.query("UPDATE sur_profiles SET lp=1199,displayed_tier='SILVER' WHERE player_id=$1",[a.session.playerId]);
   const solo=(await db.query("SELECT * FROM ranked_profiles ORDER BY player_id")).rows;await Promise.all([join(a),join(b)]);
   now+=180000;await touch();await queue.tickOnce();const id=(await api(a,"/queue/status")).body.matchId as string;
   const m=await prepare(id,[a,b]);await record(a,m,100);await record(b,m,20);now=survivalCurrentRound(m).endsAt!;await touch();await online.tickOnce();
   expect((await survival.profile(a.session.playerId)).profile).toMatchObject({lp:1249,tier:"GOLD"});
   await Promise.all([online.settle(await engine.get(id)),online.settle(await engine.get(id))]);
   expect((await db.query("SELECT * FROM sur_lp_ledger")).rows).toHaveLength(2);
   expect((await db.query("SELECT player_id FROM ranked_achievement_unlocks WHERE achievement_id='gold_reached'")).rows).toEqual([{player_id:a.session.playerId}]);
   expect((await db.query("SELECT * FROM ranked_profiles ORDER BY player_id")).rows).toEqual(solo);
   const metrics=(await db.query("SELECT ranked_matches,ranked_wins,survival_matches,survival_wins,survival_podiums,survival_finals,survival_round_wins FROM ranked_achievement_metrics WHERE player_id=$1",[a.session.playerId])).rows[0];
   expect(metrics).toEqual({ranked_matches:0,ranked_wins:0,survival_matches:1,survival_wins:1,survival_podiums:1,survival_finals:1,survival_round_wins:1});
   const saved=(await db.query<{result:Record<string,unknown>}>("SELECT result FROM sur_online_players WHERE match_id=$1 AND player_id=$2",[id,a.session.playerId])).rows[0]!.result;
   expect(saved.achievementUnlocks).toEqual(expect.arrayContaining([expect.objectContaining({achievementId:'survival_first_match'}),expect.objectContaining({achievementId:'survival_final_perfect_win',rarity:'LEGENDARY'})]));
   const achievements=new AchievementService(db,clock,ids);const claims=await Promise.all([achievements.claim(a.session.playerId,"gold_reached"),achievements.claim(a.session.playerId,"gold_reached")]);
   expect(claims.reduce((sum,claim)=>sum+claim.claimedAmount,0)).toBe(50);
 });
 it("authenticates Claim All, validates scope and isolates the session owner through real HTTP",async()=>{
   const a=await client(),b=await client();const achievements=new AchievementService(db,clock,ids);await achievements.state(a.session.playerId);
   await db.query(`INSERT INTO ranked_achievement_unlocks(player_id,achievement_id,achievement_level,reward_ap,unlocked_at,source_event_key)
    VALUES($1,'survival_first_match',0,10,$2,'http-fixture'),($1,'dual_mode_debut',0,10,$2,'http-fixture')`,[a.session.playerId,clock.now().toISOString()]);
   const endpoint=base+'/api/ranked/achievements/claim-all';
   const call=async(c:Client|null,payload:unknown)=>{const r=await fetch(endpoint,{method:'POST',headers:{'Content-Type':'application/json',...(c?{Authorization:'Bearer '+c.token}:{})},body:JSON.stringify(payload)});return {status:r.status,body:await r.json()};};
   expect((await call(null,{scope:'ALL'})).status).toBe(401);
   expect((await call(a,{scope:'FORGED'})).status).toBe(400);
   expect((await call(a,{scope:'ALL',playerId:b.session.playerId})).status).toBe(400);
   expect((await call(b,{scope:'ALL'})).body).toMatchObject({claimedAmount:0,claimedRewards:0,totalAp:0});
   const results=await Promise.all([call(a,{scope:'SURVIVAL'}),call(a,{scope:'ALL'}),call(a,{scope:'ALL'})]);
   expect(results.every(r=>r.status===201)).toBe(true);
   expect(results.reduce((sum,r)=>sum+r.body.claimedAmount,0)).toBe(20);
   expect((await achievements.state(a.session.playerId)).totalAp).toBe(20);
 });
 it("survival Admin reset clears completed assignments/Fill receipts without deleting Solo or common data",async()=>{
   const {group,id}=await game(1,true),c=group[0]!;let m=await engine.get(id);await event(c,m,"FORFEIT");m=await engine.get(id);expect(m.players.find(p=>p.playerId===c.session.playerId)?.eliminationStage).not.toBeNull();
   for(let stage=0;stage<8 && !["FINAL_RESULT","CANCELLED"].includes(m.state);stage++){
     m=await prepare(id,[]);now=survivalCurrentRound(m).endsAt!;await online.tickOnce();m=await engine.get(id);
   }
   expect(m.state).toBe("FINAL_RESULT");await touch(group);
   await db.query("UPDATE ranked_players SET gd_username='hwanhee1' WHERE id=$1",[c.session.playerId]);
   const solo=(await db.query("SELECT * FROM ranked_profiles ORDER BY player_id")).rows;
   const admin=new AdminService(db,env,{} as RankedConfigService,clock,config);
   await admin.execute({...c.session,gdUsername:"hwanhee1"},{action:"RESET_DB",scope:"SURVIVAL",confirmation:"RESET",password:"fixture-password"});
   expect((await db.query("SELECT * FROM sur_matches")).rows).toHaveLength(0);expect((await db.query("SELECT * FROM sur_queue_fill_requests")).rows).toHaveLength(0);
   expect((await api(c,"/queue/status")).body.status).toBe("LEFT");expect((await db.query("SELECT * FROM ranked_profiles ORDER BY player_id")).rows).toEqual(solo);
 });
 it("enforces current-version admission, environment policy and an atomic request limit",async()=>{
   const c=await client();await db.query("UPDATE ranked_sessions SET client_version='v0.5.0-beta.67' WHERE id=$1",[c.session.sessionId]);
   expect((await api(c,"/queue/join",{installedMods:mods})).status).toBe(426);
   await db.query("UPDATE ranked_sessions SET client_version=$2 WHERE id=$1",[c.session.sessionId,REQUIRED_RANKED_CLIENT_VERSION]);
   expect((await api(c,"/queue/join",{installedMods:mods.filter(m=>m.id!=="syzzi.click_between_frames")})).status).toBe(403);
   await db.query("DELETE FROM sur_request_rates WHERE player_id=$1",[c.session.playerId]);
   const rates=new SurvivalRateService(db,clock);await Promise.all(Array.from({length:60},()=>rates.check(c.session.playerId,"queue")));
   expect((await api(c,"/queue/heartbeat",{})).status).toBe(429);
 });

 it("accepts an app restart just before preparation closes, even when the first fresh poll renews presence",async()=>{
   const {group,id}=await game(8),old=group[0]!,peers=group.slice(1);now+=(await engine.get(id)).presentationGate==="INTRO"?6000:5000;await touch();await online.tickOnce();await engine.selectMap(id);let m=await engine.get(id);
   for(const c of peers)await event(c,m,"ASSETS_READY",{certificate:survivalCurrentRound(m).map});
   now=survivalCurrentRound(m).preparationStartedAt!+13000;await touch(peers);const fresh=await client(old.channel,old.account,true);
   const view=(await api(fresh,"/online/active")).body.match;expect(view.state).toBe("ENTERING");m=await engine.get(id);
   expect(survivalCurrentRound(m).players[0]).toMatchObject({absent:true,assetsReadyAt:null,enteredAt:null});
   now=survivalCurrentRound(m).entryRequestedAt!+5000;await touch(peers);
   for(const c of peers)await event(c,m,"ENTERED");now+=13000;await touch(peers);await online.tickOnce();m=await engine.get(id);expect(m.state).toBe("PLAYING");
   await event(fresh,m,"ASSETS_READY",{certificate:survivalCurrentRound(m).map});await event(fresh,m,"ENTERED");await record(fresh,m,81);
   expect(survivalCurrentRound(await engine.get(id)).players[0]).toMatchObject({absent:false,highestProgress:81});
   expect((await db.query<{recovering:boolean}>("SELECT recovering FROM sur_online_players WHERE match_id=$1 AND player_id=$2",[id,fresh.session.playerId])).rows[0]!.recovering).toBe(false);
 });

});

describe("exact requested queue bot counts",()=>{
 it.each([[1,1],[1,3],[1,7],[1,15],[3,5],[5,0]])("starts %i humans with exactly %i bots immediately",async(humans,bots)=>{
  const group:Client[]=[];for(let i=0;i<humans;i++)group.push(await client(i===0?"debug":"release"));
  for(const c of group)await join(c);
  const admission={password:"fixture-password",requestId:ids.next(),installedMods:mods,botCount:bots};
  const started=await api(group[0]!,"/queue/fill-bots",admission);expect(started.status).toBe(201);
  const m=await engine.get(started.body.matchId);expect(m.players.filter(p=>p.kind==="BOT")).toHaveLength(bots);
  expect(m.players).toHaveLength(humans+bots);expect(now-m.createdAt).toBe(0);
  expect(survivalCurrentRound(m).phase).toBe(survivalInitialPhase(humans+bots));
  expect((await api(group[0]!,"/queue/fill-bots",admission)).body.matchId).toBe(m.id);
  expect((await api(group[0]!,"/queue/fill-bots",{...admission,botCount:bots===0?1:bots-1})).status).toBe(409);
 });
 it("refuses too few or too many participants without adjusting the requested bots",async()=>{
  const c=await client("debug");await join(c);
  expect((await api(c,"/queue/fill-bots",{password:"fixture-password",requestId:ids.next(),installedMods:mods,botCount:0})).status).toBe(400);
  expect((await db.query("SELECT id FROM sur_matches")).rows).toHaveLength(0);
 });
});


it("commits global map history once under simultaneous selections on independent DB transactions",async()=>{
 let arrived=0,release!:()=>void;const both=new Promise<void>(resolve=>{release=resolve;});
 const selector=vi.fn(async(request:{excludedLevelIds:readonly number[]})=>{
  let id=40000000;while(request.excludedLevelIds.includes(id))id++;
  if(id===40000000){if(++arrived===2)release();await both;}
  return {levelId:id,levelVersion:1,length:2,stars:4,demon:false,demonDifficulty:null,automatic:false,platformer:false,
   geometrySha256:"a".repeat(64),geometryBytes:64,verifiedAt:new Date(now).toISOString()};
 });
 const selecting=new SurvivalEngineService(db,clock,ids,config,{select:selector});
 const bots=Array.from({length:16},(_,i)=>({participantId:`history-bot-${i}`,kind:"BOT" as const,name:`Bot ${i}`,lp:150}));
 const a=await selecting.create("history-one",bots),b=await selecting.create("history-two",bots);
 await Promise.all([selecting.selectMap(a.id),selecting.selectMap(b.id)]);
 const history=(await db.query<{level_id:string;history_window:number}>("SELECT level_id,history_window FROM sur_map_selection_history ORDER BY id")).rows;
 expect(history.map(row=>Number(row.level_id)).sort()).toEqual([40000000,40000001]);
 expect(history.every(row=>row.history_window===30)).toBe(true);expect(selector).toHaveBeenCalledTimes(3);
});

it('serializes a server cutoff observation with duplicate progress after reconnect and keeps evidence out of public HTTP',async()=>{
 const {group,id}=await game(8);let m=await prepare(id,group);
 const attempts=new Map<string,string>();
 for(const [i,c] of group.entries()){
  const attemptId=ids.next();attempts.set(c.session.playerId,attemptId);
  await event(c,m,'ATTEMPT_START',{attemptId});await event(c,m,'PROGRESS',{attemptId,progress:90-i});
 }
 now=survivalCurrentRound(m).endsAt!-10_000;await touch(group);
 await engine.recoverActiveMatches();m=await engine.get(id);
 const r=survivalCurrentRound(m),checkpoint=r.achievementCheckpoint!;
 expect(checkpoint.status).toBe('CAPTURED');
 const risk=checkpoint.standings.find(p=>p.atRisk)!,owner=group.find(c=>c.session.playerId===risk.participantId)!;
 const injected=await api(owner,`/online/matches/${id}/events`,{eventId:ids.next(),type:'PROGRESS',roundKey:r.key,mapGeneration:r.mapGeneration,levelId:r.map!.levelId,attemptId:attempts.get(owner.session.playerId),progress:95,achievementCheckpoint:{atRisk:true}});
 expect(injected.status).toBe(400);
 const restarted=new SurvivalEngineService(db,clock,ids,config,{select:async()=>{throw new Error('unexpected selection');}});
 new SurvivalOnlineService(db,clock,sessions,restarted,survival,config);
 now+=500;await touch(group);
 const command={type:'PROGRESS' as const,roundKey:r.key,mapGeneration:r.mapGeneration,levelId:r.map!.levelId,participantId:risk.participantId,attemptId:attempts.get(owner.session.playerId)!,progress:95};
 await Promise.all([engine.dispatch(id,'reconnected-progress',command,owner.session),restarted.dispatch(id,'reconnected-progress',command,owner.session)]);
 const evidence=(await db.query<{achievement_checkpoint:any}>('SELECT achievement_checkpoint FROM sur_rounds WHERE match_id=$1 AND round_key=$2',[id,r.key])).rows[0]!.achievement_checkpoint;
 expect(evidence.standings.find((p:any)=>p.participantId===risk.participantId)).toMatchObject({atRisk:true,rescuedAt:now});
 expect((await db.query('SELECT * FROM sur_match_commands WHERE match_id=$1 AND event_key=$2',[id,'reconnected-progress'])).rows).toHaveLength(1);
 const view=(await api(owner,`/online/matches/${id}`)).body;
 expect(JSON.stringify(view)).not.toMatch(/achievementCheckpoint|rescuedAt|atRisk/);
 expect(survivalCurrentRound(await restarted.get(id)).achievementCheckpoint).toEqual(evidence);
});


describe("64-player public matchmaking",()=>{
 async function adminFormat(count:16|64){
  const c=await client("debug");await db.query("UPDATE ranked_players SET gd_username='hwanhee1' WHERE id=$1",[c.session.playerId]);c.session={...c.session,gdUsername:"hwanhee1"};
  const admin=new AdminService(db,env,{} as RankedConfigService,clock,config);
  await admin.execute(c.session,{password:"fixture-password",scope:"SURVIVAL",action:"SET_SURVIVAL_MODE",matchPlayerCount:count});return c;
 }
 it("waits at 63 authenticated humans, then admits the 64th into one captured 40-cutoff match",async()=>{
  await adminFormat(64);const group:Client[]=[];for(let i=0;i<64;i++)group.push(await client());
  await Promise.all(group.slice(0,63).map(join));
  const waiting=(await api(group[0]!,"/queue/status")).body;
  expect(waiting).toMatchObject({status:"QUEUED",players:63,maximumPlayers:64});
  expect((await db.query("SELECT id FROM sur_matches")).rows).toHaveLength(0);
  await join(group[63]!);const state=(await api(group[0]!,"/queue/status")).body;
  expect(state.status).toBe("MATCHED");const match=await engine.get(state.matchId);
  expect(match.players).toHaveLength(64);expect(match.config.matchPlayerCount).toBe(64);expect(survivalCurrentRound(match).target).toBe(40);
  expect((await db.query("SELECT id FROM sur_matches")).rows).toHaveLength(1);
  expect((await api(group[0]!,`/online/matches/${match.id}`)).body).toMatchObject({matchPlayerCount:64,playerCount:64,targetQualifiers:40});
  const restart=new SurvivalEngineService(db,clock,ids,config,{select:async()=>{throw new Error("unexpected map");}});
  expect((await restart.get(match.id)).config.matchPlayerCount).toBe(64);
 },60000);
 it.each([false,true])("fills exactly 63 bots with force=%s and preserves an active 64-player snapshot after switching to 16",async forceDeathMatch=>{
  const owner=await adminFormat(64);await join(owner);
  const admission={password:"fixture-password",requestId:ids.next(),installedMods:mods,botCount:63,forceDeathMatch};
  const response=await api(owner,"/queue/fill-bots",admission);expect(response.status).toBe(201);
  const match=await engine.get(response.body.matchId);expect(match.players).toHaveLength(64);expect(match.players.filter(p=>p.kind==="BOT")).toHaveLength(63);expect(match.forceDeathMatch).toBe(forceDeathMatch);expect(survivalCurrentRound(match)).toMatchObject({phase:forceDeathMatch?"TIEBREAK":"ROUND_1",ordinal:1});
  const admin=new AdminService(db,env,{} as RankedConfigService,clock,config);
  await admin.execute(owner.session,{password:"fixture-password",scope:"SURVIVAL",action:"SET_SURVIVAL_MODE",matchPlayerCount:16});
  expect(config.getSnapshot().config.matchPlayerCount).toBe(16);expect((await engine.get(match.id)).config.matchPlayerCount).toBe(64);
  expect((await api(owner,"/queue/fill-bots",admission)).body.matchId).toBe(match.id);
  expect((await db.query("SELECT bot_count FROM sur_queue_fill_requests")).rows).toEqual([{bot_count:63}]);
  const newcomer=await client("debug");await join(newcomer);
  expect((await api(newcomer,"/queue/fill-bots",{...admission,requestId:ids.next()})).status).toBe(400);
 });
});


describe('Survival live spectator authenticated production HTTP',()=>{
 const route=(id:string)=>`/online/matches/${id}/spectator-live`;
 const query=(m:SurvivalMatch,target:string,extra='')=>{const r=survivalCurrentRound(m);return `?protocolVersion=2&worldHistory=1&targetId=${target}&roundKey=${r.key}&mapGeneration=${r.mapGeneration}&levelId=${r.map!.levelId}${extra}`;};
 function packet(m:SurvivalMatch,attemptId:string,sequence=0,elapsedMs=0,generation=1){const r=survivalCurrentRound(m);return {protocolVersion:2,matchId:m.id,roundNumber:r.ordinal,deathmatchSequence:null,attemptId,generation,sequence,elapsedMs,levelId:r.map!.levelId,inputEdges:[],keyframe:{x:100+sequence,y:40,rotation:0,mode:'CUBE',mini:false,upsideDown:false,flipped:false,dual:false,dead:false,finished:false,progress:20}};}
 const body=(m:SurvivalMatch,packets:unknown[])=>{const r=survivalCurrentRound(m);return {roundKey:r.key,mapGeneration:r.mapGeneration,levelId:r.map!.levelId,packets};};
 async function scoreState(id:string){return {match:(await engine.get(id)),attempts:(await db.query('SELECT * FROM sur_attempts WHERE match_id=$1 ORDER BY attempt_id',[id])).rows,
  profiles:(await db.query('SELECT player_id,lp FROM sur_profiles ORDER BY player_id')).rows,ap:(await db.query('SELECT player_id,total_ap FROM ranked_profiles ORDER BY player_id')).rows};}
 async function fixture(count=16){const {group,id}=await game(count);const m=await prepare(id,group);const viewer=group[0]!,target=group[1]!;
  await record(viewer,m,100);return {group,id,m,viewer,target};}
 it('requires a current official clear ACK, refuses active Attempts and eliminated users without granting a timed unlock',async()=>{
  const {group,id,m,viewer,target}=await fixture();
  expect((await fetch(base+'/api/ranked/survival'+route(id)+'/targets')).status).toBe(401);
  expect((await api(target,route(id)+'/targets')).status).toBe(403);
  const stranger=await client();expect((await api(stranger,route(id)+'/targets')).status).toBe(403);
  const targets=await api(viewer,route(id)+'/targets');expect(targets.status).toBe(200);
  expect(targets.body.targets).toHaveLength(15);expect(targets.body.targets.some((t:any)=>t.participantId===viewer.session.playerId)).toBe(false);
  expect((await api(viewer,route(id)+query(m,viewer.session.playerId))).status).toBe(403);
  const attempt=ids.next();await event(target,m,'ATTEMPT_START',{attemptId:attempt});await event(target,m,'PROGRESS',{attemptId:attempt,progress:100});
  expect((await api(target,route(id)+'/targets')).status).toBe(403);
  await event(target,m,'ATTEMPT_END',{attemptId:attempt,progress:100});expect((await api(target,route(id)+'/targets')).status).toBe(200);
  await event(viewer,m,'FORFEIT');now+=6000;await touch(group);expect((await api(viewer,route(id)+'/targets')).status).toBe(403);
 });
 it('never accepts a locally invented Attempt, publishes only on demand and never mutates scores/AP through visuals',async()=>{
  const {group,id,m,viewer,target}=await fixture();now+=5000;await touch(group);
  const attempt=ids.next(),p=packet(m,attempt);
  expect((await api(target,route(id)+'/publish',body(m,[p]))).status).toBe(409);
  expect((await api(viewer,route(id)+query(m,target.session.playerId))).body.waiting).toBe(true);
  expect((await api(target,route(id)+'/publish',body(m,[p]))).status).toBe(409);
  const ack=await event(target,m,'ATTEMPT_START',{attemptId:attempt});expect(ack.myActiveAttemptId).toBe(attempt);expect(ack.spectatorDemand).toBe(true);
  const before=await scoreState(id);expect((await api(target,route(id)+'/publish',body(m,[p]))).status).toBe(201);
  const view=await api(viewer,route(id)+query(m,target.session.playerId));expect(view.status).toBe(200);expect(view.body.packets).toHaveLength(1);
  expect(view.body.restoration).toMatchObject({available:true,startSequence:0,levelId:p.levelId});
  expect(await scoreState(id)).toEqual(before);
  const duplicate=await api(target,route(id)+'/publish',body(m,[p]));expect(duplicate.status).toBe(201);
  const changed={...p,keyframe:{...p.keyframe,x:900}};expect((await api(target,route(id)+'/publish',body(m,[changed]))).status).toBe(409);
  const bad=packet(m,attempt,1,50);expect((await api(target,route(id)+'/publish',body(m,[{...bad,levelId:1}]))).status).toBe(409);
  expect((await api(target,route(id)+'/publish',body(m,[{...bad,keyframe:{...bad.keyframe,dual:true}}]))).status).toBe(400);
  expect((await api(target,route(id)+'/publish',body(m,[bad]))).status).toBe(201);
  expect(await scoreState(id)).toEqual(before);
 });
 it('shares one stream between viewers, switches targets, respects terminal/next Attempt and expires demand without a DB timer',async()=>{
  const {group,id,m,viewer,target}=await fixture();const other=group[2]!;await record(other,m,100);now+=5000;await touch(group);
  const a=ids.next();await event(target,m,'ATTEMPT_START',{attemptId:a});
  for(const c of [viewer,other])expect((await api(c,route(id)+query(m,target.session.playerId))).status).toBe(200);
  expect((await api(target,route(id)+'/publish',body(m,[packet(m,a)]))).status).toBe(201);expect(spectator.diagnostics().streams).toBe(1);
  const death=packet(m,a,1,50);death.keyframe.dead=true;
  await event(target,m,'ATTEMPT_END',{attemptId:a,progress:20});expect((await api(target,route(id)+'/publish',body(m,[death]))).status).toBe(201);
  expect((await api(viewer,route(id)+query(m,target.session.playerId))).body.packets.at(-1).keyframe.dead).toBe(true);
  const b=ids.next();await event(target,m,'ATTEMPT_START',{attemptId:b});
  expect((await api(viewer,route(id)+query(m,target.session.playerId))).body.waiting).toBe(true);
  expect((await api(target,route(id)+'/publish',body(m,[packet(m,b)]))).status).toBe(201);
  expect((await api(target,route(id)+'/publish',body(m,[packet(m,a,0,0,2)]))).status).toBe(409);
  expect((await api(viewer,route(id)+query(m,target.session.playerId,`&attemptId=${a}&generation=1&afterSequence=1`))).body).toMatchObject({reset:true,stream:{attemptId:b}});
  const clear=packet(m,b,1,100);clear.keyframe.finished=true;clear.keyframe.progress=100;
  await event(target,m,'ATTEMPT_END',{attemptId:b,progress:100});expect((await api(target,route(id)+'/publish',body(m,[clear]))).status).toBe(201);
  expect((await api(viewer,route(id)+query(m,target.session.playerId))).body.packets.at(-1).keyframe.finished).toBe(true);
  expect((await api(viewer,route(id)+'/targets')).body.targets.find((t:any)=>t.participantId===target.session.playerId).status).toBe('CLEARED');
  expect((await api(viewer,route(id)+query(m,group[3]!.session.playerId))).status).toBe(200);
  await api(other,route(id)+'/leave',{});expect(spectator.diagnostics().streams).toBe(0);
  now+=8001;expect(spectator.diagnostics()).toMatchObject({streams:0,viewers:0});
 });
 it('rejects revoked sessions, retired contexts, target elimination and ended rounds; a restarted relay waits for a fresh generation',async()=>{
  const {group,id,m,viewer,target}=await fixture();now+=5000;await touch(group);
  await api(viewer,route(id)+query(m,target.session.playerId));const a=ids.next();await event(target,m,'ATTEMPT_START',{attemptId:a});
  await api(target,route(id)+'/publish',body(m,[packet(m,a)]));
  const restarted=new SurvivalSpectatorLiveService(db,clock);
  expect((await restarted.subscribe(id,viewer.session,Object.fromEntries(new URLSearchParams(query(m,target.session.playerId).slice(1))))).waiting).toBe(true);
  await expect(restarted.publish(id,target.session,body(m,[packet(m,a,1,50)]))).rejects.toThrow('SPECTATOR_STREAM_MUST_START_AT_SEQUENCE_ZERO');
  await expect(restarted.publish(id,target.session,body(m,[packet(m,a,0,100,2)]))).resolves.toMatchObject({accepted:true,generation:2});
  expect((await api(viewer,route(id)+query(m,target.session.playerId).replace('mapGeneration=1','mapGeneration=999'))).status).toBe(409);
  await event(target,m,'FORFEIT');expect((await api(viewer,route(id)+query(m,target.session.playerId))).status).toBe(403);
  expect((await api(target,route(id)+'/publish',body(m,[packet(m,a,1,50)]))).status).toBe(403);
  await db.query('UPDATE ranked_sessions SET revoked_at=$2 WHERE id=$1',[viewer.session.sessionId,new Date(now).toISOString()]);
  expect((await api(viewer,route(id)+'/targets')).status).toBe(401);
  now=survivalCurrentRound(m).endsAt!;await touch(group.slice(1));await online.tickOnce();
  await expect(restarted.subscribe(id,group[2]!.session,Object.fromEntries(new URLSearchParams(query(m,group[3]!.session.playerId).slice(1))))).rejects.toThrow();
 });
 it('keeps simulated bots unavailable and 64-player multiple-subscriber state within the same finite relay budget',async()=>{
  const adminClient=await client('debug');await db.query("UPDATE ranked_players SET gd_username='hwanhee1' WHERE id=$1",[adminClient.session.playerId]);
  const admin=new AdminService(db,env,{} as RankedConfigService,clock,config);await admin.execute({...adminClient.session,gdUsername:'hwanhee1'},{password:'fixture-password',scope:'SURVIVAL',action:'SET_SURVIVAL_MODE',matchPlayerCount:64});
  const {group,id}=await game(3,true);const m=await prepare(id,group);await record(group[0]!,m,100);now+=5000;await touch(group);
  const targets=await api(group[0]!,route(id)+'/targets');expect(targets.body.targets).toHaveLength(63);
  expect(targets.body.targets.filter((t:any)=>t.kind==='BOT').every((t:any)=>t.selectable===false&&t.status==='NO_NATIVE_STREAM')).toBe(true);
  const bot=targets.body.targets.find((t:any)=>t.kind==='BOT');expect((await api(group[0]!,route(id)+query(m,bot.participantId))).status).toBe(403);
  const target=group[1]!,a=ids.next();await event(target,m,'ATTEMPT_START',{attemptId:a});await api(group[0]!,route(id)+query(m,target.session.playerId));
  const before=await scoreState(id),began=performance.now();
  for(let i=0;i<10;i++){now+=1000;await api(group[0]!,route(id)+query(m,target.session.playerId));const batch=Array.from({length:16},(_,j)=>packet(m,a,i*16+j,(i*16+j)*50));expect((await api(target,route(id)+'/publish',body(m,batch))).status).toBe(201);}
  const d=spectator.diagnostics();expect(d.streams).toBe(1);expect(d.serializedBytes).toBeLessThan(d.limits.totalBytes);expect(await scoreState(id)).toEqual(before);
  console.info('Survival spectator 64-player isolated HTTP, 160 native-format frames, elapsedMs=',performance.now()-began,'serializedBytes=',d.serializedBytes);
 },60000);
});

it('processes official Survival progress concurrently with shared viewer reads and native pose uploads without spectator scoring writes',async()=>{
 const {group,id}=await game(16),m=await prepare(id,group),viewer=group[0]!,target=group[1]!,peer=group[2]!;
 await record(viewer,m,100);now+=5000;await touch(group);
 const r=survivalCurrentRound(m),targetAttempt=ids.next(),peerAttempt=ids.next();
 await event(target,m,'ATTEMPT_START',{attemptId:targetAttempt});await event(peer,m,'ATTEMPT_START',{attemptId:peerAttempt});
 const route=`/online/matches/${id}/spectator-live`,query=`?protocolVersion=2&worldHistory=1&targetId=${target.session.playerId}&roundKey=${r.key}&mapGeneration=${r.mapGeneration}&levelId=${r.map!.levelId}`;
 await api(viewer,route+query);
 const latencies:number[]=[],began=performance.now();
 for(let i=0;i<8;++i){now+=500;await touch(group);const start=performance.now();
  const packets=Array.from({length:10},(_,j)=>({protocolVersion:2,matchId:id,roundNumber:r.ordinal,levelId:r.map!.levelId,attemptId:targetAttempt,generation:1,sequence:i*10+j,elapsedMs:(i*10+j)*50,inputEdges:[],keyframe:{x:100+i*10+j,y:40,rotation:0,mode:'CUBE',mini:false,upsideDown:false,flipped:false,dual:false,dead:false,finished:false}}));
  const [upload,view]=await Promise.all([api(target,route+'/publish',{roundKey:r.key,mapGeneration:r.mapGeneration,levelId:r.map!.levelId,packets}),api(viewer,route+query),event(peer,m,'PROGRESS',{attemptId:peerAttempt,progress:10+i,currentProgress:10+i})]);
  expect(upload.status).toBe(201);expect(view.status).toBe(200);latencies.push(performance.now()-start);
 }
 const state=await engine.get(id),round=survivalCurrentRound(state);
 expect(round.players.find(p=>p.participantId===viewer.session.playerId)?.attempts).toHaveLength(1);
 expect(round.players.find(p=>p.participantId===target.session.playerId)?.attempts).toHaveLength(1);
 expect(round.players.find(p=>p.participantId===peer.session.playerId)?.highestProgress).toBe(17);
 expect(round.startsAt).toBe(r.startsAt);expect(round.endsAt).toBe(r.endsAt);
 if(process.env['CORUM_SPECTATOR_LOAD_REPORT']){
  latencies.sort((a,b)=>a-b);await (await import('node:fs/promises')).writeFile(process.env['CORUM_SPECTATOR_LOAD_REPORT'],JSON.stringify({environment:pool?'isolated PostgreSQL':'isolated PGlite',players:16,frames:80,concurrentBatches:8,totalMs:performance.now()-began,p50BatchMs:latencies[4],p95BatchMs:latencies[7],...spectator.diagnostics()},null,2));
 }
});

describe('own Survival replay official receipt over authenticated production HTTP',()=>{
 it('exposes only the owner Attempt, includes forced timeout and map identity, and never changes scores',async()=>{
  const {group,id}=await game(16),m=await prepare(id,group),owner=group[0]!,other=group[1]!,attempt=ids.next();
  await event(owner,m,'ATTEMPT_START',{attemptId:attempt});await event(owner,m,'PROGRESS',{attemptId:attempt,progress:74.999,currentProgress:74.999});
  const path=`/replays/matches/${id}/attempts/${attempt}`;
  expect((await api(owner,path)).body).toMatchObject({accepted:true,ended:false,progress:74.999,participantId:owner.session.playerId});
  expect((await api(other,path)).body.accepted).toBe(false);
  now=survivalCurrentRound(await engine.get(id)).endsAt!+1;await touch(group);await online.tickOnce();
  const before=await engine.get(id),receipt=await api(owner,path);
  expect(receipt.status).toBe(200);expect(receipt.body).toMatchObject({accepted:true,ended:true,progress:74.999,score:74,roundBestScore:74,
   mapGeneration:survivalCurrentRound(m).mapGeneration,geometryHash:survivalCurrentRound(m).map!.geometrySha256});
  expect(await engine.get(id)).toEqual(before);
  const stranger=await client();expect((await api(stranger,path)).status).toBe(403);
 });
});


describe("authenticated audio recovery and delayed entry",()=>{
 it("requires independent stable accounts, rejects stale generations and changes only a pre-play map",async()=>{
  const {group,id}=await game(8);now+=6000;await touch(group);await online.tickOnce();await engine.selectMap(id);let m=await engine.get(id);
  const original=survivalCurrentRound(m),level=original.map!.levelId;
  const failure={category:"AUDIO_UNAVAILABLE_PERMANENT",resourceKind:"SONG",resourceId:123,nativeCode:2};
  await event(group[0]!,m,"PREPARATION_FAILED",{reason:failure.category,audioFailure:failure});
  await event(group[0]!,m,"PREPARATION_FAILED",{reason:failure.category,audioFailure:failure});
  expect((await engine.get(id)).state).toBe("PREPARING");
  expect(Number((await db.query<{n:string}>("SELECT count(*)::text AS n FROM sur_audio_failure_reports WHERE match_id=$1",[id])).rows[0]!.n)).toBe(1);
  expect((await db.query("SELECT * FROM sur_resource_quarantine WHERE resource_id=$1",[level])).rows).toHaveLength(0);
  const stale=await api(group[1]!,`/online/matches/${id}/events`,{eventId:ids.next(),type:"PREPARATION_FAILED",roundKey:original.key,mapGeneration:99,levelId:level,reason:failure.category,audioFailure:failure});expect(stale.status).toBe(409);
  await event(group[1]!,m,"PREPARATION_FAILED",{reason:failure.category,audioFailure:failure});
  m=await engine.get(id);expect(m.state).toBe("MAP_SELECTION");expect(survivalCurrentRound(m).mapGeneration).toBe(original.mapGeneration+1);expect(survivalCurrentRound(m).map).toBeNull();
  const quarantined=(await db.query<{resource_kind:string}>("SELECT resource_kind FROM sur_resource_quarantine WHERE resource_id IN ($1,123)",[level])).rows.map(r=>r.resource_kind).sort();expect(quarantined).toEqual(["MAP","SONG"]);
  expect((await db.query("SELECT * FROM sur_lp_ledger WHERE player_id=ANY($1::uuid[])",[group.map(c=>c.session.playerId)])).rows).toHaveLength(0);
  expect((await db.query("SELECT * FROM sur_attempts WHERE match_id=$1",[id])).rows).toHaveLength(0);
  await engine.selectMap(id);const next=await engine.get(id);expect(survivalCurrentRound(next).map!.levelId).not.toBe(level);
  const oldReady=await api(group[0]!,`/online/matches/${id}/events`,{eventId:ids.next(),type:"ASSETS_READY",roundKey:original.key,mapGeneration:original.mapGeneration,levelId:level,certificate:original.map,installedMods:mods});expect(oldReady.status).toBe(409);
 });
 it("does not let transient failures or a lone reporter quarantine any map, and forbids reports after play begins",async()=>{
  const {group,id}=await game(8);now+=6000;await touch(group);await online.tickOnce();await engine.selectMap(id);let m=await engine.get(id);
  for(const c of group.slice(0,2))await event(c,m,"PREPARATION_FAILED",{reason:"AUDIO_DOWNLOAD_TRANSIENT",audioFailure:{category:"AUDIO_DOWNLOAD_TRANSIENT",resourceKind:"SONG",resourceId:123,nativeCode:1}});
  expect((await engine.get(id)).state).toBe("PREPARING");expect((await db.query("SELECT * FROM sur_resource_quarantine WHERE resource_id=123")).rows).toHaveLength(0);
  for(const c of group)await event(c,m,"ASSETS_READY",{certificate:survivalCurrentRound(m).map});m=await engine.get(id);
  now=survivalCurrentRound(m).entryRequestedAt!+5000;await touch(group);for(const c of group)await event(c,m,"ENTERED");m=await engine.get(id);now=survivalCurrentRound(m).startsAt!;await touch(group);m=await engine.tick(id);
  const r=survivalCurrentRound(m),response=await api(group[0]!,`/online/matches/${id}/events`,{eventId:ids.next(),type:"PREPARATION_FAILED",roundKey:r.key,mapGeneration:r.mapGeneration,levelId:r.map!.levelId,reason:"AUDIO_UNAVAILABLE_PERMANENT",audioFailure:{category:"AUDIO_UNAVAILABLE_PERMANENT",resourceKind:"SONG",resourceId:123,nativeCode:2}});
  expect(response.status).toBe(409);expect((await engine.get(id)).state).toBe("PLAYING");expect((await engine.get(id)).rounds[0]!.endsAt).toBe(r.endsAt);
 });
});

describe("authenticated bot-fill Force Death Match HTTP contract",()=>{
 it.each([false,true])("persists force=%s, defaults off, and rejects conflicting request identity",async force=>{
  const c=await client("debug");await join(c);const admission={password:"fixture-password",requestId:ids.next(),installedMods:mods,botCount:7,...(force?{forceDeathMatch:true}:{})};
  const start=await api(c,"/queue/fill-bots",admission);expect(start.status).toBe(201);const m=await engine.get(start.body.matchId);
  expect(m.forceDeathMatch).toBe(force);expect(survivalCurrentRound(m)).toMatchObject({phase:force?"TIEBREAK":"ROUND_2",ordinal:1});
  expect((await api(c,"/queue/fill-bots",admission)).body.matchId).toBe(m.id);
  expect((await api(c,"/queue/fill-bots",{...admission,forceDeathMatch:!force})).status).toBe(409);
  expect((await api(c,"/queue/fill-bots",{...admission,requestId:ids.next(),forceDeathMatch:!force})).status).toBe(409);
 });
 it("rejects Force Death Match on normal queue admission and on unauthenticated debug routes",async()=>{
  const c=await client("release");const joinResponse=await api(c,"/queue/join",{installedMods:mods,forceDeathMatch:true});expect(joinResponse.status).toBe(400);
  await join(c);expect((await api(c,"/queue/fill-bots",{password:"fixture-password",requestId:ids.next(),installedMods:mods,botCount:7,forceDeathMatch:true})).status).toBe(403);
  const d=await client("debug");await join(d);expect((await api(d,"/queue/fill-bots",{password:"wrong",requestId:ids.next(),installedMods:mods,botCount:7,forceDeathMatch:true})).status).toBe(403);
  expect((await db.query("SELECT id FROM sur_matches")).rows).toHaveLength(0);
 });
});
