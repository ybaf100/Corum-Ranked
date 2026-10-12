import "reflect-metadata";
import { readdir,readFile } from "node:fs/promises";
import { PGlite } from "@electric-sql/pglite";
import { Test } from "@nestjs/testing";
import { ForbiddenException, ValidationPipe, type INestApplication } from "@nestjs/common";
import { beforeAll,beforeEach,afterAll,describe,it,expect,vi } from "vitest";
import { DEFAULT_SURVIVAL_CONFIG,survivalBotPlan,survivalCurrentRound,survivalMapRange,type SurvivalMatch } from "@corum-ranked/rules";
import { PgliteDatabase } from "./pglite-database.js";
import { environmentFixture,configDocumentFixture } from "./fixtures.js";
import { SurvivalConfigService,type SurvivalConfigDocument } from "../src/config/survival-config.service.js";
import { SurvivalEngineService } from "../src/survival/survival-engine.service.js";
import { SurvivalService } from "../src/survival/survival.service.js";
import { SurvivalDebugService } from "../src/survival/survival-debug.service.js";
import { SurvivalDebugController } from "../src/survival/survival-debug.controller.js";
import { CreateSurvivalDebugDto,ForceSurvivalDebugDto,SurvivalDebugEventDto } from "../src/survival/survival-debug.dto.js";
import { AchievementService } from "../src/achievement/achievement.service.js";
import { SessionService } from "../src/session/session.service.js";
import { SessionGuard } from "../src/session/session.guard.js";
import { AdmissionVersionGuard } from "../src/session/admission-version.guard.js";
import type { RankedSessionContext } from "../src/session/session.types.js";
import { REQUIRED_RANKED_CLIENT_VERSION } from "../src/session/client-version.js";
import { TokenService } from "../src/common/token.service.js";
import { SERVER_CLOCK } from "../src/common/runtime.module.js";
import { DATABASE } from "../src/database/database.port.js";
import type { RankedConfigService } from "../src/config/ranked-config.service.js";

const id=(n:number)=>`00000000-0000-4000-8000-${String(n).padStart(12,"0")}`;
let pg:PGlite,db:PgliteDatabase,engine:SurvivalEngineService,debug:SurvivalDebugService,survival:SurvivalService;
let sessions:SessionService,achievements:AchievementService;
let now:number,sequence:number,mapId:number,document:SurvivalConfigDocument,config:SurvivalConfigService;
const clock={now:()=>new Date(now)},ids={next:()=>id(++sequence)};
const env={...environmentFixture(),debugBot:{password:"fixture-password"}} as ReturnType<typeof environmentFixture>;
const tokens=new TokenService(env);
const session:RankedSessionContext={sessionId:id(2),playerId:id(1),gdAccountId:"100",gdUsername:"Human",displayedTier:"SILVER",
  hiddenMmr:1234,placementGames:5,clientVersion:REQUIRED_RANKED_CLIENT_VERSION,clientBuildChannel:"debug"};
const solo={getSnapshot:()=>({...configDocumentFixture(),operational:{...configDocumentFixture().operational,enabled:false}})} as RankedConfigService;
const mods=[
  {id:"hwanhee1.corum_ranked",version:REQUIRED_RANKED_CLIENT_VERSION,enabled:true,loaded:true,internal:false,system:false},
  {id:"geode.node-ids",version:"v1.23.3",enabled:true,loaded:true,internal:false,system:false},
  {id:"syzzi.click_between_frames",version:"v1.0.0",enabled:true,loaded:true,internal:false,system:false,settings:{}}];
const body=(forceRank?:number,averageGroup:CreateSurvivalDebugDto["averageGroup"]="RED_BRONZE"):CreateSurvivalDebugDto=>
  ({password:"fixture-password",requestId:ids.next(),averageGroup,installedMods:mods,...(forceRank?{forceRank}:{})});
beforeAll(async()=>{pg=new PGlite();db=new PgliteDatabase(pg);
  const dir=new URL("../../../migrations/",import.meta.url);
  for(const f of (await readdir(dir)).filter(f=>f.endsWith(".sql")).sort())await pg.exec(await readFile(new URL(f,dir),"utf8"));
},60000);
afterAll(async()=>{await pg.close();});
beforeEach(async()=>{
  await pg.exec("TRUNCATE ranked_players,sur_matches CASCADE");now=Date.parse("2026-10-11T12:00:00Z");sequence=100;mapId=40000000;
  document={generation:"m4",generatedAt:clock.now().toISOString(),config:{...DEFAULT_SURVIVAL_CONFIG,enabled:true}};
  config=new SurvivalConfigService({fetchConfig:async()=>structuredClone(document)},env);await config.refresh();
  engine=new SurvivalEngineService(db,clock,ids,config,{select:async request=>{
    const range=survivalMapRange(request.group,request.phase);
    return {levelId:mapId++,levelVersion:1,stars:range.minimumStars??10,demon:range.minimumStars===null,
      demonDifficulty:range.minimumStars===null?range.demons[0]!:null,automatic:false,platformer:false,geometrySha256:"a".repeat(64),geometryBytes:64,verifiedAt:clock.now().toISOString()};
  }});
  achievements=new AchievementService(db,clock,ids);
  survival=new SurvivalService(db,clock,ids,config,achievements);
  sessions=new SessionService(db,{fetchCurrentTier:async()=>"NONE"},clock,ids,solo,tokens);
  // CBF settings are tested by the existing environment integration suite.
  vi.spyOn(sessions,"assertEnvironment").mockImplementation(()=>{});
  debug=new SurvivalDebugService(db,env,clock,{next:()=>42/0x100000000},config,sessions,engine,survival);
  await db.query("INSERT INTO ranked_players(id,gd_account_id,gd_username) VALUES($1,100,'Human')",[session.playerId]);
  await db.query(`INSERT INTO ranked_profiles(player_id,hidden_mmr,visible_ranked_score,displayed_tier,placement_games,initial_csmp_tier,initial_seed_mmr,seed_applied_at)
    VALUES($1,1234,1100,'SILVER',5,'SILVER',1234,$2)`,[session.playerId,clock.now().toISOString()]);
  await db.query(`INSERT INTO ranked_sessions(id,player_id,token_hash,client_version,environment_snapshot,expires_at,last_heartbeat_at)
    VALUES($1,$2,$3,$4,'{"clientBuildChannel":"debug"}',$5,$6)`,[session.sessionId,session.playerId,tokens.hash("fixture-token"),REQUIRED_RANKED_CLIENT_VERSION,new Date(now+86400000),clock.now()]);
});
async function create(){return engine.get((await debug.create(session,body())).matchId);}
async function send(m:SurvivalMatch,type:SurvivalDebugEventDto["type"],fields:Partial<SurvivalDebugEventDto>={}) {
  await db.query("UPDATE ranked_sessions SET last_heartbeat_at=$2 WHERE id=$1",[session.sessionId,clock.now().toISOString()]);
  const r=survivalCurrentRound(m);
  return debug.event(m.id,session,{eventId:ids.next(),type,roundKey:r.key,mapGeneration:r.mapGeneration,levelId:r.map?.levelId??0,installedMods:mods,...fields});
}
async function prepare(m:SurvivalMatch) {
  await engine.selectMap(m.id);m=await engine.get(m.id);
  await send(m,"ASSETS_READY",{certificate:survivalCurrentRound(m).map!});m=await engine.get(m.id);
  expect(m.state).toBe("ENTERING");
  await send(m,"ENTERED");m=await engine.get(m.id);expect(m.state).toBe("COUNTDOWN");
  now=survivalCurrentRound(m).startsAt!;await debug.tickOnce();return engine.get(m.id);
}
async function record(m:SurvivalMatch,progress:number) {
  const attemptId=ids.next();await send(m,"ATTEMPT_START",{attemptId});return send(m,"ATTEMPT_END",{attemptId,progress});
}
describe("Survival M4 persistent debug game",()=>{
  it("admits one human + 15 distinct bot participants while Solo is OFF and never creates bot accounts",async()=>{
    const m=await create();expect(m.players).toHaveLength(16);expect(m.players.filter(p=>p.kind==="BOT")).toHaveLength(15);
    expect(new Set(m.players.map(p=>p.participantId)).size).toBe(16);
    expect(m.players.filter(p=>p.kind==="BOT").map(p=>p.name)).toEqual(Array.from({length:15},(_,i)=>`Bot ${i+1}`));
    expect((await db.query("SELECT * FROM ranked_players")).rows).toHaveLength(1);
    expect((await db.query("SELECT * FROM sur_profiles")).rows).toHaveLength(1);
  });
  it("rejects invalid credentials/release clients/OFF and returns idempotent creations",async()=>{
    await expect(debug.create(session,{...body(),password:"wrong"})).rejects.toMatchObject({status:403});
    await expect(debug.create({...session,clientBuildChannel:"release"},body())).rejects.toMatchObject({status:403});
    const b=body(),a=await debug.create(session,b);expect((await debug.create(session,b)).matchId).toBe(a.matchId);
    await expect(debug.create(session,{...b,averageGroup:"GOLD"})).rejects.toMatchObject({status:409});
    document={...document,config:{...document.config,enabled:false}};await config.refresh();
    expect((await debug.view(a.matchId,session)).matchId).toBe(a.matchId);
    // New admission is blocked after the existing match is explicitly forced.
    await debug.force(a.matchId,session,{password:"fixture-password",rank:1});
    await expect(debug.create(session,body())).rejects.toMatchObject({status:503});
  });
  it("uses actual native readiness before bot readiness/entry and keeps the 3s countdown outside play",async()=>{
    let m=await create();await engine.selectMap(m.id);await debug.tickOnce();m=await engine.get(m.id);
    expect(survivalCurrentRound(m).players.every(p=>p.assetsReadyAt===null)).toBe(true);
    const first=survivalCurrentRound(m).map!.levelId;
    now+=15000;await debug.tickOnce();m=await engine.get(m.id);expect(m.state).toBe("MAP_SELECTION");
    await engine.selectMap(m.id);m=await engine.get(m.id);expect(survivalCurrentRound(m).map!.levelId).not.toBe(first);
    await send(m,"ASSETS_READY",{certificate:survivalCurrentRound(m).map!});m=await engine.get(m.id);
    now+=5000;await debug.tickOnce();m=await engine.get(m.id);expect(m.state).toBe("MAP_SELECTION");
    m=await prepare(m);expect(m.state).toBe("PLAYING");
    expect(survivalCurrentRound(m).endsAt!-survivalCurrentRound(m).startsAt!).toBe(180000);
  });
  it("persists initial/four monotone updates and does not redraw on duplicate ticks or restart",async()=>{
    const m=await prepare(await create()),r=survivalCurrentRound(m);
    for(const t of [0,45000,95000,125000,155000]) {
      now=r.startsAt!+t;await debug.tickOnce();
      const current=await engine.get(m.id);
      current.players.filter(p=>p.kind==="BOT").forEach((p,i)=>{
        const expected=survivalBotPlan(42,`${r.key}:${r.mapGeneration}`,i).filter(e=>e.offsetMs<=t).at(-1)!.best;
        expect(survivalCurrentRound(current).players.find(v=>v.participantId===p.participantId)!.highestProgress).toBe(expected);
      });
      const revision=current.revision;await Promise.all([debug.tickOnce(),debug.tickOnce()]);expect((await engine.get(m.id)).revision).toBe(revision);
    }
    expect((await db.query<{driver_state:Record<string,number>}>("SELECT driver_state FROM sur_debug_matches")).rows[0]!.driver_state).toEqual(
      Object.fromEntries(Array.from({length:15},(_,i)=>[`${r.key}:${r.mapGeneration}:bot-${String(i+1).padStart(2,"0")}`,4])));
  });
  it("ends when the human is eliminated and settles the recorded rank once without running bot-only rounds",async()=>{
    const soloBefore=(await db.query("SELECT * FROM ranked_profiles")).rows;
    const m=await prepare(await create());await record(m,0);now=survivalCurrentRound(m).endsAt!;
    await debug.tickOnce();const result=await debug.view(m.id,session);
    expect(result.state).toBe("FINAL_RESULT");expect(result.result!.rank).toBeGreaterThan(10);
    expect((await engine.get(m.id)).rounds).toHaveLength(1);
    await Promise.all([debug.view(m.id,session),debug.view(m.id,session),debug.tickOnce()]);
    expect((await db.query("SELECT * FROM sur_lp_ledger")).rows).toHaveLength(1);
    expect((await db.query("SELECT * FROM ranked_profiles")).rows).toEqual(soloBefore);
    expect((await survival.profile(session.playerId)).profile.lp).toBe(150+result.result!.actualDelta);
    expect((await survival.history(session.playerId)).matches).toHaveLength(1);
  });
  it("surrenders using current-stage records and allows a new session to recover a running game",async()=>{
    const m=await prepare(await create());await record(m,35);
    const recovered=await debug.active(session);expect(recovered.match!.matchId).toBe(m.id);
    const result=await send(m,"FORFEIT");expect(result.state).toBe("FINAL_RESULT");
    expect((await engine.get(m.id)).players.find(p=>p.playerId===session.playerId)!.forfeitReason).toBe("DEBUG_HUMAN_SURRENDER");
    expect(result.result!.rank).toBeGreaterThanOrEqual(1);
  });
  it("forces every n=1..16 immediately and applies the real M1 reward to only the human",async()=>{
    const deltas=[50,40,30,25,20,15,10,8,5,2,-10,-20,-30,-40,-50,-55];
    for(let rank=1;rank<=16;rank++) {
      const view=await debug.create(session,body(rank));expect(view.state).toBe("FINAL_RESULT");
      expect(view.result).toMatchObject({rank,requestedDelta:deltas[rank-1],completionKind:"DEBUG_FORCE"});
      await debug.acknowledge(view.matchId,session);
    }
    expect((await db.query("SELECT * FROM sur_lp_ledger")).rows).toHaveLength(16);
    expect((await db.query("SELECT * FROM ranked_players")).rows).toHaveLength(1);
  });
  it("adjusts bot snapshots to each selected tier and uses virtual LP without changing account LP",async()=>{
    await survival.profile(session.playerId);await db.query("UPDATE sur_profiles SET lp=20000,displayed_tier='GOLD'");
    const before=(await db.query("SELECT * FROM sur_profiles")).rows;
    const view=await debug.create(session,body(undefined,"SILVER"));
    expect(view.averageLp).toBeGreaterThanOrEqual(900);expect(view.averageLp).toBeLessThan(1200);
    expect((await db.query("SELECT * FROM sur_profiles")).rows).toEqual(before);
    expect((await db.query<{virtual_human_lp:boolean}>("SELECT virtual_human_lp FROM sur_debug_matches")).rows[0]!.virtual_human_lp).toBe(true);
  });
  it("runs normal rounds then immediate bot Death Match attempts and ends when the human completes three",async()=>{
    let m=await create();
    await db.query("UPDATE sur_debug_matches SET seed=3 WHERE match_id=$1",[m.id]);
    for(let phase=0;phase<4;phase++) {
      m=await prepare(m);await record(m,100);now=survivalCurrentRound(m).endsAt!;await debug.tickOnce();
      m=await engine.get(m.id);if(m.state==="FINAL_RESULT")break;
      await debug.tickOnce();m=await engine.get(m.id);
    }
    expect(m.state).not.toBe("FINAL_RESULT");
    {
      expect(survivalCurrentRound(m).phase).toBe("TIEBREAK");m=await prepare(m);
      expect(survivalCurrentRound(m).players.filter(p=>p.participantId!=="human").every(p=>p.attempts.length===3 && p.attempts.every(a=>a.endedAt!==null))).toBe(true);
      for(let i=0;i<3;i++)await record(m,100);
      const end=await debug.view(m.id,session);expect(end.state).toBe("FINAL_RESULT");expect(end.result!.rank).toBe(1);
    }
    expect((await debug.view(m.id,session)).result!.rank).toBe(1);
  });
  it("keeps bot joint winners on early elimination without bot accounts or official LP",async()=>{
    const m=await prepare(await create());await record(m,0);
    const snapshot=await engine.get(m.id);
    survivalCurrentRound(snapshot).players.filter(p=>p.participantId!=="human").forEach(p=>p.highestProgress=100);
    await db.query("UPDATE sur_matches SET snapshot=$2::jsonb WHERE id=$1",[m.id,JSON.stringify(snapshot)]);
    const result=await send(m,"FORFEIT");
    expect(result.result).toMatchObject({rank:16,requestedDelta:-55});
    expect(result.result!.standings.filter(p=>p.kind==="BOT").every(p=>p.rank===1 && p.tiedPlayers===15 && p.delta===-50)).toBe(true);
    expect((await engine.get(m.id)).winnerIds).toHaveLength(15);
    expect((await db.query("SELECT * FROM sur_profiles")).rows).toHaveLength(1);
  });
  it("accepts surrender before map selection using its zero-record competitive rank",async()=>{
    const m=await create();const result=await send(m,"FORFEIT");
    expect(result.state).toBe("FINAL_RESULT");expect(result.result).toMatchObject({rank:1,tiedPlayers:16,requestedDelta:-55});
  });
  it("rechecks environment at resource, entry and Attempt boundaries independently of Solo OFF",async()=>{
    let m=await create();await engine.selectMap(m.id);m=await engine.get(m.id);
    vi.mocked(sessions.assertEnvironment).mockImplementationOnce(()=>{throw new ForbiddenException("MOD_ENVIRONMENT_BLOCKED");});
    await expect(send(m,"ASSETS_READY",{certificate:survivalCurrentRound(m).map!})).rejects.toMatchObject({status:403});
    expect((await engine.get(m.id)).state).toBe("PREPARING");
    m=await prepare(m);await record(m,50);expect(sessions.assertEnvironment).toHaveBeenCalledWith(mods);
  });
  it("settles real GOLD promotion and grants shared GOLD and its AP claim only once",async()=>{
    await survival.profile(session.playerId);await db.query("UPDATE sur_profiles SET lp=1199,displayed_tier='SILVER'");
    const first=await debug.create(session,body(1));expect(first.result).toMatchObject({promoted:true,profileAfter:{tier:"GOLD",lp:1249}});
    await debug.acknowledge(first.matchId,session);const second=await debug.create(session,body(1));
    await Promise.all([debug.view(second.matchId,session),debug.tickOnce()]);
    expect((await db.query("SELECT * FROM ranked_achievement_unlocks WHERE achievement_id='gold_reached'")).rows).toHaveLength(1);
    expect((await achievements.claim(session.playerId,"gold_reached")).claimedRewards).toBe(1);
    expect((await achievements.claim(session.playerId,"gold_reached")).duplicate).toBe(true);
    expect((await db.query("SELECT * FROM ranked_ap_ledger WHERE achievement_id='gold_reached'")).rows).toHaveLength(1);
  });
  it("recovers with a replacement session and rejects mutations from the revoked session",async()=>{
    const m=await prepare(await create());await record(m,51);
    await db.query("UPDATE ranked_sessions SET revoked_at=$2 WHERE id=$1",[session.sessionId,clock.now().toISOString()]);
    const restored={...session,sessionId:id(3)};
    await db.query(`INSERT INTO ranked_sessions(id,player_id,token_hash,client_version,environment_snapshot,expires_at,last_heartbeat_at)
      VALUES($1,$2,$3,$4,'{"clientBuildChannel":"debug"}',$5,$6)`,[restored.sessionId,restored.playerId,tokens.hash("replacement-token"),REQUIRED_RANKED_CLIENT_VERSION,new Date(now+86400000),clock.now()]);
    expect((await debug.active(restored)).match!.matchId).toBe(m.id);
    await expect(debug.force(m.id,session,{password:"fixture-password",rank:1})).rejects.toMatchObject({status:403});
    const r=survivalCurrentRound(m);
    await expect(debug.event(m.id,session,{eventId:ids.next(),type:"FORFEIT",roundKey:r.key,mapGeneration:r.mapGeneration,levelId:r.map!.levelId})).rejects.toMatchObject({status:403});
    expect((await debug.force(m.id,restored,{password:"fixture-password",rank:3})).result!.rank).toBe(3);
  });
  it("exposes guarded HTTP mutation with whitelist validation and rejects client LP/rank submissions",async()=>{
    let app:INestApplication;
    const module=await Test.createTestingModule({controllers:[SurvivalDebugController],providers:[SessionGuard,AdmissionVersionGuard,
      {provide:SurvivalDebugService,useValue:debug},{provide:DATABASE,useValue:db},{provide:TokenService,useValue:tokens},{provide:SERVER_CLOCK,useValue:clock}]}).compile();
    for(const [name,dto] of [["create",CreateSurvivalDebugDto],["event",SurvivalDebugEventDto],["force",ForceSurvivalDebugDto]] as const)
      Reflect.defineMetadata("design:paramtypes",name==="create"?[Object,dto]:[String,Object,dto],SurvivalDebugController.prototype,name);
    vi.spyOn(debug,"onApplicationBootstrap").mockImplementation(()=>{});
    app=module.createNestApplication();app.useLogger(false);app.useGlobalPipes(new ValidationPipe({whitelist:true,forbidNonWhitelisted:true,transform:true}));
    await app.listen(0,"127.0.0.1");const url=await app.getUrl(),headers={Authorization:"Bearer fixture-token","Content-Type":"application/json"};
    try {
      expect((await fetch(`${url}/api/ranked/survival/debug/active`)).status).toBe(401);
      const bad=await fetch(`${url}/api/ranked/survival/debug/matches`,{method:"POST",headers,body:JSON.stringify({...body(),delta:999})});expect(bad.status).toBe(400);
      const response=await fetch(`${url}/api/ranked/survival/debug/matches`,{method:"POST",headers,body:JSON.stringify(body())});expect(response.status).toBe(201);
      const view=await response.json() as {matchId:string};
      expect((await fetch(`${url}/api/ranked/survival/debug/matches/${view.matchId}`,{headers})).status).toBe(200);
      const event=await fetch(`${url}/api/ranked/survival/debug/matches/${view.matchId}/events`,{method:"POST",headers,body:JSON.stringify({eventId:ids.next(),type:"PROGRESS",roundKey:"stage-1",mapGeneration:1,levelId:40000000,participantId:"bot-01",rank:1,progress:99})});expect(event.status).toBe(400);
    } finally {await app.close();}
  });
});
