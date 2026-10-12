import "reflect-metadata";
import { readFile,readdir } from "node:fs/promises";
import { Test } from "@nestjs/testing";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { afterAll,beforeAll,beforeEach,describe,expect,it,vi } from "vitest";
import { DEFAULT_SURVIVAL_CONFIG,survivalCurrentRound,survivalMapRange,survivalSettlementInputs,
  type SurvivalCommand,type SurvivalMatch,type SurvivalVerifiedMap } from "@corum-ranked/rules";
import { SurvivalConfigService,type SurvivalConfigDocument } from "../src/config/survival-config.service.js";
import { SurvivalEngineService,type SurvivalAdmissionPlayer } from "../src/survival/survival-engine.service.js";
import { SurvivalEngineController } from "../src/survival/survival-engine.controller.js";
import { SessionGuard } from "../src/session/session.guard.js";
import { TokenService } from "../src/common/token.service.js";
import { SERVER_CLOCK } from "../src/common/runtime.module.js";
import { DATABASE } from "../src/database/database.port.js";
import { REQUIRED_RANKED_CLIENT_VERSION } from "../src/session/client-version.js";
import { SurvivalMapError,type SurvivalMapProvider,type SurvivalMapRequest } from "../src/survival/survival-map.provider.js";
import { PgliteDatabase } from "./pglite-database.js";
import { environmentFixture } from "./fixtures.js";

let pg:PGlite,database:PgliteDatabase,config:SurvivalConfigService,engine:SurvivalEngineService;
let now:number,sequence:number,events:number,levelId:number,document:SurvivalConfigDocument;
let entrants:SurvivalAdmissionPlayer[];
const clock={now:()=>new Date(now)},ids={next:()=>`00000000-0000-4000-8000-${String(++sequence).padStart(12,"0")}`};
const provider:SurvivalMapProvider={select:vi.fn(async(request:SurvivalMapRequest)=>{
  const range=survivalMapRange(request.group,request.phase);
  return {levelId:levelId++,stars:range.minimumStars??10,demon:range.minimumStars===null,
    demonDifficulty:range.minimumStars===null?range.demons[0]!:null,automatic:false,platformer:false,
    levelVersion:1,geometrySha256:"a".repeat(64),geometryBytes:64,verifiedAt:clock.now().toISOString()};
})};
beforeAll(async()=>{
  pg=new PGlite();database=new PgliteDatabase(pg);
  const directory=fileURLToPath(new URL("../../../migrations/",import.meta.url));
  for(const file of (await readdir(directory)).filter(n=>n.endsWith(".sql")).sort())await pg.exec(await readFile(`${directory}/${file}`,"utf8"));
},60_000);
afterAll(async()=>{await pg.close();});
beforeEach(async()=>{
  await database.query("TRUNCATE sur_matches,ranked_players CASCADE");now=Date.parse("2026-10-11T10:00:00Z");sequence=100;events=0;levelId=40000000;
  vi.mocked(provider.select).mockClear();
  document={generation:"engine-v1",generatedAt:clock.now().toISOString(),config:{...DEFAULT_SURVIVAL_CONFIG,enabled:true}};
  config=new SurvivalConfigService({fetchConfig:async()=>structuredClone(document)},environmentFixture());await config.refresh();
  engine=new SurvivalEngineService(database,clock,ids,config,provider);
  entrants=[];
  for(let i=0;i<16;i++){
    const playerId=ids.next();
    await database.query("INSERT INTO ranked_players(id,gd_account_id,gd_username) VALUES($1,$2,$3)",[playerId,10000+i,`Player ${i}`]);
    await database.query("INSERT INTO sur_profiles(player_id,lp,displayed_tier) VALUES($1,$2,'RED')",[playerId,150+i]);
    await database.query(`INSERT INTO ranked_profiles(player_id,hidden_mmr,initial_csmp_tier,initial_seed_mmr,seed_applied_at)
      VALUES($1,5000,'NONE',5000,CURRENT_TIMESTAMP)`,[playerId]);
    entrants.push({participantId:`p${String(i).padStart(2,"0")}`,kind:"HUMAN",playerId});
  }
});
async function send(match:SurvivalMatch,command:SurvivalCommand,eventKey=`event-${++events}`){return engine.dispatch(match.id,eventKey,command);}
const context=(match:SurvivalMatch)=>{const r=survivalCurrentRound(match);return{roundKey:r.key,mapGeneration:r.mapGeneration,levelId:r.map!.levelId};};
async function prepare(match:SurvivalMatch){
  await engine.selectMap(match.id);match=await engine.get(match.id);
  for(const p of survivalCurrentRound(match).players)match=await send(match,{type:"ASSETS_READY",...context(match),participantId:p.participantId,certificate:survivalCurrentRound(match).map!});
  for(const p of survivalCurrentRound(match).players)match=await send(match,{type:"ENTERED",...context(match),participantId:p.participantId});
  now=survivalCurrentRound(match).startsAt!;return engine.tick(match.id);
}
async function play(match:SurvivalMatch,scores?:number[]){
  match=await prepare(match);
  for(const [i,p] of survivalCurrentRound(match).players.entries()){
    match=await send(match,{type:"ATTEMPT_START",...context(match),participantId:p.participantId,attemptId:"a1"});
    match=await send(match,{type:"ATTEMPT_END",...context(match),participantId:p.participantId,attemptId:"a1",progress:scores?.[i]??100-i});
  }
  now=survivalCurrentRound(match).endsAt!;return engine.tick(match.id);
}
describe("durable Survival M3 engine",()=>{
  it("serves authenticated participant HUD state over real HTTP while keeping public mutation routes closed",async()=>{
    const match=await engine.create("http",entrants);
    const tokens=new TokenService(environmentFixture()),token="survival-fixture-session";
    const participant=entrants[0] as Extract<SurvivalAdmissionPlayer,{kind:"HUMAN"}>;
    await database.query(`INSERT INTO ranked_sessions(id,player_id,token_hash,client_version,environment_snapshot,expires_at,last_heartbeat_at)
      VALUES($1,$2,$3,$4,'{}',$5,$6)`,[ids.next(),participant.playerId,tokens.hash(token),REQUIRED_RANKED_CLIENT_VERSION,new Date(now+3600000),clock.now()]);
    vi.spyOn(engine,"onApplicationBootstrap").mockImplementation(()=>{});
    const module=await Test.createTestingModule({controllers:[SurvivalEngineController],providers:[SessionGuard,
      {provide:SurvivalEngineService,useValue:engine},{provide:DATABASE,useValue:database},
      {provide:TokenService,useValue:tokens},{provide:SERVER_CLOCK,useValue:clock}]}).compile();
    const app=module.createNestApplication();app.useLogger(false);await app.listen(0,"127.0.0.1");
    const url=await app.getUrl(),path=`${url}/api/ranked/survival/matches/${match.id}`,headers={Authorization:`Bearer ${token}`};
    try {
      expect((await fetch(path)).status).toBe(401);
      const response=await fetch(path,{headers});expect(response.status).toBe(200);expect(response.headers.get("cache-control")).toBe("no-store");
      const view=await response.json();expect(view).toMatchObject({matchId:match.id,phase:"ROUND_1",configGeneration:"engine-v1"});
      expect(view.participants).toHaveLength(16);
      expect((await fetch(`${url}/api/ranked/survival/matches/not-a-uuid`,{headers})).status).toBe(400);
      expect((await fetch(`${url}/api/ranked/survival/matches/${ids.next()}`,{headers})).status).toBe(404);
      for(const target of [`${url}/api/ranked/survival/matches`,`${path}/progress`,`${path}/result`])expect((await fetch(target,{method:"POST",headers})).status).toBe(404);
      document={...document,config:{...document.config,enabled:false}};await config.refresh();
      expect((await fetch(path,{headers})).status).toBe(200);
      const outsider=ids.next();await database.query("INSERT INTO ranked_players(id,gd_account_id,gd_username) VALUES($1,50000,'Outsider')",[outsider]);
      await database.query(`INSERT INTO ranked_profiles(player_id,hidden_mmr,initial_csmp_tier,initial_seed_mmr,seed_applied_at)
        VALUES($1,5000,'NONE',5000,CURRENT_TIMESTAMP)`,[outsider]);
      await database.query("UPDATE ranked_sessions SET player_id=$1",[outsider]);
      expect((await fetch(path,{headers})).status).toBe(403);
      expect((await engine.get(match.id)).revision).toBe(match.revision);
    } finally {await app.close();}
  });
  it("recovers a due playing round without waiting for another match's slow GD search",async()=>{
    const playing=await prepare(await engine.create("playing",entrants));
    const bots:SurvivalAdmissionPlayer[]=Array.from({length:16},(_,i)=>({participantId:`other-${i}`,kind:"BOT",name:`Fixture ${i}`,lp:150}));
    const selecting=await engine.create("slow-map",bots);let resolve!:(map:SurvivalVerifiedMap)=>void;
    vi.mocked(provider.select).mockImplementationOnce(()=>new Promise(r=>{resolve=r;}));
    now=survivalCurrentRound(playing).endsAt!;
    await engine.recoverActiveMatches();
    while(!resolve)await new Promise(r=>setTimeout(r,0));
    expect((await engine.get(playing.id)).state).toBe("ROUND_RESULT");
    expect((await engine.get(selecting.id)).state).toBe("MAP_SELECTION");
    resolve({levelId:50000000,stars:4,demon:false,demonDifficulty:null,automatic:false,platformer:false,
      levelVersion:1,geometrySha256:"a".repeat(64),geometryBytes:64,verifiedAt:clock.now().toISOString()});
    await engine.onApplicationShutdown();expect((await engine.get(selecting.id)).state).toBe("PREPARING");
  });
  it("captures Survival-only LP atomically, creates once, and preserves Solo rows",async()=>{
    const solo=(await database.query("SELECT * FROM ranked_profiles ORDER BY player_id")).rows;
    const [a,b]=await Promise.all([engine.create("once",entrants),engine.create("once",entrants)]);
    expect(a.id).toBe(b.id);expect(a.averageLp).toBe(157.5);
    await database.query("UPDATE sur_profiles SET lp=2000");document={...document,generation:"new",config:{...document.config,enabled:false}};await config.refresh();
    expect(await engine.create("once",entrants)).toEqual(a);
    expect((await engine.get(a.id)).configGeneration).toBe("engine-v1");
    expect((await database.query("SELECT * FROM ranked_profiles ORDER BY player_id")).rows).toEqual(solo);
    expect((await database.query<{count:number}>("SELECT COUNT(*)::int AS count FROM sur_matches")).rows[0]!.count).toBe(1);
  });
  it("rejects OFF admission, partial fields and conflicting idempotent creation",async()=>{
    await expect(engine.create("short",entrants.slice(0,15))).rejects.toThrow("exactly 16");
    await engine.create("once",entrants);await expect(engine.create("once",[...entrants].reverse())).rejects.toThrow("key changed");
    document={...document,config:{...document.config,enabled:false}};await config.refresh();
    await expect(engine.create("off",entrants)).rejects.toThrow("disabled");
  });
  it("runs every round, persists normalized results and keeps LP/achievements unchanged after OFF",async()=>{
    let match=await engine.create("full",entrants);const lp=(await database.query("SELECT * FROM sur_profiles ORDER BY player_id")).rows;
    const counts:number[]=[];
    for(let i=0;i<4;i++){
      counts.push(survivalCurrentRound(match).players.length);match=await play(match);
      if(i===0){document={...document,generation:"off",config:{...document.config,enabled:false}};await config.refresh();}
      if(i<3)match=await engine.tick(match.id);
    }
    expect(counts).toEqual([16,10,6,3]);expect(match.state).toBe("FINAL_RESULT");
    expect(survivalSettlementInputs(match)).toHaveLength(16);
    const ranked=await database.query<{final_rank:number}>("SELECT final_rank FROM sur_match_players WHERE match_id=$1 ORDER BY final_rank",[match.id]);
    expect(ranked.rows.map(r=>r.final_rank)).toEqual(Array.from({length:16},(_,i)=>i+1));
    expect((await database.query("SELECT * FROM sur_profiles ORDER BY player_id")).rows).toEqual(lp);
    expect((await database.query<{count:number}>("SELECT COUNT(*)::int AS count FROM ranked_achievement_events")).rows[0]!.count).toBe(0);
    expect((await database.query<{count:number}>("SELECT COUNT(*)::int AS count FROM sur_attempts")).rows[0]!.count).toBe(35);
    expect((await database.query<{level_id:string}>("SELECT level_id FROM sur_rounds ORDER BY ordinal")).rows.map(r=>Number(r.level_id))).toEqual([40000000,40000001,40000002,40000003]);
  },30_000);
  it("makes duplicate commands/late results harmless and forbids altered replay payloads",async()=>{
    let match=await prepare(await engine.create("duplicates",entrants));
    const start:SurvivalCommand={type:"ATTEMPT_START",...context(match),participantId:"p00",attemptId:"a1"};
    const started=await Promise.all([send(match,start,"start"),send(match,start,"start")]);expect(started[0]!.revision).toBe(started[1]!.revision);
    const progress:SurvivalCommand={type:"PROGRESS",...context(match),participantId:"p00",attemptId:"a1",progress:51};match=await send(match,progress,"progress");
    await expect(send(match,{...progress,progress:100},"progress")).rejects.toThrow("payload changed");
    now=survivalCurrentRound(match).endsAt!;
    const late={...progress,progress:100};const completed=await Promise.all([send(match,late,"late"),send(match,late,"late")]);
    expect(survivalCurrentRound(completed[0]!).players[0]!.highestProgress).toBe(51);
    expect(completed[0]!.rounds).toHaveLength(1);
    const next=await Promise.all([engine.tick(match.id),engine.tick(match.id)]);expect(next[0]!.rounds).toHaveLength(2);expect(next[1]!.rounds).toHaveLength(2);
    expect((await database.query<{count:number}>("SELECT COUNT(*)::int AS count FROM sur_rounds")).rows[0]!.count).toBe(2);
  });
  it("recovers persisted deadlines with a new service and never trusts a spectator identity",async()=>{
    const match=await prepare(await engine.create("recovery",entrants));now=survivalCurrentRound(match).endsAt!;
    const recovered=new SurvivalEngineService(database,clock,ids,config,provider);
    const result=await recovered.tick(match.id);expect(result.state).toBe("ROUND_RESULT");
    const account=match.players[0]!.playerId!;expect((await recovered.viewForPlayer(match.id,account)).myRank).not.toBeNull();
    await expect(recovered.viewForPlayer(match.id,ids.next())).rejects.toThrow("Not a Survival match participant");
  });
  it("persists tied final/Death Match attempts and precise settlement ranks",async()=>{
    let match=await engine.create("deathmatch",entrants);
    for(let i=0;i<3;i++){match=await play(match);match=await engine.tick(match.id);}
    match=await play(match,[100,100,97]);match=await engine.tick(match.id);expect(survivalCurrentRound(match).phase).toBe("TIEBREAK");
    match=await prepare(match);
    for(const [i,p] of survivalCurrentRound(match).players.entries())for(let a=1;a<=3;a++){
      match=await send(match,{type:"ATTEMPT_START",...context(match),participantId:p.participantId,attemptId:`a${a}`});
      match=await send(match,{type:"ATTEMPT_END",...context(match),participantId:p.participantId,attemptId:`a${a}`,progress:94.371-i*0.001});
    }
    expect(match.winnerId).toBe("p00");
    expect((await database.query<{score_millipercent:number}>("SELECT score_millipercent FROM sur_tiebreak_attempts ORDER BY participant_id,sequence")).rows.map(r=>r.score_millipercent)).toEqual([94371,94371,94371,94370,94370,94370]);
    expect((await database.query("SELECT * FROM sur_tiebreaks")).rows).toHaveLength(1);
  },30_000);
  it("stores provider failures and retries without starting a timer",async()=>{
    const match=await engine.create("retry",entrants);
    vi.mocked(provider.select).mockRejectedValueOnce(new SurvivalMapError("EMPTY_SEARCH","No maps"));
    await engine.selectMap(match.id);let saved=await engine.get(match.id);expect(saved.state).toBe("MAP_SELECTION");expect(survivalCurrentRound(saved).startsAt).toBeNull();
    const error=(await database.query<{map_error:{code:string}}>("SELECT map_error FROM sur_matches")).rows[0]!.map_error;expect(error.code).toBe("EMPTY_SEARCH");
    await engine.selectMap(match.id);expect(provider.select).toHaveBeenCalledTimes(1);now+=2000;await engine.selectMap(match.id);
    saved=await engine.get(match.id);expect(saved.state).toBe("PREPARING");expect(provider.select).toHaveBeenCalledTimes(2);
  });
  it("fences overlapping provider jobs and rejects replies from expired leases",async()=>{
    const match=await engine.create("lease",entrants);let resolve!:(map:SurvivalVerifiedMap)=>void;
    vi.mocked(provider.select).mockImplementationOnce(()=>new Promise(r=>{resolve=r;}));
    const pending=engine.selectMap(match.id);while(!resolve)await new Promise(r=>setTimeout(r,0));
    await engine.selectMap(match.id);expect(provider.select).toHaveBeenCalledTimes(1);
    now+=40001;await engine.selectMap(match.id);const after=await engine.get(match.id);
    resolve({...survivalCurrentRound(after).map!,levelId:99999999});await pending;
    expect(survivalCurrentRound(await engine.get(match.id)).map!.levelId).toBe(40000000);
  });
  it("accepts future bot identities through the same rules without creating M4 bots or Solo rows",async()=>{
    const mixed:SurvivalAdmissionPlayer[]=[entrants[0]!,...Array.from({length:15},(_,i)=>({participantId:`fixture-bot-${i}`,kind:"BOT" as const,name:`Fixture ${i}`,lp:150}))];
    const before=(await database.query("SELECT * FROM ranked_players ORDER BY id")).rows;
    const match=await engine.create("mixed",mixed);expect(match.players.filter(p=>p.kind==="BOT")).toHaveLength(15);
    expect((await database.query("SELECT * FROM ranked_players ORDER BY id")).rows).toEqual(before);
  });
});
