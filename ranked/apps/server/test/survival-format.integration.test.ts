import "reflect-metadata";
import {readFile,readdir} from "node:fs/promises";
import {PGlite} from "@electric-sql/pglite";
import {Test} from "@nestjs/testing";
import {ValidationPipe,type INestApplication} from "@nestjs/common";
import {beforeAll,beforeEach,afterAll,describe,it,expect} from "vitest";
import {DEFAULT_SURVIVAL_CONFIG} from "@corum-ranked/rules";
import {PgliteDatabase} from "./pglite-database.js";
import {countSql,failSqlOnce} from "./sql-counter.js";
import {configDocumentFixture,environmentFixture} from "./fixtures.js";
import {AdminController} from "../src/admin/admin.module.js";
import {AdminService} from "../src/admin/admin.service.js";
import {AdminActionDto} from "../src/admin/admin.dto.js";
import {RankedConfigService} from "../src/config/ranked-config.service.js";
import {SurvivalConfigService,type SurvivalConfigDocument} from "../src/config/survival-config.service.js";
import {SurvivalEngineService} from "../src/survival/survival-engine.service.js";
import {SessionGuard} from "../src/session/session.guard.js";
import {TokenService} from "../src/common/token.service.js";
import {DATABASE} from "../src/database/database.port.js";
import {SERVER_CLOCK} from "../src/common/runtime.module.js";
import {REQUIRED_RANKED_CLIENT_VERSION} from "../src/session/client-version.js";
import type {RankedSessionContext} from "../src/session/session.types.js";

const id=(n:number)=>`00000000-0000-4000-8000-${String(n).padStart(12,"0")}`;
const now=new Date("2026-10-24T12:00:00Z"),clock={now:()=>now};
const env={...environmentFixture(),debugBot:{password:"format-test-only"}} as ReturnType<typeof environmentFixture>;
const tokens=new TokenService(env);
const context:RankedSessionContext={sessionId:id(101),playerId:id(1),gdAccountId:"1",gdUsername:"hwanhee1",displayedTier:"RED",hiddenMmr:0,placementGames:0};
let pg:PGlite,db:PgliteDatabase,app:INestApplication,url:string,config:SurvivalConfigService,solo:RankedConfigService,admin:AdminService,document:SurvivalConfigDocument,sequence:number;
const source={fetchConfig:async()=>structuredClone(document)};
const migrations=new URL("../../../migrations/",import.meta.url);
const body=(count?:16|64)=>({password:"format-test-only",scope:"SURVIVAL" as const,action:count?"SET_SURVIVAL_MODE" as const:"GET_SURVIVAL_MODE" as const,...(count?{matchPlayerCount:count}:{})});
async function post(payload:unknown,token="format-1"){
 const response=await fetch(url+"/api/ranked/debug/admin",{method:"POST",headers:{authorization:`Bearer ${token}`,"content-type":"application/json"},body:JSON.stringify(payload)});
 return {status:response.status,body:await response.json() as any};
}
beforeAll(async()=>{
 pg=new PGlite();db=new PgliteDatabase(pg);
 for(const name of (await readdir(migrations)).filter(n=>n.endsWith(".sql")).sort())await pg.exec(await readFile(new URL(name,migrations),"utf8"));
 const module=await Test.createTestingModule({controllers:[AdminController],providers:[SessionGuard,
  {provide:AdminService,useValue:{execute:(session:RankedSessionContext,payload:AdminActionDto)=>admin.execute(session,payload)}},
  {provide:DATABASE,useValue:db},{provide:TokenService,useValue:tokens},{provide:SERVER_CLOCK,useValue:clock}]}).compile();
 Reflect.defineMetadata("design:paramtypes",[Object,AdminActionDto],AdminController.prototype,"execute");
 app=module.createNestApplication();app.useLogger(false);app.useGlobalPipes(new ValidationPipe({whitelist:true,forbidNonWhitelisted:true,transform:true}));
 await app.listen(0,"127.0.0.1");url=await app.getUrl();
},60000);
afterAll(async()=>{await app?.close();await pg.close();});
beforeEach(async()=>{
 await db.query("TRUNCATE ranked_players,sur_mode_state,sur_matches,ranked_config_snapshots CASCADE");sequence=1000;
 for(const n of [1,2]){
  await db.query("INSERT INTO ranked_players(id,gd_account_id,gd_username) VALUES($1,$2,$3)",[id(n),n,n===1?"hwanhee1":"Other"]);
  await db.query("INSERT INTO ranked_profiles(player_id) VALUES($1)",[id(n)]);
  await db.query("INSERT INTO ranked_sessions(id,player_id,token_hash,client_version,environment_snapshot,expires_at,last_heartbeat_at) VALUES($1,$2,$3,$4,'{}',$5,$6)",
   [id(100+n),id(n),tokens.hash(`format-${n}`),REQUIRED_RANKED_CLIENT_VERSION,new Date(now.getTime()+3600000),now]);
 }
 solo=new RankedConfigService({fetchConfig:async()=>configDocumentFixture()},env,db);await solo.refresh();
 document={generation:"format-1",generatedAt:now.toISOString(),config:{...DEFAULT_SURVIVAL_CONFIG,enabled:true}};
 config=new SurvivalConfigService(source,env,db,solo);expect(await config.refresh()).toBe(true);
 admin=new AdminService(db,env,solo,clock,config);
});

describe("authenticated persistent operating format",()=>{
 it("validates identity, password, scope and exact 16/64 choices before mutation",async()=>{
  expect((await post(body())).body.matchPlayerCount).toBe(16);
  expect((await post(body(64),"format-2")).status).toBe(403);
  expect((await post({...body(64),password:"wrong"})).status).toBe(403);
  for(const count of [8,32,65])expect((await post({...body(64),matchPlayerCount:count})).status).toBe(400);
  expect((await post({...body(64),matchPlayerCount:undefined})).status).toBe(400);
  expect((await post({...body(64),scope:"COMMON"})).status).toBe(400);
  expect(config.getSnapshot().config.matchPlayerCount).toBe(16);
 });
 it("persists 64 across restart/shared-tier refresh and preserves queued users until an actual OFF transition",async()=>{
  expect((await post(body(64))).status).toBe(201);
  expect(config.getSnapshot().config).toMatchObject({matchPlayerCount:64,round1Qualifiers:40,round2Qualifiers:26,round3Qualifiers:12,round4Qualifiers:5});
  const restart=new SurvivalConfigService(source,env,db,solo);expect(await restart.refresh()).toBe(true);
  expect(restart.getStatus()).toMatchObject({matchPlayerCount:64,generation:"format-1:64p"});expect(restart.getSnapshot().config.matchPlayerCount).toBe(64);
  await db.query("INSERT INTO sur_queue_entries(player_id,session_id,client_version,joined_at,wait_started_at,last_heartbeat_at,status) VALUES($1,$2,$3,$4,$4,$4,'QUEUED')",[id(2),id(102),REQUIRED_RANKED_CLIENT_VERSION,now]);
  document={...document,generation:"format-2",generatedAt:new Date(now.getTime()+1000).toISOString()};await restart.refresh();
  expect((await db.query("SELECT status FROM sur_queue_entries")).rows).toEqual([{status:"QUEUED"}]);
  document={...document,generation:"format-off",config:{...document.config,enabled:false}};await restart.refresh();
  expect(restart.getSnapshot().config).toMatchObject({matchPlayerCount:64,enabled:false});
  expect((await db.query("SELECT status FROM sur_queue_entries")).rows).toEqual([{status:"DISABLED"}]);
 });
 it("switches new admissions without mutating 16-player snapshots or their idempotent creation receipts",async()=>{
  const engine=new SurvivalEngineService(db,clock,{next:()=>id(++sequence)},config,{select:async()=>{throw new Error("not requested");}});
  const bots=(count:number)=>Array.from({length:count},(_,i)=>({participantId:`bot-${i}`,kind:"BOT" as const,name:`Bot ${i}`,lp:150}));
  const original=await engine.create("old-16",bots(16));expect(original.players).toHaveLength(16);
  await post(body(64));const enlarged=await engine.create("new-64",bots(64));expect(enlarged.players).toHaveLength(64);
  expect((await engine.create("old-16",bots(16))).id).toBe(original.id);
  expect((await engine.get(original.id)).config.matchPlayerCount).toBe(16);
  await post(body(16));expect((await engine.get(enlarged.id)).config.matchPlayerCount).toBe(64);
 });
 it("does no config SQL for unchanged external documents after selecting 64",async()=>{
  await post(body(64));const counter=countSql(db,()=>false);
  try{for(let i=1;i<=6;i++){document={...document,generatedAt:new Date(now.getTime()+60000*i).toISOString()};expect(await config.refresh()).toBe(true);expect(config.getSnapshot().config.matchPlayerCount).toBe(64);}
   expect(counter.metrics.queries).toBe(0);
  }finally{counter.stop();}
 });
 it("keeps memory and SQL at 16 after rollback, then reconciles a lost COMMIT acknowledgment on one refresh",async()=>{
  const failure=failSqlOnce(db,sql=>sql.startsWith("INSERT INTO sur_mode_state"));
  try{await expect(admin.execute(context,body(64))).rejects.toThrow("Injected");}finally{failure.stop();}
  expect(config.getSnapshot().config.matchPlayerCount).toBe(16);expect((await db.query("SELECT match_player_count FROM sur_mode_state")).rows).toEqual([{match_player_count:16}]);
  const original=db.transaction.bind(db);let failed=false;
  db.transaction=async operation=>{const value=await original(operation);if(!failed){failed=true;throw new Error("Lost COMMIT acknowledgment");}return value;};
  try{await expect(admin.execute(context,body(64))).rejects.toThrow("Lost COMMIT");}finally{db.transaction=original;}
  expect(config.getSnapshot().config.matchPlayerCount).toBe(16);expect(await config.refresh()).toBe(true);expect(config.getSnapshot().config.matchPlayerCount).toBe(64);
 });
 it("reapplies the additive migration without resetting achievements or its operating mode",async()=>{
  await post(body(64));await db.query("UPDATE ranked_profiles SET total_ap=77 WHERE player_id=$1",[id(1)]);
  const epoch=(await db.query("SELECT * FROM ranked_achievement_versions")).rows;
  await pg.exec(await readFile(new URL("0030_survival_64_player_format.sql",migrations),"utf8"));
  expect((await db.query("SELECT total_ap FROM ranked_profiles WHERE player_id=$1",[id(1)])).rows).toEqual([{total_ap:77}]);
  expect((await db.query("SELECT * FROM ranked_achievement_versions")).rows).toEqual(epoch);
  expect((await db.query("SELECT match_player_count FROM sur_mode_state")).rows).toEqual([{match_player_count:64}]);
 });
});
