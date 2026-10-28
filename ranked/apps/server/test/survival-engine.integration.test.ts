import {SurvivalPlayerInfoController} from "../src/survival/survival-player-info.controller.js";
import {ForumRecordsService} from "../src/forum/forum-records.service.js";
import "reflect-metadata";
import { readFile,readdir } from "node:fs/promises";
import { Test } from "@nestjs/testing";
import { fileURLToPath } from "node:url";
import { gzipSync } from "node:zlib";
import { PGlite } from "@electric-sql/pglite";
import { afterAll,beforeAll,beforeEach,describe,expect,it,vi } from "vitest";
import { DEFAULT_SURVIVAL_CONFIG,SeededRandom,survivalCurrentRound,survivalMapRange,survivalSettlementInputs,
  type SurvivalCommand,type SurvivalMatch,type SurvivalVerifiedMap } from "@corum-ranked/rules";
import { SurvivalConfigService,type SurvivalConfigDocument } from "../src/config/survival-config.service.js";
import { SurvivalEngineService,type SurvivalAdmissionPlayer } from "../src/survival/survival-engine.service.js";
import { SurvivalRateService } from "../src/survival/survival-rate.service.js";
import { SurvivalEngineController } from "../src/survival/survival-engine.controller.js";
import { SessionGuard } from "../src/session/session.guard.js";
import { TokenService } from "../src/common/token.service.js";
import { SERVER_CLOCK } from "../src/common/runtime.module.js";
import { DATABASE } from "../src/database/database.port.js";
import { REQUIRED_RANKED_CLIENT_VERSION } from "../src/session/client-version.js";
import { GdSurvivalMapProvider,SurvivalMapError,type SurvivalMapProvider,type SurvivalMapRequest } from "../src/survival/survival-map.provider.js";
import { createSurvivalGdFetch } from "../src/survival/survival-gd-transport.js";
import { createGdRelayWorker } from "../../../tools/gd-relay/worker.mjs";
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
const defaultSelection=vi.mocked(provider.select).getMockImplementation()!;
beforeAll(async()=>{
  pg=new PGlite();database=new PgliteDatabase(pg);
  const directory=fileURLToPath(new URL("../../../migrations/",import.meta.url));
  for(const file of (await readdir(directory)).filter(n=>n.endsWith(".sql")).sort())await pg.exec(await readFile(`${directory}/${file}`,"utf8"));
},60_000);
afterAll(async()=>{await pg.close();});
beforeEach(async()=>{
  await database.query("TRUNCATE sur_matches,ranked_players CASCADE");now=Date.parse("2026-10-11T10:00:00Z");sequence=100;events=0;levelId=40000000;
  vi.mocked(provider.select).mockReset().mockImplementation(defaultSelection);
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
  now=survivalCurrentRound(match).entryRequestedAt!+5000;
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
  it("bounds failed pre-play supply searches and cancels without mutating profiles or LP",async()=>{
    const match=await engine.create("bounded-supply",entrants);
    const profiles=(await database.query("SELECT * FROM sur_profiles ORDER BY player_id")).rows;
    vi.mocked(provider.select).mockRejectedValue(new SurvivalMapError("API_ERROR","fixture upstream unavailable"));
    for(let attempt=0;attempt<6;attempt++){await engine.selectMap(match.id);now+=2001;}
    const final=await engine.get(match.id);expect(final.state).toBe("CANCELLED");expect(final.cancellationReason).toBe("MAP_SUPPLY_UNAVAILABLE");
    expect(survivalSettlementInputs(final)).toEqual([]);
    expect((await database.query("SELECT * FROM sur_profiles ORDER BY player_id")).rows).toEqual(profiles);
    expect((await database.query("SELECT * FROM sur_lp_ledger")).rows).toHaveLength(0);
  });
  it("keeps a relay-auth failure before the timer and recovers the same match through verified GD data without changing profiles",async()=>{
    const beforeSolo=(await database.query("SELECT * FROM ranked_profiles ORDER BY player_id")).rows;
    const beforeSurvival=(await database.query("SELECT * FROM sur_profiles ORDER BY player_id")).rows;
    const token="fixture_"+"a".repeat(48),geometry="kS38,1;1,1,2,30,3,10;";
    const record="1:40000000:18:4:17::43:0:25::15:2:8:10:9:30";
    const worker=createGdRelayWorker(async input=>String(input).endsWith("getGJLevels21.php")?
      new Response(`${record}###1:0:10#hash`):new Response(`${record}:5:1:4:${gzipSync(geometry).toString("base64url")}#hash`));
    const transport:typeof fetch=async(input,init)=>worker.fetch(new Request(input,init),{GD_RELAY_TOKEN:token});
    const makeEngine=(key:string)=>new SurvivalEngineService(database,clock,ids,config,new GdSurvivalMapProvider(new SeededRandom(1),
      createSurvivalGdFetch({origin:"https://relay.example",token:key},transport)));
    const broken=makeEngine("b".repeat(56)),match=await broken.create("relay-recovery",entrants);
    await broken.selectMap(match.id);
    expect((await broken.mapSelectionStatus(match.id)).errorMessage).toContain("UNAUTHORIZED");
    expect(survivalCurrentRound(await broken.get(match.id)).startsAt).toBeNull();
    now+=30000;
    const restored=makeEngine(token);await restored.selectMap(match.id);
    const saved=await restored.get(match.id);
    expect(saved.state).toBe("PREPARING");expect(survivalCurrentRound(saved).startsAt).toBeNull();
    expect(survivalCurrentRound(saved).map).toMatchObject({levelId:40000000,geometryBytes:Buffer.byteLength(geometry)});
    expect((await restored.mapSelectionStatus(match.id)).errorMessage).toBeNull();
    expect((await database.query("SELECT * FROM ranked_profiles ORDER BY player_id")).rows).toEqual(beforeSolo);
    expect((await database.query("SELECT * FROM sur_profiles ORDER BY player_id")).rows).toEqual(beforeSurvival);
  });
  it("serves authenticated participant HUD state over real HTTP while keeping public mutation routes closed",async()=>{
    const match=await engine.create("http",entrants);
    const tokens=new TokenService(environmentFixture()),token="survival-fixture-session";
    const participant=entrants[0] as Extract<SurvivalAdmissionPlayer,{kind:"HUMAN"}>;
    await database.query(`INSERT INTO ranked_sessions(id,player_id,token_hash,client_version,environment_snapshot,expires_at,last_heartbeat_at)
      VALUES($1,$2,$3,$4,'{}',$5,$6)`,[ids.next(),participant.playerId,tokens.hash(token),REQUIRED_RANKED_CLIENT_VERSION,new Date(now+3600000),clock.now()]);
    vi.spyOn(engine,"onApplicationBootstrap").mockImplementation(async()=>{});
    const module=await Test.createTestingModule({controllers:[SurvivalEngineController,SurvivalPlayerInfoController],providers:[SessionGuard,SurvivalRateService,
      {provide:SurvivalEngineService,useValue:engine},{provide:ForumRecordsService,useValue:{read:async(account:string)=>({gdAccountId:account,records:[{provider:"GDKF",rank:17}]})}},{provide:DATABASE,useValue:database},
      {provide:TokenService,useValue:tokens},{provide:SERVER_CLOCK,useValue:clock}]}).compile();
    const app=module.createNestApplication();app.useLogger(false);await app.listen(0,"127.0.0.1");
    const url=await app.getUrl(),path=`${url}/api/ranked/survival/matches/${match.id}`,headers={Authorization:`Bearer ${token}`};
    try {
      expect((await fetch(path)).status).toBe(401);
      const response=await fetch(path,{headers});expect(response.status).toBe(200);expect(response.headers.get("cache-control")).toBe("no-store");
      const view=await response.json();expect(view).toMatchObject({matchId:match.id,phase:"ROUND_1",configGeneration:"engine-v1"});
      expect(view.participants).toHaveLength(16);
      const info=await fetch(`${url}/api/ranked/survival/matches/${match.id}/player-info/${participant.participantId}`,{headers});
      expect(info.status).toBe(200);expect(await info.json()).toMatchObject({gdAccountId:"10000",participantId:participant.participantId,bot:false,records:[{provider:"GDKF",rank:17}]});
      expect((await fetch(`${url}/api/ranked/survival/matches/${match.id}/player-info/foreign`,{headers})).status).toBe(404);
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
    const fast=vi.mocked(provider.select).getMockImplementation()!;
    vi.mocked(provider.select).mockImplementation(request=>request.phase==="ROUND_1"?new Promise(r=>{resolve=r;}):fast(request));
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
  it("persists upstream denial diagnostics and retry deadlines across recovery without starting a round",async()=>{
    const match=await engine.create("http-denial",entrants);
    const http={endpoint:"getGJLevels21.php",status:403,server:"cloudflare",contentType:"text/plain",cfRay:"fixture-ray",
      cfMitigated:null,retryAfter:null,title:null,edgeErrorCode:"1006",textSummary:"error code: 1006",
      bodyKind:"CLOUDFLARE_BLOCK" as const};
    vi.mocked(provider.select).mockRejectedValueOnce(new SurvivalMapError("API_ERROR",
      `GD getGJLevels21.php HTTP 403; http=${JSON.stringify(http)}`,[],http,30000));
    await engine.selectMap(match.id);
    const status=await engine.mapSelectionStatus(match.id);
    expect(status.httpDiagnostics).toEqual(http);expect(status.errorMessage).toContain("fixture-ray");
    expect(Date.parse(status.retryAt!)-now).toBe(30000);
    const saved=await engine.get(match.id);expect(saved.state).toBe("MAP_SELECTION");
    expect(survivalCurrentRound(saved).startsAt).toBeNull();expect(survivalCurrentRound(saved).map).toBeNull();
    const recovered=new SurvivalEngineService(database,clock,ids,config,provider);
    now+=29999;await recovered.selectMap(match.id);expect(provider.select).toHaveBeenCalledTimes(1);
    now++;await recovered.selectMap(match.id);expect(provider.select).toHaveBeenCalledTimes(2);
    expect((await recovered.get(match.id)).state).toBe("PREPARING");
    expect((await recovered.mapSelectionStatus(match.id)).httpDiagnostics).toBeNull();
  });
  it("fences overlapping provider jobs and rejects replies from expired leases",async()=>{
    const match=await engine.create("lease",entrants);let resolve!:(map:SurvivalVerifiedMap)=>void;
    vi.mocked(provider.select).mockImplementationOnce(()=>new Promise(r=>{resolve=r;}));
    const pending=engine.selectMap(match.id);while(!resolve)await new Promise(r=>setTimeout(r,0));
    await engine.selectMap(match.id);expect(provider.select).toHaveBeenCalledTimes(1);
    now+=180001;await engine.selectMap(match.id);const after=await engine.get(match.id);
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

describe("global durable map selection history",()=>{
 const bots:SurvivalAdmissionPlayer[]=Array.from({length:16},(_,i)=>({participantId:`history-bot-${i}`,kind:"BOT",name:`History ${i}`,lp:150}));
 it("keeps the last thirty confirmed selections across matches and a fresh service",async()=>{
  for(let i=0;i<32;i++){const m=await engine.create(`history-${i}`,bots);await engine.selectMap(m.id);}
  const history=(await database.query<{level_id:string}>("SELECT level_id FROM sur_map_selection_history ORDER BY id DESC")).rows;
  expect(history).toHaveLength(32);expect(new Set(history.map(r=>r.level_id)).size).toBe(32);
  const restored=new SurvivalEngineService(database,clock,ids,config,provider);
  const next=await restored.create("history-restart",bots);await restored.selectMap(next.id);
  const request=vi.mocked(provider.select).mock.calls.at(-1)![0];
  expect(request.excludedLevelIds).toEqual(expect.arrayContaining(history.slice(0,30).map(r=>Number(r.level_id))));
  expect(request.excludedLevelIds).not.toContain(Number(history[31]!.level_id));
 });
 it("serializes confirmation of concurrent candidates and records no failed candidate",async()=>{
  const original=vi.mocked(provider.select).getMockImplementation()!;
  vi.mocked(provider.select).mockImplementation(async request=>{
   const map=await original(request);return {...map,levelId:request.excludedLevelIds.includes(40000000)?40000001:40000000};
  });
  const a=await engine.create("collision-a",entrants),b=await engine.create("collision-b",bots);
  await Promise.all([engine.selectMap(a.id),engine.selectMap(b.id)]);
  const history=(await database.query<{level_id:string;history_window:number}>("SELECT level_id,history_window FROM sur_map_selection_history")).rows;
  expect(history).toHaveLength(2);expect(new Set(history.map(r=>r.level_id)).size).toBe(2);
  expect(history.every(r=>r.history_window===30)).toBe(true);
  vi.mocked(provider.select).mockImplementation(original);
 });
 it("preserves the strict recent thirty exclusion when the verified pool is exhausted",async()=>{
  const a=await engine.create("scarce-a",entrants);await engine.selectMap(a.id);
  const selected=survivalCurrentRound(await engine.get(a.id)).map!;
  const original=vi.mocked(provider.select).getMockImplementation()!;
  vi.mocked(provider.select).mockImplementation(async request=>{
   if(request.excludedLevelIds.includes(selected.levelId))throw new SurvivalMapError("NO_ELIGIBLE_MAP","Only one eligible map in this fixture");
   return selected;
  });
  const b=await engine.create("scarce-b",bots);await engine.selectMap(b.id);
  expect((await database.query<{history_window:number}>("SELECT history_window FROM sur_map_selection_history WHERE match_id=$1",[b.id])).rows).toEqual([]);
  expect(survivalCurrentRound(await engine.get(b.id)).map).toBeNull();
  vi.mocked(provider.select).mockImplementation(original);
 });
});

it("durably preselects the next official map during results and adopts it after a process restart without rerolling",async()=>{
 let m=await database.transaction(tx=>engine.createWithin(tx,"controlled-prefetch",entrants,document,undefined,true));
 await engine.selectMap(m.id);m=await engine.get(m.id);expect(survivalCurrentRound(m).map).not.toBeNull();
 expect(m.state).toBe("MAP_SELECTION");now+=6000;await engine.tick(m.id);
 m=await prepare(await engine.get(m.id));
 for(const [i,p] of survivalCurrentRound(m).players.entries()){
  const attemptId=ids.next();m=await send(m,{type:"ATTEMPT_START",...context(m),participantId:p.participantId,attemptId});
  m=await send(m,{type:"ATTEMPT_END",...context(m),participantId:p.participantId,attemptId,progress:100-i});
 }
 now=survivalCurrentRound(m).endsAt!;m=await engine.tick(m.id);expect(m.state).toBe("ROUND_RESULT");
 await engine.selectMap(m.id);m=await engine.get(m.id);const next=structuredClone(m.nextMap);expect(next?.map.levelId).toBeGreaterThan(0);
 const history=(await database.query("SELECT * FROM sur_map_selection_history WHERE match_id=$1",[m.id])).rows;
 await engine.selectMap(m.id);expect((await database.query("SELECT * FROM sur_map_selection_history WHERE match_id=$1",[m.id])).rows).toEqual(history);
 const select=vi.fn(async()=>{throw new Error("must reuse the durable next map");});
 const restarted=new SurvivalEngineService(database,clock,ids,config,{select});
 now+=5000;m=await restarted.tick(m.id);expect(m.state).toBe("PREPARING");expect(survivalCurrentRound(m).map).toEqual(next!.map);
 await restarted.selectMap(m.id);expect(select).not.toHaveBeenCalled();
});

it('durably observes the last-ten-second cutoff without client polling and survives concurrent reports/restart',async()=>{
 let match=await prepare(await engine.create('achievement-boundary',entrants));
 const r=survivalCurrentRound(match);
 for(const [i,p] of r.players.entries()){
  match=await send(match,{type:'ATTEMPT_START',...context(match),participantId:p.participantId,attemptId:'boundary'});
  match=await send(match,{type:'PROGRESS',...context(match),participantId:p.participantId,attemptId:'boundary',progress:100-i});
 }
 now=survivalCurrentRound(match).endsAt!-10_000;
 // Recovery selects a playing round at its checkpoint, not only at endsAt.
 await engine.recoverActiveMatches();match=await engine.get(match.id);
 const checkpoint=survivalCurrentRound(match).achievementCheckpoint!;
 expect(checkpoint.status).toBe('CAPTURED');
 const risk=checkpoint.standings.find(p=>p.atRisk)!;
 const command={type:'PROGRESS' as const,...context(match),participantId:risk.participantId,attemptId:'boundary',progress:100};
 const restarted=new SurvivalEngineService(database,clock,ids,config,provider);
 now+=1000;
 await Promise.all([restarted.dispatch(match.id,'same-report',command),engine.dispatch(match.id,'same-report',command)]);
 const durable=(await database.query<{achievement_checkpoint:unknown}>('SELECT achievement_checkpoint FROM sur_rounds WHERE match_id=$1 AND round_key=$2',[match.id,r.key])).rows[0]!.achievement_checkpoint;
 expect(durable).toMatchObject({at:checkpoint.at,status:'CAPTURED',standings:expect.arrayContaining([expect.objectContaining({participantId:risk.participantId,atRisk:true,rescuedAt:now})])});
 expect((await database.query('SELECT event_key FROM sur_match_commands WHERE match_id=$1 AND event_key=$2',[match.id,'same-report'])).rows).toHaveLength(1);
 const saved=survivalCurrentRound(await restarted.get(match.id)).achievementCheckpoint;
 expect(saved).toEqual(durable);
});

it('preserves real Survival history and attempts while discarding only achievement-era evidence during reset',async()=>{
 const {AchievementService}=await import('../src/achievement/achievement.service.js');
 let match=await play(await engine.create('reset-preservation',entrants));
 const playerId=(entrants[0] as Extract<SurvivalAdmissionPlayer,{kind:'HUMAN'}>).playerId;
 const achievements=new AchievementService(database,clock,ids);await achievements.state(playerId);
 await database.query(`INSERT INTO ranked_survival_achievement_facts(player_id,match_id,completed_at,facts) VALUES($1,$2,$3,'{}')`,[playerId,match.id,clock.now().toISOString()]);
 const tables=['sur_profiles','sur_match_players','sur_round_players','sur_attempts','sur_tiebreak_attempts','sur_match_commands','sur_lp_ledger'];
 const before=Object.fromEntries(await Promise.all(tables.map(async t=>[t,(await database.query(`SELECT to_jsonb(x) AS row FROM ${t} x ORDER BY to_jsonb(x)::text`)).rows])));
 const snapshot=await engine.get(match.id);
 await database.query('UPDATE ranked_achievement_versions SET overhaul_activated_at=NULL');
 await pg.exec(await readFile(new URL('../../../migrations/0029_achievement_survival_overhaul.sql',import.meta.url),'utf8'));
 const after=Object.fromEntries(await Promise.all(tables.map(async t=>[t,(await database.query(`SELECT to_jsonb(x) AS row FROM ${t} x ORDER BY to_jsonb(x)::text`)).rows])));
 expect(after).toEqual(before);
 for(const round of snapshot.rounds)delete round.achievementCheckpoint;
 expect(await engine.get(match.id)).toEqual(snapshot);
 expect((await database.query("SELECT achievement_checkpoint FROM sur_rounds WHERE match_id=$1",[match.id])).rows.every((r:any)=>r.achievement_checkpoint===null)).toBe(true);
 expect((await database.query('SELECT * FROM ranked_survival_achievement_facts')).rows).toHaveLength(0);
});
