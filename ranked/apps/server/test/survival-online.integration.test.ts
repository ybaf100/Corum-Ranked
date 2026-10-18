import "reflect-metadata";
import { readFile,readdir } from "node:fs/promises";
import { Pool } from "pg";
import type { DatabasePort,SqlExecutor } from "../src/database/database.port.js";
import { PGlite } from "@electric-sql/pglite";
import { Test } from "@nestjs/testing";
import { ValidationPipe,type INestApplication } from "@nestjs/common";
import { beforeAll,beforeEach,afterEach,afterAll,describe,it,expect,vi } from "vitest";
import { DEFAULT_SURVIVAL_CONFIG,survivalCurrentRound,survivalMapRange,type SurvivalMatch } from "@corum-ranked/rules";
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
 await db.query("TRUNCATE ranked_players,sur_matches,sur_mode_state CASCADE");now=Date.parse("2026-10-13T00:00:00Z");sequence=1000;mapId=40000000;clients.length=0;
 document={generation:"online-1",generatedAt:new Date(now).toISOString(),config:{...DEFAULT_SURVIVAL_CONFIG,enabled:true}};
 config=new SurvivalConfigService({fetchConfig:async()=>document},env,db);await config.refresh();
 soloDoc=configDocumentFixture();const solo={getSnapshot:()=>({...soloDoc,operational:{...soloDoc.operational,enabled:false}})} as RankedConfigService;
 engine=new SurvivalEngineService(db,clock,ids,config,{select:async(request)=>{const range=survivalMapRange(request.group,request.phase);
  return {levelId:mapId++,levelVersion:1,stars:range.minimumStars??10,demon:range.minimumStars===null,demonDifficulty:range.minimumStars===null?(range.demons[0]??null):null,
    automatic:false,platformer:false,geometrySha256:"a".repeat(64),geometryBytes:64,verifiedAt:new Date(now).toISOString()};}});
 vi.spyOn(engine,"ensureMapSelection").mockImplementation(()=>{});
 const achievements=new AchievementService(db,clock,ids);survival=new SurvivalService(db,clock,ids,config,achievements);
 sessions=new SessionService(db,{fetchCurrentTier:async()=>"NONE"},clock,ids,solo,tokens);
 online=new SurvivalOnlineService(db,clock,sessions,engine,survival,config);
 queue=new SurvivalQueueService(db,clock,{next:()=>42/0x100000000},env,config,sessions,engine,survival);
 const rates=new SurvivalRateService(db,clock);
 // Deterministic server clock tests explicitly tick production workers. This
 // leaves real HTTP handlers, auth, transactions, reducer and payment intact.
 vi.spyOn(online,"onApplicationBootstrap").mockImplementation(()=>{});vi.spyOn(queue,"onApplicationBootstrap").mockImplementation(()=>{});
 const module=await Test.createTestingModule({controllers:[SurvivalQueueController,SurvivalOnlineController],providers:[SessionGuard,AdmissionVersionGuard,
  {provide:DATABASE,useValue:db},{provide:SERVER_CLOCK,useValue:clock},{provide:TokenService,useValue:tokens},
  {provide:SurvivalQueueService,useValue:queue},{provide:SurvivalOnlineService,useValue:online},{provide:SurvivalRateService,useValue:rates}]}).compile();
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
async function event(c:Client,m:SurvivalMatch,type:string,fields:Record<string,unknown>={}){const r=survivalCurrentRound(m);const response=await api(c,`/online/matches/${m.id}/events`,{eventId:ids.next(),type,roundKey:r.key,mapGeneration:r.mapGeneration,levelId:r.map?.levelId??0,...(["ASSETS_READY","ENTERED","ATTEMPT_START"].includes(type)?{installedMods:mods}:{}),...fields});expect(response.status).toBe(201);return response.body;}
async function prepare(id:string,group:Client[]){let m=await engine.get(id);now+=5000;await touch(group);await online.tickOnce();await engine.selectMap(id);m=await engine.get(id);
 for(const c of group)await event(c,m,"ASSETS_READY",{certificate:survivalCurrentRound(m).map});m=await engine.get(id);
 now=survivalCurrentRound(m).entryRequestedAt!+5000;await touch(group);
 for(const c of group)await event(c,m,"ENTERED");m=await engine.get(id);now=survivalCurrentRound(m).startsAt!;await touch(group);await online.tickOnce();return engine.get(id);}
async function record(c:Client,m:SurvivalMatch,progress:number){const attemptId=ids.next();await event(c,m,"ATTEMPT_START",{attemptId});await event(c,m,"ATTEMPT_END",{attemptId,progress});}

describe("Survival M5 public HTTP and persistent online game",()=>{
 it("holds the ready room for exactly 5s, accepts surrender during the wait and preserves its deadline",async()=>{
  const {group,id}=await game(8),owner=group[0]!,peers=group.slice(1);
  now+=5000;await touch(group);await online.tickOnce();await engine.selectMap(id);let m=await engine.get(id);
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
  expect(response.body.roundPlayers.find((p:any)=>p.participantId===owner.session.playerId)).toMatchObject({bestProgress:87,currentProgress:40,attemptActive:true,cleared:false});
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
   const {id}=await game(n);expect(survivalCurrentRound(await engine.get(id)).phase).toBe(n<=3?"FINAL":"ROUND_3");
 });
 it("restarts one player's waiting cycle instead of creating a one-person game",async()=>{
   const c=await client();await join(c);now+=180000;await touch();await queue.tickOnce();const s=await api(c,"/queue/status");
   expect(s.body).toMatchObject({status:"QUEUED",players:1,waitingSeconds:0,remainingSeconds:180});expect((await db.query("SELECT id FROM sur_matches")).rows).toHaveLength(0);
 });
 it.each([1,3,8,15])("fills %i actual participants to 16; release peers share the normal engine and all receive LP",async n=>{
   const {group,id}=await game(n,true);const m=await engine.get(id);expect(m.players.filter(p=>p.kind==="BOT")).toHaveLength(16-n);
   expect((await db.query("SELECT player_id FROM sur_profiles")).rows).toHaveLength(n);
   for(let stage=0;stage<4;stage++){
     const live=await prepare(id,group);for(const c of group)await record(c,live,0);
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
   const history=await survival.history(group[0]!.session.playerId);expect(history.matches[0]?.result).toMatchObject({rank:8,requestedDelta:-55});
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
   now+=5000;await touch(peers);await online.tickOnce();await engine.selectMap(id);let m=await engine.get(id);
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
     now+=5000;await online.tickOnce();await engine.selectMap(id);let m=await engine.get(id);
     now=survivalCurrentRound(m).preparationStartedAt!+15000;await online.tickOnce();m=await engine.get(id);
     expect(m.state).toBe("COUNTDOWN");expect(survivalCurrentRound(m).players.every(p=>p.assetsReadyAt===null&&p.enteredAt===null)).toBe(true);
     now=survivalCurrentRound(m).endsAt!;await online.tickOnce();m=await engine.get(id);
     expect(m.state).toBe("ROUND_RESULT");expect(m.cancellationReason).toBeNull();expect(m.winnerId).toBeNull();
   }
   expect((await db.query("SELECT * FROM sur_lp_ledger")).rows).toHaveLength(0);
   now+=5000;await online.tickOnce();await engine.selectMap(id);let m=await engine.get(id);expect(survivalCurrentRound(m).phase).toBe("TIEBREAK");
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
 it("waits for three DM attempts, rounds only DM to .001%, repeats exact ties and pays a unique winner once",async()=>{
   const {group,id}=await game(2);let m=await prepare(id,group);
   for(const c of group)await record(c,m,100);now=survivalCurrentRound(m).endsAt!;await touch(group);await online.tickOnce();
   m=await prepare(id,group);expect(survivalCurrentRound(m).phase).toBe("TIEBREAK");
   for(const c of group)for(const score of [20,30,94.3704])await record(c,m,score);
   m=await engine.get(id);expect(m.state).toBe("ROUND_RESULT");expect((await db.query("SELECT * FROM sur_lp_ledger")).rows).toHaveLength(0);
   m=await prepare(id,group);for(const score of [20,30,94.3704])await record(group[0]!,m,score);
   expect((await engine.get(id)).state).toBe("PLAYING");
   for(const score of [20,30,94.3714])await record(group[1]!,m,score);
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
   const {group,id}=await game(8);now+=5000;await touch();await online.tickOnce();await engine.selectMap(id);let m=await engine.get(id);
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
   for(const c of remaining)await event(c,m,"FORFEIT");m=await engine.get(id);expect(m.state).toBe("FINAL_RESULT");
   expect(m.winnerIds?.every(id=>m.players.find(p=>p.participantId===id)?.kind==="BOT")).toBe(true);
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
   const achievements=new AchievementService(db,clock,ids);const claims=await Promise.all([achievements.claim(a.session.playerId,"gold_reached"),achievements.claim(a.session.playerId,"gold_reached")]);
   expect(claims.reduce((sum,claim)=>sum+claim.claimedAmount,0)).toBe(50);
 });
 it("survival Admin reset clears completed assignments/Fill receipts without deleting Solo or common data",async()=>{
   const {group,id}=await game(1,true),c=group[0]!;let m=await engine.get(id);await event(c,m,"FORFEIT");m=await engine.get(id);expect(m.state).toBe("FINAL_RESULT");
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
   const {group,id}=await game(8),old=group[0]!,peers=group.slice(1);now+=5000;await touch();await online.tickOnce();await engine.selectMap(id);let m=await engine.get(id);
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
  expect(survivalCurrentRound(m).phase).toBe(humans+bots<=3?"FINAL":humans+bots<8?"ROUND_3":"ROUND_1");
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
