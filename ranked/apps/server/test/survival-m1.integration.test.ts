import { readFile, readdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { beforeAll, afterAll, beforeEach, describe, expect, it, vi } from "vitest";
import { DEFAULT_SURVIVAL_CONFIG, SeededRandom } from "@corum-ranked/rules";
import { AchievementService } from "../src/achievement/achievement.service.js";
import { RankedConfigService } from "../src/config/ranked-config.service.js";
import { SurvivalConfigService, type SurvivalConfigDocument } from "../src/config/survival-config.service.js";
import { SurvivalService } from "../src/survival/survival.service.js";
import { SessionService } from "../src/session/session.service.js";
import { TokenService } from "../src/common/token.service.js";
import { QueueService } from "../src/queue/queue.service.js";
import type { MatchService } from "../src/match/match.service.js";
import type { RankedSessionContext } from "../src/session/session.types.js";
import type { CreateSessionDto } from "../src/session/session.dto.js";
import { PgliteDatabase } from "./pglite-database.js";
import { configDocumentFixture, environmentFixture } from "./fixtures.js";

let pg: PGlite, database: PgliteDatabase, achievements: AchievementService, survival: SurvivalService;
let soloConfig: RankedConfigService, survivalConfig: SurvivalConfigService, sessions: SessionService, queue: QueueService;
let soloDocument = configDocumentFixture();
let survivalDocument: SurvivalConfigDocument;
let milliseconds: number, sequence: number;
const clock = { now: () => new Date(milliseconds) };
const ids = { next: () => `00000000-0000-4000-8000-${String(++sequence).padStart(12,"0")}` };
const tokens = new TokenService(environmentFixture());
const mods: CreateSessionDto["installedMods"] = [
  { id:"hwanhee1.corum_ranked", version:"0.1.0", enabled:true, loaded:true, internal:false, system:false },
  { id:"geode.node-ids", version:"v1.23.3", enabled:true, loaded:true, internal:false, system:false },
  { id:"syzzi.click_between_frames", version:"1.5.0", enabled:true, loaded:true, internal:false, system:false,
    settings: {"soft-toggle":false,"click-on-steps":false,"physics-bypass":false} },
];
const advance = () => { milliseconds += 10_000; };
async function admit(account: number): Promise<RankedSessionContext> {
  const created=await sessions.create({gdAccountId:String(account),gdUsername:`SurPlayer${account}`,clientVersion:"v0.1.0",clientInstanceId:`instance-${account}`,installedMods:mods});
  const row=(await database.query<{id:string;player_id:string}>("SELECT id,player_id FROM ranked_sessions WHERE token_hash=$1",[tokens.hash(created.sessionToken)])).rows[0]!;
  return {sessionId:row.id,playerId:row.player_id,gdAccountId:String(account),gdUsername:`SurPlayer${account}`,displayedTier:"BRONZE",hiddenMmr:2500,placementGames:5};
}
async function setModes(solo: boolean, sur: boolean) {
  soloDocument={...soloDocument, operational:{...soloDocument.operational,enabled:solo}};
  survivalDocument={...survivalDocument,config:{...survivalDocument.config,enabled:sur}};
  expect(await soloConfig.refresh()).toBe(true); expect(await survivalConfig.refresh()).toBe(true);
}
beforeAll(async()=>{
  pg=new PGlite(); database=new PgliteDatabase(pg);
  const directory=fileURLToPath(new URL("../../../migrations/",import.meta.url));
  for (const name of (await readdir(directory)).filter(name=>name.endsWith(".sql")).sort()) await pg.exec(await readFile(`${directory}/${name}`,"utf8"));
},60_000);
afterAll(async()=>{await pg.close();});
beforeEach(async()=>{
  await database.query("TRUNCATE ranked_players,ranked_config_snapshots CASCADE");
  milliseconds=Date.parse("2026-10-11T10:00:00Z"); sequence=100;
  soloDocument=configDocumentFixture("solo-m1");
  survivalDocument={generation:"sur-m1",generatedAt:clock.now().toISOString(),config:structuredClone(DEFAULT_SURVIVAL_CONFIG)};
  soloConfig=new RankedConfigService({fetchConfig:async()=>structuredClone(soloDocument)},environmentFixture(),database);
  survivalConfig=new SurvivalConfigService({fetchConfig:async()=>structuredClone(survivalDocument)},environmentFixture());
  achievements=new AchievementService(database,clock,ids);
  survival=new SurvivalService(database,clock,ids,survivalConfig,achievements);
  sessions=new SessionService(database,{fetchCurrentTier:async()=>"NONE"},clock,ids,soloConfig,tokens);
  queue=new QueueService(database,clock,ids,new SeededRandom(1),soloConfig,sessions,tokens,
    {settleActiveMatchesForPlayer:async()=>undefined} as unknown as MatchService);
  await setModes(true,false);
});

describe("Survival M1 independent profiles and LP",()=>{
  it("creates RED 150 even on first OFF browse, never resets on repeat, and leaves Solo unchanged",async()=>{
    await setModes(false,false);
    const player=await admit(111);
    const before=(await database.query("SELECT * FROM ranked_profiles WHERE player_id=$1",[player.playerId])).rows;
    const initial=await survival.profile(player.playerId);
    expect(initial).toMatchObject({enabled:false,profile:{lp:150,tier:"RED",demotionWarning:false,warningRecoveryLp:null}});
    await survival.settleLp(player.playerId,"internal-fixture-award",50);
    advance();
    const repeated=await Promise.all([survival.profile(player.playerId),survival.profile(player.playerId)]);
    expect(repeated.map(response=>response.profile.lp)).toEqual([200,200]);
    expect((await database.query("SELECT * FROM ranked_profiles WHERE player_id=$1",[player.playerId])).rows).toEqual(before);
    expect((await database.query("SELECT COUNT(*)::int AS count FROM sur_profiles")).rows).toEqual([{count:1}]);
  });
  it("uses shared ranks with gaps and first-to-current-LP ordering, without moving a zero-delta player",async()=>{
    const a=await admit(111),b=await admit(222),c=await admit(333);
    for (const player of [a,b,c]) await survival.profile(player.playerId);
    advance(); await survival.settleLp(b.playerId,"b-300",150);
    advance(); await survival.settleLp(a.playerId,"a-300",150);
    advance(); await survival.settleLp(b.playerId,"zero",0);
    const page=await survival.leaderboard();
    expect(page.players.map(player=>[player.gdAccountId,player.rank,player.visibleRankedScore])).toEqual([["222",1,300],["111",1,300],["333",3,150]]);
    expect((await survival.standing(a.playerId)).rank).toBe(1);
    expect((await survival.leaderboard(1,1))).toMatchObject({offset:1,hasMore:true,total:3});
    expect((await survival.leaderboard(10,1))).toMatchObject({players:[],total:3,hasMore:false});
    advance(); await survival.settleLp(b.playerId,"b-leaves-300",10);
    advance(); await survival.settleLp(b.playerId,"b-returns-to-300",-10);
    expect((await survival.leaderboard()).players.map(player=>[player.gdAccountId,player.rank]))
      .toEqual([["111",1],["222",1],["333",3]]);
  });
  it("atomically protects, clears at floor +50, and demotes SILVER900 to BRONZE850",async()=>{
    const player=await admit(111); await survival.profile(player.playerId);
    await survival.settleLp(player.playerId,"reach-silver",800);
    await survival.settleLp(player.playerId,"first-loss",-100);
    expect((await survival.profile(player.playerId)).profile).toMatchObject({lp:900,tier:"SILVER",demotionWarning:true,warningRecoveryLp:950});
    await survival.settleLp(player.playerId,"recover49",49);
    expect((await survival.profile(player.playerId)).profile.demotionWarning).toBe(true);
    await survival.settleLp(player.playerId,"recover1",1);
    expect((await survival.profile(player.playerId)).profile.demotionWarning).toBe(false);
    await survival.settleLp(player.playerId,"another-protection",-60);
    await survival.settleLp(player.playerId,"demote",-1);
    expect((await survival.profile(player.playerId)).profile).toMatchObject({lp:850,tier:"BRONZE",demotionWarning:false});
  });
  it("deduplicates concurrent result keys and rolls back LP plus ledger if shared unlock fails",async()=>{
    const player=await admit(111); await survival.profile(player.playerId);
    const results=await Promise.all([survival.settleLp(player.playerId,"same",50),survival.settleLp(player.playerId,"same",50)]);
    expect(results[0]).toEqual(results[1]);
    expect((await survival.profile(player.playerId)).profile.lp).toBe(200);
    await expect(survival.settleLp(player.playerId,"same",40)).rejects.toThrow("cannot change");
    const spy=vi.spyOn(achievements,"recordSharedTierReached").mockRejectedValueOnce(new Error("unlock write failed"));
    await expect(survival.settleLp(player.playerId,"gold-fails",1000)).rejects.toThrow("unlock write failed");
    spy.mockRestore();
    expect((await survival.profile(player.playerId)).profile.lp).toBe(200);
    expect((await database.query("SELECT COUNT(*)::int AS count FROM sur_lp_ledger")).rows).toEqual([{count:1}]);
  });
  it("auto-promotes with excess LP and reuses GOLD unlock, Claim, AP and one shared title",async()=>{
    const player=await admit(111); await survival.profile(player.playerId);
    await survival.settleLp(player.playerId,"sur-gold",1100);
    expect((await survival.profile(player.playerId)).profile).toMatchObject({lp:1250,tier:"GOLD",demotionWarning:false});
    await database.transaction(transaction=>achievements.recordPlacementCompletion(transaction,player.playerId,ids.next(),true,clock.now()));
    const state=await achievements.state(player.playerId);
    expect(state.achievements.filter(card=>card.scope==="SHARED").map(card=>card.id)).toEqual(["gold_reached"]);
    expect(state.achievements.filter(card=>card.scope==="SOLO_RANKED")).toHaveLength(20);
    expect(state.achievements.find(card=>card.id==="gold_reached")?.unlocks).toHaveLength(1);
    const claims=await Promise.all([achievements.claim(player.playerId,"gold_reached"),achievements.claim(player.playerId,"gold_reached")]);
    expect(claims.reduce((sum,claim)=>sum+claim.claimedAmount,0)).toBe(50);
    expect((await achievements.equipTitle(player.playerId,"gold_reached")).equippedTitle).toMatchObject({scope:"SHARED",nameKo:"[공용] 정상",level:null,displayLabel:null});
    expect((await achievements.state(player.playerId)).equippedTitle?.nameEn).toBe("[Shared] The Summit");
    expect((await database.query("SELECT COUNT(*)::int AS count FROM ranked_ap_ledger WHERE achievement_id='gold_reached'")).rows).toEqual([{count:1}]);
  });
  it("preserves previously claimed Solo GOLD and progressive titles after adding Survival",async()=>{
    const player=await admit(111);
    await database.transaction(transaction=>achievements.recordPlacementCompletion(transaction,player.playerId,ids.next(),true,clock.now()));
    await achievements.claim(player.playerId,"gold_reached");
    const unlock=(await database.query("SELECT * FROM ranked_achievement_unlocks WHERE player_id=$1 AND achievement_id='gold_reached'",[player.playerId])).rows;
    await survival.profile(player.playerId); await survival.settleLp(player.playerId,"second-mode-gold",1050);
    expect((await database.query("SELECT * FROM ranked_achievement_unlocks WHERE player_id=$1 AND achievement_id='gold_reached'",[player.playerId])).rows).toEqual(unlock);
    expect((await achievements.claim(player.playerId,"gold_reached")).claimedAmount).toBe(0);
    expect(achievements.titleProjection("ranked_wins",3)).toMatchObject({scope:"SOLO_RANKED",nameKo:"[솔로 랭크] 승리자",displayLabel:"III"});
  });
});

describe("Independent operational mode switches",()=>{
  it.each([[true,true],[false,true],[true,false],[false,false]])("Solo %s / Survival %s permits the shared lobby and only the configured admissions",async(solo,sur)=>{
    await setModes(solo,sur);
    const player=await admit(111);
    expect(soloConfig.getStatus().queueEnabled).toBe(solo);
    expect(survivalConfig.getStatus().enabled).toBe(sur);
    expect((await survival.profile(player.playerId)).enabled).toBe(sur);
    expect(()=>survival.rejectNewMatch()).toThrow(sur?"Survival matches arrive":"Survival is disabled");
    expect((await survival.leaderboard()).players).toHaveLength(1);
    if (!solo) {
      await expect(queue.join(player,{installedMods:mods})).rejects.toThrow("not operationally configured");
      await expect(queue.createDebugBotMatch(player,{installedMods:mods},{} as never)).rejects.toThrow("not operationally configured");
    } else {
      await database.query("UPDATE ranked_profiles SET placement_games=5,displayed_tier='BRONZE',hidden_mmr=2500 WHERE player_id=$1",[player.playerId]);
      expect((await queue.join(player,{installedMods:mods})).status).toBe("QUEUED");
    }
    expect((await survival.history(player.playerId)).matches).toEqual([]);
  });
  it("removes only QUEUED Solo entries before OFF becomes visible, preserving Survival profiles",async()=>{
    const player=await admit(111); await survival.profile(player.playerId);
    await database.query("UPDATE ranked_profiles SET placement_games=5,displayed_tier='BRONZE',hidden_mmr=2500 WHERE player_id=$1",[player.playerId]);
    expect((await queue.join(player,{installedMods:mods})).status).toBe("QUEUED");
    await setModes(false,true);
    expect((await database.query("SELECT status FROM ranked_queue_entries WHERE player_id=$1",[player.playerId])).rows).toEqual([{status:"LEFT"}]);
    expect((await survival.profile(player.playerId)).profile.lp).toBe(150);
    expect((await queue.status(player)).status).toBe("LEFT");
  });
  it("fences an in-flight admission against OFF publication and releases the fence after errors",async()=>{
    let release!:()=>void;
    const held=soloConfig.withAdmission(()=>new Promise<void>(resolve=>{release=resolve;}));
    await Promise.resolve();
    soloDocument={...soloDocument,operational:{...soloDocument.operational,enabled:false}};
    const closing=soloConfig.refresh();
    expect(soloConfig.getStatus().queueEnabled).toBe(true);
    release(); await held; expect(await closing).toBe(true);
    expect(soloConfig.getStatus().queueEnabled).toBe(false);
    await expect(soloConfig.withAdmission(async()=>{throw new Error("admission failure");})).rejects.toThrow();
    expect(await soloConfig.withAdmission(async()=>42)).toBe(42);
  });
  it("keeps last good Survival config on an invalid refresh and never changes Solo",async()=>{
    await setModes(true,true);
    survivalDocument={...survivalDocument,config:{...survivalDocument.config,minimumPlayers:7}};
    expect(await survivalConfig.refresh()).toBe(false);
    expect(survivalConfig.getStatus()).toMatchObject({ready:true,enabled:true,error:expect.stringContaining("player counts")});
    expect(soloConfig.getSnapshot().operational.tierBands[1]!.minInclusive).toBe(1000);
    expect(soloConfig.getStatus().queueEnabled).toBe(true);
  });
});
