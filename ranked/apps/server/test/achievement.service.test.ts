import { readFile, readdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, beforeEach, describe, expect, it } from "vitest";
import type { IdGenerator, ServerClock } from "../src/common/runtime.module.js";
import { AchievementService } from "../src/achievement/achievement.service.js";
import type { DatabasePort, SqlExecutor, SqlResult } from "../src/database/database.port.js";
import { PgliteDatabase } from "./pglite-database.js";

const directory = new URL("../../../migrations/", import.meta.url);

let pglite: PGlite;
let database: PgliteDatabase;
let service: AchievementService;

class SequenceIds implements IdGenerator {
  private value = 100;
  public next(): string {
    this.value += 1;
    return `00000000-0000-4000-8000-${String(this.value).padStart(12, "0")}`;
  }
}

class FixedClock implements ServerClock {
  public now(): Date {
    return new Date("2026-09-13T12:00:00.000Z");
  }
}

class SerializedTransactionDatabase implements DatabasePort {
  private tail: Promise<void> = Promise.resolve();

  public constructor(private readonly delegate: DatabasePort) {}

  public query<Row>(sql: string, values: readonly unknown[] = []): Promise<SqlResult<Row>> {
    return this.delegate.query<Row>(sql, values);
  }

  public transaction<T>(operation: (client: SqlExecutor) => Promise<T>): Promise<T> {
    const result = this.tail.then(() => this.delegate.transaction(operation));
    this.tail = result.then(() => undefined, () => undefined);
    return result;
  }

  public ping(): Promise<boolean> {
    return this.delegate.ping();
  }
}

const playerA = "00000000-0000-4000-8000-000000000001";
const playerB = "00000000-0000-4000-8000-000000000002";
const legacyPlayer = "00000000-0000-4000-8000-000000000003";
const snapshotId = "00000000-0000-4000-8000-000000000090";

const baseFacts = (overrides: Record<string, unknown> = {}) => ({
  playerId: playerA,
  won: false,
  rankedClears: 0,
  perfectWin: false,
  lastAttemptClears: 0,
  reverseSweep: false,
  sweepWin: false,
  promotionSucceeded: false,
  reachedGold: false,
  deathmatches: [],
  ...overrides,
});

beforeAll(async () => {
  pglite = new PGlite();
  database = new PgliteDatabase(pglite);
  for (const migration of (await readdir(directory)).filter(name => name.endsWith(".sql")).sort()) {
    const path = fileURLToPath(new URL(`../../../migrations/${migration}`, import.meta.url));
    await pglite.exec(await readFile(path, "utf8"));
  }
  await database.query("UPDATE ranked_achievement_versions SET activated_at='2000-01-01',debug_enabled_at='2000-01-01',overhaul_activated_at='2000-01-01'");
  service = new AchievementService(database, new FixedClock(), new SequenceIds());
}, 60_000);

afterAll(async () => {
  await pglite.close();
});

beforeEach(async () => {
  await database.query("UPDATE ranked_achievement_versions SET activated_at='2000-01-01',debug_enabled_at='2000-01-01',overhaul_activated_at='2000-01-01'");
  await pglite.exec("TRUNCATE TABLE ranked_config_snapshots, ranked_players CASCADE");
  await database.query(
    `INSERT INTO ranked_players (id, gd_account_id, gd_username) VALUES
       ($1, 1, 'Player A'), ($2, 2, 'Player B')`,
    [playerA, playerB],
  );
  for (const id of [playerA, playerB]) {
    await database.query(
      `INSERT INTO ranked_profiles (
         player_id, hidden_mmr, visible_ranked_score, initial_csmp_tier,
         initial_seed_mmr, seed_applied_at
       ) VALUES ($1, 2500, 100, 'BRONZE', 2500, CURRENT_TIMESTAMP)`,
      [id],
    );
  }
  await database.query(
    `INSERT INTO ranked_config_snapshots
       (id, generation, rules_version, source_payload, fetched_at)
     VALUES ($1, 'achievement-test', 'v1', '{}'::jsonb, CURRENT_TIMESTAMP)`,
    [snapshotId],
  );
});

describe("AchievementService", () => {
  it("unlocks every crossed progressive level without awarding AP until Claim", async () => {
    await service.state(playerA);
    await database.query(
      "UPDATE ranked_achievement_metrics SET ranked_wins = 49 WHERE player_id = $1",
      [playerA],
    );
    const notices = await database.transaction((transaction) => service.recordMatchResult(
      transaction,
      "00000000-0000-4000-8000-000000000020",
      [baseFacts({ won: true })],
      new FixedClock().now(),
    ));
    expect(notices[playerA]?.filter(notice => notice.achievementId === "ranked_wins").map((notice) => notice.displayLabel)).toEqual(["I", "II", "III"]);

    const pending = await service.state(playerA);
    expect(pending.totalAp).toBe(0);
    expect(pending.unclaimedRewardCount).toBe(5);
    const winner = pending.achievements.find((item) => item.id === "ranked_wins");
    expect(winner).toMatchObject({ claimableRewards: 3, claimableAp: 35 });

    const claimed = await service.claim(playerA, "ranked_wins");
    expect(claimed).toMatchObject({ claimedAmount: 35, claimedRewards: 3, totalAp: 35, unclaimedRewardCount: 2 });
    const duplicate = await service.claim(playerA, "ranked_wins");
    expect(duplicate).toMatchObject({ claimedAmount: 0, claimedRewards: 0, duplicate: true, totalAp: 35 });

    const ledger = await database.query<{ amount: number }>(
      "SELECT amount FROM ranked_ap_ledger WHERE player_id = $1 ORDER BY achievement_level",
      [playerA],
    );
    expect(ledger.rows.map((row) => Number(row.amount))).toEqual([5, 10, 20]);

    // The append-only ledger, not today's config or unlock presentation value,
    // remains authoritative for AP that was already awarded.
    await database.query(
      "UPDATE ranked_achievement_unlocks SET reward_ap = 75 WHERE player_id = $1 AND achievement_id = 'ranked_wins'",
      [playerA],
    );
    expect((await service.state(playerA)).totalAp).toBe(35);
  });

  it("serializes concurrent Claim requests so one reward can enter the AP ledger only once", async () => {
    await database.transaction((transaction) => service.recordMatchResult(
      transaction,
      "00000000-0000-4000-8000-000000000026",
      [baseFacts({ won: true })],
      new FixedClock().now(),
    ));
    const concurrent = new AchievementService(
      new SerializedTransactionDatabase(database),
      new FixedClock(),
      new SequenceIds(),
    );
    const claims = await Promise.all([
      concurrent.claim(playerA, "ranked_wins"),
      concurrent.claim(playerA, "ranked_wins"),
    ]);
    expect(claims.map((claim) => claim.claimedAmount).sort((a, b) => a - b)).toEqual([0, 5]);
    expect(claims.every((claim) => claim.totalAp === 5)).toBe(true);
    const ledger = await database.query<{ count: number; total: number }>(
      `SELECT COUNT(*)::int AS count, COALESCE(SUM(amount), 0)::int AS total
       FROM ranked_ap_ledger WHERE player_id = $1 AND achievement_id = 'ranked_wins'`,
      [playerA],
    );
    expect(ledger.rows[0]).toEqual({ count: 1, total: 5 });
  });

  it("keeps counting beyond V without unlocking a sixth level or awarding AP", async () => {
    await service.state(playerA);
    await database.query(
      "UPDATE ranked_achievement_metrics SET ranked_wins = 810 WHERE player_id = $1",
      [playerA],
    );
    const first = await database.transaction((transaction) => service.recordMatchResult(
      transaction,
      "00000000-0000-4000-8000-000000000027",
      [baseFacts({ won: true })],
      new FixedClock().now(),
    ));
    expect(first[playerA]?.filter((notice) => notice.achievementId === "ranked_wins"))
      .toHaveLength(5);
    const second = await database.transaction((transaction) => service.recordMatchResult(
      transaction,
      "00000000-0000-4000-8000-000000000028",
      [baseFacts({ won: true })],
      new FixedClock().now(),
    ));
    expect(second[playerA]?.filter((notice) => notice.achievementId === "ranked_wins"))
      .toHaveLength(0);
    const state = await service.state(playerA);
    expect(state.totalAp).toBe(0);
    expect(state.achievements.find((item) => item.id === "ranked_wins")).toMatchObject({
      progress: { level: 5, metricValue: 812, gauge: 1, maxed: true },
      claimableRewards: 5,
      claimableAp: 150,
    });
  });

  it("deduplicates one-shot unlocks and supports Claim-independent titles", async () => {
    await database.transaction((transaction) => service.recordMatchResult(
      transaction,
      "00000000-0000-4000-8000-000000000021",
      [baseFacts({ won: true, reverseSweep: true })],
      new FixedClock().now(),
    ));
    await database.transaction((transaction) => service.recordMatchResult(
      transaction,
      "00000000-0000-4000-8000-000000000022",
      [baseFacts({ won: true, reverseSweep: true })],
      new FixedClock().now(),
    ));
    const rows = await database.query<{ count: number }>(
      `SELECT COUNT(*)::int AS count FROM ranked_achievement_unlocks
       WHERE player_id = $1 AND achievement_id = 'reverse_sweep_first'`,
      [playerA],
    );
    expect(rows.rows[0]?.count).toBe(1);
    await expect(service.equipTitle(playerB, "reverse_sweep_first")).rejects.toThrow();
    const equipped = await service.equipTitle(playerA, "reverse_sweep_first");
    expect(equipped.equippedTitle).toMatchObject({ achievementId: "reverse_sweep_first", level: null });
    expect((await service.state(playerA)).totalAp).toBe(0);
    expect((await service.clearTitle(playerA)).equippedTitle).toBeNull();
  });

  it("allows historical progressive title versions but rejects locked higher versions", async () => {
    await service.state(playerA);
    await database.query(
      "UPDATE ranked_achievement_metrics SET ranked_wins = 99 WHERE player_id = $1",
      [playerA],
    );
    await database.transaction((transaction) => service.recordMatchResult(
      transaction,
      "00000000-0000-4000-8000-000000000023",
      [baseFacts({ won: true })],
      new FixedClock().now(),
    ));
    await expect(service.equipTitle(playerA, "ranked_wins", 5)).rejects.toThrow();
    await expect(service.equipTitle(playerA, "ranked_wins", 1)).resolves.toMatchObject({
      equippedTitle: { achievementId: "ranked_wins", level: 1, displayLabel: "I" },
    });
    await expect(service.equipTitle(playerA, "ranked_wins", 4)).resolves.toMatchObject({
      equippedTitle: { level: 4, displayLabel: "IV" },
    });
  });

  it("retroactively unlocks First Step only for normal Placement completion", async () => {
    const completedRun = "00000000-0000-4000-8000-000000000030";
    const skippedRun = "00000000-0000-4000-8000-000000000031";
    const runSql = `INSERT INTO ranked_placement_runs (
      id, player_id, config_snapshot_id, config_generation, state, starting_pool,
      current_stage_index, current_map_index, placement_rating, final_hidden_mmr,
      final_visible_ranked_score, final_displayed_tier, last_heartbeat_at,
      finalized_at, completion_kind, stage_count
    ) VALUES ($1, $2, $3, 'achievement-test', 'PLACEMENT_RESULT', 3, 5, 2,
      2500, 2500, 100, 'BRONZE', CURRENT_TIMESTAMP, CURRENT_TIMESTAMP, $4, 5)`;
    await database.query(runSql, [completedRun, playerA, snapshotId, "COMPLETED"]);
    await database.query(runSql, [skippedRun, playerB, snapshotId, "SKIPPED"]);
    await database.query(
      "UPDATE ranked_placement_runs SET final_displayed_tier = 'GOLD' WHERE id = $1",
      [completedRun],
    );

    const completed = await service.state(playerA);
    const skipped = await service.state(playerB);
    expect(completed.achievements.find((item) => item.id === "placement_complete"))
      .toMatchObject({ unlocked: true, claimableRewards: 1 });
    expect(skipped.achievements.find((item) => item.id === "placement_complete"))
      .toMatchObject({ unlocked: false, claimableRewards: 0 });
    expect(completed.achievements.find((item) => item.id === "gold_reached"))
      .toMatchObject({ unlocked: true, claimableRewards: 1 });
  });

  it("does not fabricate pre-cutover Placement from profile counters or a persisted skip", async () => {
    await database.query(
      "INSERT INTO ranked_players (id, gd_account_id, gd_username) VALUES ($1, 3, 'Legacy Player')",
      [legacyPlayer],
    );
    await database.query(
      `INSERT INTO ranked_profiles (
         player_id, hidden_mmr, visible_ranked_score, displayed_tier, placement_games,
         initial_csmp_tier, initial_seed_mmr, seed_applied_at
       ) VALUES ($1, 2300, 200, 'AQUA', 5, 'AQUA', 2300, CURRENT_TIMESTAMP)`,
      [legacyPlayer],
    );
    const legacy = await service.state(legacyPlayer);
    expect(legacy.achievements.find((item) => item.id === "placement_complete"))
      .toMatchObject({ unlocked: false, claimableRewards: 0 });

    const skippedRun = "00000000-0000-4000-8000-000000000032";
    await database.query(
      `INSERT INTO ranked_placement_runs (
         id, player_id, config_snapshot_id, config_generation, state, starting_pool,
         current_stage_index, current_map_index, placement_rating, final_hidden_mmr,
         final_visible_ranked_score, final_displayed_tier, last_heartbeat_at,
         finalized_at, completion_kind, stage_count
       ) VALUES ($1, $2, $3, 'achievement-test', 'PLACEMENT_RESULT', 3, 5, 2,
         150, 150, 150, 'RED', CURRENT_TIMESTAMP, CURRENT_TIMESTAMP, 'SKIPPED', 5)`,
      [skippedRun, playerB, snapshotId],
    );
    await database.query(
      "UPDATE ranked_profiles SET displayed_tier = 'RED', placement_games = 5 WHERE player_id = $1",
      [playerB],
    );
    expect((await service.state(playerB)).achievements.find((item) => item.id === "placement_complete"))
      .toMatchObject({ unlocked: false, claimableRewards: 0 });
  });

  it("keeps a selected title unchanged when a higher level unlocks", async () => {
    await database.transaction((transaction) => service.recordMatchResult(
      transaction,
      "00000000-0000-4000-8000-000000000024",
      [baseFacts({ won: true })],
      new FixedClock().now(),
    ));
    await service.equipTitle(playerA, "ranked_wins", 1);
    await database.query(
      "UPDATE ranked_achievement_metrics SET ranked_wins = 9 WHERE player_id = $1",
      [playerA],
    );
    await database.transaction((transaction) => service.recordMatchResult(
      transaction,
      "00000000-0000-4000-8000-000000000025",
      [baseFacts({ won: true })],
      new FixedClock().now(),
    ));
    expect((await service.state(playerA)).equippedTitle).toMatchObject({
      achievementId: "ranked_wins", level: 1,
    });
  });
});

const seedReward = async (id: string, level = 0, ap = 10) => database.query(
  `INSERT INTO ranked_achievement_unlocks(player_id,achievement_id,achievement_level,reward_ap,unlocked_at,source_event_key)
   VALUES($1,$2,$3,$4,CURRENT_TIMESTAMP,'v2-fixture')`, [playerA,id,level,ap]);

describe('Achievement v2 Claim All and cutover', () => {
  it('returns all 48 cards partitioned 20/19/9 and rejects unknown scopes', async () => {
    const state=await service.state(playerA);
    expect(state.achievements).toHaveLength(48);
    expect(['SOLO_RANKED','SURVIVAL','SHARED'].map(scope=>state.achievements.filter(a=>a.scope===scope).length)).toEqual([20,19,9]);
    await expect(service.claimAll(playerA,'FORGED' as never)).rejects.toThrow('scope');
  });
  it('claims only the selected scope, snapshots AP, groups levels and preserves equipped titles', async () => {
    await service.state(playerA);
    await seedReward('ranked_wins',1,5);await seedReward('ranked_wins',2,10);
    await seedReward('deathmatch_double_hundred',0,200);await seedReward('survival_first_match');await seedReward('dual_mode_debut');
    await service.equipTitle(playerA,'ranked_wins',1);
    const result=await service.claimAll(playerA,'SOLO_RANKED');
    expect(result).toMatchObject({claimedAmount:215,claimedRewards:3,totalAp:215,unclaimedRewardCount:2});
    expect(result.claimedByAchievement).toContainEqual({achievementId:'ranked_wins',levels:[1,2]});
    expect(result.claimedByAchievement).toContainEqual({achievementId:'deathmatch_double_hundred',levels:[null]});
    expect((await service.state(playerA)).equippedTitle?.level).toBe(1);
    expect((await service.claimAll(playerA,'SURVIVAL')).claimedAmount).toBe(10);
    expect((await service.claimAll(playerA,'SHARED')).claimedAmount).toBe(10);
    expect((await service.claimAll(playerA,'ALL'))).toMatchObject({claimedAmount:0,claimedRewards:0,totalAp:235,unclaimedRewardCount:0});
  });
  it('serializes Claim/Claim All/retries without a second ledger credit', async () => {
    await service.state(playerA);
    for(let level=1;level<=5;level++)await seedReward('ranked_wins',level,[5,10,20,40,75][level-1]);
    await seedReward('survival_final_perfect_win',0,200);
    const responses=await Promise.all([service.claim(playerA,'ranked_wins'),service.claimAll(playerA,'ALL'),service.claimAll(playerA,'SOLO_RANKED'),service.claimAll(playerA,'ALL')]);
    expect(responses.reduce((sum,r)=>sum+r.claimedAmount,0)).toBe(350);
    expect((await service.state(playerA)).totalAp).toBe(350);
    expect((await database.query('SELECT amount FROM ranked_ap_ledger WHERE player_id=$1',[playerA])).rows).toHaveLength(6);
  });
  it('rolls back the entire claim if any ledger insertion fails, then permits an exact retry', async () => {
    await service.state(playerA);await seedReward('ranked_wins',1,5);await seedReward('ranked_wins',2,10);
    let writes=0;
    const failing:DatabasePort={query:database.query.bind(database),ping:database.ping.bind(database),transaction:operation=>database.transaction(tx=>operation({query:async <R>(sql:string,values:readonly unknown[]=[])=>{
      if(sql.includes('INSERT INTO ranked_ap_ledger')&&++writes===2)throw new Error('simulated storage failure');
      return tx.query<R>(sql,values);
    }}))};
    await expect(new AchievementService(failing,new FixedClock(),new SequenceIds()).claimAll(playerA,'ALL')).rejects.toThrow('storage failure');
    expect((await service.state(playerA))).toMatchObject({totalAp:0,unclaimedRewardCount:2});
    expect((await database.query('SELECT * FROM ranked_ap_ledger')).rows).toHaveLength(0);
    expect(await service.claimAll(playerA,'ALL')).toMatchObject({claimedAmount:15,claimedRewards:2,totalAp:15});
  });
  it('does not infer rewards from pre-cutover results, current tier, bot identity or invalid titles', async () => {
    await service.state(playerA);
    await database.query("UPDATE ranked_profiles SET displayed_tier='GOLD',placement_games=5 WHERE player_id=$1",[playerA]);
    await database.transaction(tx=>service.recordMatchResult(tx,'old',[baseFacts({won:true,reachedGold:true})],new Date('1999-01-01')));
    await database.query('UPDATE ranked_players SET is_bot=TRUE WHERE id=$1',[playerB]);
    await database.transaction(tx=>service.recordMatchResult(tx,'debug',[baseFacts({playerId:playerB,won:true,reachedGold:true})],new FixedClock().now()));
    expect((await service.state(playerA)).unclaimedRewardCount).toBe(0);expect((await service.state(playerB)).unclaimedRewardCount).toBe(0);
    expect((await database.query('SELECT * FROM ranked_achievement_events')).rows).toHaveLength(0);
    await expect(service.equipTitle(playerA,'survival_secret_2')).rejects.toThrow('locked');
  });
});

it('resets v1 achievement data once at cutover, preserves LP/MMR/account data and never resets v2 again',async()=>{
 await service.state(playerA);await seedReward('ranked_wins',1,5);await service.claim(playerA,'ranked_wins');await service.equipTitle(playerA,'ranked_wins',1);
 await database.query("UPDATE ranked_achievement_metrics SET ranked_wins=300 WHERE player_id=$1",[playerA]);
 const identity=(await database.query('SELECT * FROM ranked_players ORDER BY id')).rows;
 const rating=(await database.query('SELECT player_id,hidden_mmr,visible_ranked_score,displayed_tier,placement_games FROM ranked_profiles ORDER BY player_id')).rows;
 await database.query('DELETE FROM ranked_achievement_versions'); // Test an actual first deployment over populated v1 state.
 const sql=await readFile(new URL('0027_achievements_v2.sql',directory),'utf8');await pglite.exec(sql);
 await pglite.exec(await readFile(new URL('0029_achievement_survival_overhaul.sql',directory),'utf8'));
 expect((await database.query('SELECT * FROM ranked_players ORDER BY id')).rows).toEqual(identity);
 expect((await database.query('SELECT player_id,hidden_mmr,visible_ranked_score,displayed_tier,placement_games FROM ranked_profiles ORDER BY player_id')).rows).toEqual(rating);
 expect((await service.state(playerA))).toMatchObject({totalAp:0,unclaimedRewardCount:0,equippedTitle:null});
 expect((await database.query('SELECT * FROM ranked_ap_ledger')).rows).toHaveLength(0);
 expect((await database.query('SELECT ranked_wins FROM ranked_achievement_metrics WHERE player_id=$1',[playerA])).rows[0]).toEqual({ranked_wins:0});
 await seedReward('survival_first_match');await service.claimAll(playerA,'SURVIVAL');await service.equipTitle(playerA,'survival_first_match');
 const cutover=(await database.query('SELECT * FROM ranked_achievement_versions')).rows;
 await pglite.exec(sql);
 expect((await database.query('SELECT * FROM ranked_achievement_versions')).rows).toEqual(cutover);
 expect((await service.state(playerA))).toMatchObject({totalAp:10,unclaimedRewardCount:0,equippedTitle:{achievementId:'survival_first_match'}});
 await database.query("UPDATE ranked_achievement_versions SET activated_at='2000-01-01',debug_enabled_at='2000-01-01',overhaul_activated_at='2000-01-01'");
});
it('derives all shared goals from separate post-cutover aggregates without merging LP/MMR',async()=>{
 await service.state(playerA);
 const rating=(await database.query('SELECT hidden_mmr,visible_ranked_score FROM ranked_profiles WHERE player_id=$1',[playerA])).rows;
 // Persisted aggregates are reconstructed by the server, independently in each mode.
 await database.query(`UPDATE ranked_achievement_metrics SET ranked_matches=20,ranked_wins=5,ranked_clears=1000,
  survival_matches=25,survival_wins=50,survival_round_survives=100,ranked_rounds_played=1000,survival_rounds_played=100,survival_perfect_rounds=10,solo_gold_reached=TRUE,survival_gold_reached=TRUE WHERE player_id=$1`,[playerA]);
 await database.transaction(tx=>service.recordMatchResult(tx,'00000000-0000-4000-8000-000000000060',[baseFacts()],new FixedClock().now()));
 const state=await service.state(playerA),shared=state.achievements.filter(c=>c.scope==='SHARED');
 expect(shared.find(c=>c.id==='shared_matches')).toMatchObject({progress:{metricValue:46,level:1}});
 expect(shared.find(c=>c.id==='shared_wins')).toMatchObject({progress:{metricValue:55,level:2}});
 expect(shared.find(c=>c.id==='shared_mastery')).toMatchObject({progress:{metricValue:1100,level:4}});
 expect(shared.find(c=>c.id==='shared_clears')).toMatchObject({progress:{metricValue:1010,level:4}});
 for(const id of ['dual_mode_debut','dual_mode_winner','gold_reached','dual_gold_reached','master_of_two_worlds'])expect(shared.find(c=>c.id===id)).toMatchObject({unlocked:true});
 expect((await database.query('SELECT hidden_mmr,visible_ranked_score FROM ranked_profiles WHERE player_id=$1',[playerA])).rows).toEqual(rating);
});
it('does not turn admin-granted V titles into dual-mode performance mastery',async()=>{
 await service.state(playerA);await seedReward('ranked_wins',5,75);await seedReward('survival_wins',5,75);
 await database.transaction(tx=>service.recordMatchResult(tx,'00000000-0000-4000-8000-000000000061',[baseFacts()],new FixedClock().now()));
 expect((await service.state(playerA)).achievements.find(c=>c.id==='master_of_two_worlds')).toMatchObject({unlocked:false});
 await expect(service.equipTitle(playerA,'survival_wins',5)).resolves.toMatchObject({equippedTitle:{level:5}});
});

it('enables Debug parity without resetting earned v2 AP/titles and excludes results before its separate activation time',async()=>{
 await service.state(playerA);await seedReward('ranked_wins',1,5);await service.claim(playerA,'ranked_wins');await service.equipTitle(playerA,'ranked_wins',1);
 const sql=await readFile(new URL('0028_debug_achievement_parity.sql',directory),'utf8');
 const before=(await database.query('SELECT * FROM ranked_achievement_versions')).rows;
 await pglite.exec(sql);await pglite.exec(sql);
 expect((await database.query('SELECT * FROM ranked_achievement_versions')).rows).toEqual(before);
 expect((await service.state(playerA))).toMatchObject({totalAp:5,equippedTitle:{achievementId:'ranked_wins',level:1}});
 await database.query("UPDATE ranked_achievement_versions SET debug_enabled_at='2020-01-01'");
 await database.transaction(tx=>service.recordMatchResult(tx,'00000000-0000-4000-8000-000000000080',[baseFacts({won:true})],new Date('2010-01-01'),'DEBUG_BOT'));
 expect((await database.query('SELECT ranked_matches FROM ranked_achievement_metrics WHERE player_id=$1',[playerA])).rows[0]).toEqual({ranked_matches:0});
 await database.transaction(tx=>service.recordMatchResult(tx,'00000000-0000-4000-8000-000000000081',[baseFacts({won:true})],new Date('2021-01-01'),'DEBUG_BOT'));
 expect((await database.query('SELECT ranked_matches,ranked_wins FROM ranked_achievement_metrics WHERE player_id=$1',[playerA])).rows[0]).toEqual({ranked_matches:1,ranked_wins:1});
});

it('conceals Hidden names/conditions in the public state until real unlock and keeps Claim/title authority',async()=>{
 const state=await service.state(playerA);
 for(const [id,hint,rarity,ap] of [['survival_secret_1','일반 매치에서 달성 가능','EPIC',75],['survival_secret_2','데스매치에서 달성 가능','MYTHIC',200]] as const){
  const card=state.achievements.find(c=>c.id===id)!;
  expect(card).toMatchObject({hidden:true,concealed:true,nameKo:'Hidden',nameEn:'Hidden',descriptionKo:hint,titleEnabled:false,rarity,rewardAp:ap});
  const json=JSON.stringify(card);expect(json).not.toMatch(/아직 끝나지 않았다|자강두천|10초|last 10|Titans Clash|It Is Not Over/);
  expect(card).not.toHaveProperty('condition');
  await expect(service.equipTitle(playerA,id)).rejects.toThrow('locked');
  expect(await service.claim(playerA,id)).toMatchObject({claimedAmount:0,claimedRewards:0});
 }
 await seedReward('survival_secret_1',0,75);
 expect((await service.state(playerA)).achievements.find(c=>c.id==='survival_secret_1')).toMatchObject({concealed:false,nameKo:'아직 끝나지 않았다',titleEnabled:true});
 expect((await service.equipTitle(playerA,'survival_secret_1')).equippedTitle).toMatchObject({scope:'SURVIVAL',nameKo:'[서바이벌] 아직 끝나지 않았다'});
 expect((await service.claim(playerA,'survival_secret_1')).claimedAmount).toBe(75);
 expect((await service.claim(playerA,'survival_secret_1')).claimedAmount).toBe(0);
 expect(service.titleProjection('last_attempt_match_win',0)).toMatchObject({scope:'SOLO_RANKED',nameKo:'[솔로 랭크] 아직 끝나지 않았다'});
});

async function seedCompletedSoloMatch(){
 const id='00000000-0000-4000-8000-000000000200',roundId='00000000-0000-4000-8000-000000000201';
 await database.query(`INSERT INTO ranked_matches(id,player_a_id,player_b_id,config_snapshot_id,mmr_a_before,mmr_b_before,effective_rating_average,effective_tier,candidate_maps_snapshot,series_state,state,rules_version,match_type,winner_id,result_applied_at,created_at,finished_at,rating_presentation)
  VALUES($1,$2,$3,$4,2500,2500,2500,'RED','[]','{"roundResults":["A","A"]}','MATCH_RESULT','v1','RANKED_PVP',$2,'2026-09-13T12:00:00Z','2026-09-13T11:50:00Z','2026-09-13T12:00:00Z','{"keep":"rating","achievementUnlocks":[]}')`,[id,playerA,playerB,snapshotId]);
 await database.query(`INSERT INTO ranked_rounds(id,match_id,round_number,level_id,canonical_level_id,playable_level_id,title,creator,difficulty,pool,qualifying_percent,phase,started_at,settled_at)
  VALUES($1,$2,1,'40000000','40000000','40000000','fixture','fixture','NORMAL',1,50,'ROUND_RESULT','2026-09-13T11:50:00Z','2026-09-13T11:55:00Z')`,[roundId,id]);
 for(let i=1;i<=3;i++)await database.query(`INSERT INTO ranked_attempts(id,round_id,player_id,attempt_sequence,server_accepted_start_at,ended_at,level_id,played_level_id,progress_percent,cleared,valid,domain_attempt_id,client_start_event_id)
  VALUES($1,$2,$3,$4,'2026-09-13T11:51:00Z','2026-09-13T11:52:00Z','40000000','40000000',100,TRUE,$5,$6,$6)`,[`00000000-0000-4000-8000-${String(210+i).padStart(12,'0')}`,roundId,playerA,i,i!==3,'attempt-'+i]);
 return {id,roundId};
}
it('counts an official Solo round once across many attempts and settlement/restart retries',async()=>{
 const {id}=await seedCompletedSoloMatch();await service.state(playerA);
 // state recovery applied this source exactly once; retries must not add rounds.
 await database.transaction(tx=>service.recordMatchResult(tx,id,[baseFacts({won:true,rankedClears:2})],new FixedClock().now()));
 const restarted=new AchievementService(database,new FixedClock(),new SequenceIds());
 await database.transaction(tx=>restarted.recordMatchResult(tx,id,[baseFacts({won:true,rankedClears:2})],new FixedClock().now()));
 const row=(await database.query('SELECT ranked_rounds_played,ranked_clears,ranked_matches FROM ranked_achievement_metrics WHERE player_id=$1',[playerA])).rows[0];
 expect(row).toEqual({ranked_rounds_played:1,ranked_clears:2,ranked_matches:1});
 expect((await restarted.state(playerA)).achievements.find(c=>c.id==='shared_mastery')).toMatchObject({progress:{metricValue:1}});
 expect((await restarted.state(playerA)).achievements.find(c=>c.id==='shared_clears')).toMatchObject({progress:{metricValue:2}});
});

it('rolls back a failed overhaul, resets only achievements once, and blocks old-history resurrection',async()=>{
 await seedCompletedSoloMatch();await service.state(playerA);await service.claimAll(playerA,'ALL');await service.equipTitle(playerA,'ranked_wins',1);
 await database.query("INSERT INTO sur_profiles(player_id,lp,displayed_tier) VALUES($1,777,'BRONZE')",[playerA]);
 // Full rows of every unrelated table remain identical, including accounts,
 // actual attempts, config, LP/MMR and both mode history tables.
 const snapshot=async()=>{
  const tables=(await database.query<{tablename:string}>("SELECT tablename FROM pg_tables WHERE schemaname='public' ORDER BY tablename")).rows.map(t=>t.tablename)
   .filter(t=>!t.startsWith('ranked_achievement_')&&!['ranked_ap_ledger','ranked_survival_achievement_facts'].includes(t));
  return Object.fromEntries(await Promise.all(tables.map(async t=>{
   const exclusions=t==='ranked_profiles'?"- 'total_ap' - 'equipped_achievement_id' - 'equipped_achievement_level'":t==='ranked_matches'?"- 'rating_presentation'":"";
   return [t,(await database.query(`SELECT (to_jsonb(x) ${exclusions}) AS row FROM ${t} x ORDER BY (to_jsonb(x) ${exclusions})::text`)).rows];
  })));
 };
 const before=await snapshot();const priorRating=(await database.query("SELECT rating_presentation-'achievementUnlocks' AS rating FROM ranked_matches")).rows;
 await database.query('UPDATE ranked_achievement_versions SET overhaul_activated_at=NULL');
 const achievementsBefore=(await database.query('SELECT * FROM ranked_ap_ledger')).rows;
 const sql=await readFile(new URL('0029_achievement_survival_overhaul.sql',directory),'utf8');
 await pglite.exec(`CREATE FUNCTION fail_reset() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN IF NEW.total_ap=0 THEN RAISE EXCEPTION 'injected reset failure'; END IF; RETURN NEW; END $$;
 CREATE TRIGGER fail_reset BEFORE UPDATE ON ranked_profiles FOR EACH ROW EXECUTE FUNCTION fail_reset()`);
 await expect(pglite.exec(sql)).rejects.toThrow('injected reset failure');await pglite.exec('ROLLBACK');
 expect((await database.query('SELECT * FROM ranked_ap_ledger')).rows).toEqual(achievementsBefore);
 expect((await database.query('SELECT overhaul_activated_at FROM ranked_achievement_versions')).rows[0]).toEqual({overhaul_activated_at:null});
 await pglite.exec('DROP TRIGGER fail_reset ON ranked_profiles; DROP FUNCTION fail_reset()');
 await pglite.exec(sql);
 expect(await snapshot()).toEqual(before);
 expect((await database.query("SELECT rating_presentation-'achievementUnlocks' AS rating FROM ranked_matches")).rows).toEqual(priorRating);
 const marker=(await database.query('SELECT * FROM ranked_achievement_versions')).rows;
 expect((await service.state(playerA))).toMatchObject({totalAp:0,unclaimedRewardCount:0,equippedTitle:null});
 for(const table of ['ranked_ap_ledger','ranked_achievement_unlocks','ranked_achievement_events','ranked_survival_achievement_facts'])expect((await database.query(`SELECT * FROM ${table}`)).rows).toHaveLength(0);
 // Even deleting the lazy-sync cache cannot replay pre-reset source records.
 await database.query('DELETE FROM ranked_achievement_metrics');
 await service.state(playerA);expect((await database.query('SELECT * FROM ranked_achievement_unlocks')).rows).toHaveLength(0);
 await seedReward('survival_first_match');await service.claimAll(playerA,'ALL');await service.equipTitle(playerA,'survival_first_match');
 await pglite.exec(sql);await pglite.exec(sql);
 expect((await database.query('SELECT * FROM ranked_achievement_versions')).rows).toEqual(marker);
 expect((await new AchievementService(database,new FixedClock(),new SequenceIds()).state(playerA))).toMatchObject({totalAp:10,equippedTitle:{achievementId:'survival_first_match'}});
 expect(await snapshot()).toEqual(before);
});

it('persists five consecutive wins as a distinct Mythic and counts Survival perfect/played rounds once on late settlement and restart',async()=>{
 const {createSurvivalMatch,DEFAULT_SURVIVAL_CONFIG}=await import('@corum-ranked/rules');
 await service.state(playerA);
 const session='00000000-0000-4000-8000-000000000300';
 await database.query(`INSERT INTO ranked_sessions(id,player_id,token_hash,client_version,environment_snapshot,expires_at,last_heartbeat_at)
  VALUES($1,$2,'achievement-source-fixture','v0.5.0-beta.94','{}','2030-01-01','2026-09-13')`,[session,playerA]);
 const stored:string[]=[];
 for(let i=1;i<=8;i++){
  const id=`00000000-0000-4000-8000-${String(300+i).padStart(12,'0')}`,at=Date.parse('2026-09-13T11:00:00Z')+i*200_000;
  const m=createSurvivalMatch(id,[{participantId:'human',playerId:playerA,kind:'HUMAN',name:'Human',lp:100},{participantId:'bot',playerId:null,kind:'BOT',name:'Bot',lp:100}],DEFAULT_SURVIVAL_CONFIG,'test',at,false,true);
  const won=i!==6;m.state='FINAL_RESULT';m.finishedAt=at+180_000;m.winnerId=won?'human':'bot';m.players[0]!.finalRank=won?1:2;m.players[1]!.finalRank=won?2:1;
  const r=m.rounds[0]!;r.startsAt=at;r.endsAt=at+180_000;r.finishedAt=r.endsAt;r.map={levelId:40_000_000} as NonNullable<typeof r.map>;
  for(const [j,p] of r.players.entries()){
   p.rank=j===0?(won?1:2):(won?2:1);p.qualified=p.rank===1;p.enteredAt=at;
   p.highestProgress=j===0?(won?100:99):(won?50:100);p.attempts=[{id:'a',sequence:1,startedAt:at,endedAt:at+1000,highestProgress:p.highestProgress}];
  }
  await database.query(`INSERT INTO sur_matches(id,creation_key,request_hash,state,revision,config_generation,config_snapshot,average_lp,difficulty_group,current_round_key,snapshot,created_at,updated_at,finished_at)
   VALUES($1::uuid,$1::text,'test','FINAL_RESULT',1,'test','{}',100,'RED_BRONZE','stage-1',$2::jsonb,$3,$4,$4)`,[id,JSON.stringify(m),new Date(at).toISOString(),new Date(m.finishedAt).toISOString()]);
  await database.query(`INSERT INTO sur_debug_matches(match_id,human_player_id,admission_session_id,seed,average_group,human_actual_lp,last_seen_at,result,settled_at,created_at)
   VALUES($1,$2,$3,1,'RED_BRONZE',100,$4,'{"profileAfter":{"tier":"BRONZE"}}',$4,$4)`,[id,playerA,session,new Date(m.finishedAt).toISOString()]);
  stored.push(id);
 }
 const apply=async(id:string)=>database.transaction(tx=>service.recordSurvivalResult(tx,id,playerA,new FixedClock().now()));
 for(const i of [3,0,4,2])await apply(stored[i]!);
 expect((await service.state(playerA)).achievements.find(c=>c.id==='survival_true_champion')).toMatchObject({unlocked:false});
 const notices=await apply(stored[1]!);
 expect(notices).toEqual(expect.arrayContaining([expect.objectContaining({achievementId:'survival_true_champion',rarity:'MYTHIC',rewardAp:200,scope:'SURVIVAL'})]));
 expect((await service.claim(playerA,'survival_true_champion')).claimedAmount).toBe(200);
 const restarted=new AchievementService(database,new FixedClock(),new SequenceIds());
 await database.transaction(tx=>restarted.recordSurvivalResult(tx,stored[1]!,playerA,new FixedClock().now()));
 expect((await restarted.claim(playerA,'survival_true_champion')).claimedAmount).toBe(0);
 for(const i of [5,6,7])await apply(stored[i]!);
 expect((await database.query(`SELECT survival_matches,survival_wins,survival_rounds_played,survival_perfect_rounds,current_survival_win_streak,survival_win_streak,survival_podium_streak FROM ranked_achievement_metrics WHERE player_id=$1`,[playerA])).rows[0]).toEqual({survival_matches:8,survival_wins:7,survival_rounds_played:8,survival_perfect_rounds:7,current_survival_win_streak:2,survival_win_streak:5,survival_podium_streak:8});
 const state=await service.state(playerA);
 expect(state.achievements.find(c=>c.id==='shared_mastery')).toMatchObject({progress:{metricValue:8}});
 expect(state.achievements.find(c=>c.id==='shared_clears')).toMatchObject({progress:{metricValue:7}});
 expect(state.achievements.map(c=>String(c.id)).includes('survival_win_streak')).toBe(false);
});
