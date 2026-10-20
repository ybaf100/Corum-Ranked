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
  await database.query("UPDATE ranked_achievement_versions SET activated_at='2000-01-01',debug_enabled_at='2000-01-01'");
  service = new AchievementService(database, new FixedClock(), new SequenceIds());
}, 60_000);

afterAll(async () => {
  await pglite.close();
});

beforeEach(async () => {
  await database.query("UPDATE ranked_achievement_versions SET activated_at='2000-01-01',debug_enabled_at='2000-01-01'");
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
  it('returns all 46 cards partitioned 20/18/8 and rejects unknown scopes', async () => {
    const state=await service.state(playerA);
    expect(state.achievements).toHaveLength(46);
    expect(['SOLO_RANKED','SURVIVAL','SHARED'].map(scope=>state.achievements.filter(a=>a.scope===scope).length)).toEqual([20,18,8]);
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
    await expect(service.equipTitle(playerA,'survival_never_below_cutoff')).rejects.toThrow('locked');
  });
});

it('resets v1 achievement data once at cutover, preserves LP/MMR/account data and never resets v2 again',async()=>{
 await service.state(playerA);await seedReward('ranked_wins',1,5);await service.claim(playerA,'ranked_wins');await service.equipTitle(playerA,'ranked_wins',1);
 await database.query("UPDATE ranked_achievement_metrics SET ranked_wins=300 WHERE player_id=$1",[playerA]);
 const identity=(await database.query('SELECT * FROM ranked_players ORDER BY id')).rows;
 const rating=(await database.query('SELECT player_id,hidden_mmr,visible_ranked_score,displayed_tier,placement_games FROM ranked_profiles ORDER BY player_id')).rows;
 await database.query('DELETE FROM ranked_achievement_versions'); // Test an actual first deployment over populated v1 state.
 const sql=await readFile(new URL('0027_achievements_v2.sql',directory),'utf8');await pglite.exec(sql);
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
 await database.query("UPDATE ranked_achievement_versions SET activated_at='2000-01-01',debug_enabled_at='2000-01-01'");
});
it('derives all shared goals from separate post-cutover aggregates without merging LP/MMR',async()=>{
 await service.state(playerA);
 const rating=(await database.query('SELECT hidden_mmr,visible_ranked_score FROM ranked_profiles WHERE player_id=$1',[playerA])).rows;
 // Persisted aggregates are reconstructed by the server, independently in each mode.
 await database.query(`UPDATE ranked_achievement_metrics SET ranked_matches=20,ranked_wins=5,ranked_clears=1000,
  survival_matches=25,survival_wins=50,survival_round_survives=100,solo_gold_reached=TRUE,survival_gold_reached=TRUE WHERE player_id=$1`,[playerA]);
 await database.transaction(tx=>service.recordMatchResult(tx,'00000000-0000-4000-8000-000000000060',[baseFacts()],new FixedClock().now()));
 const state=await service.state(playerA),shared=state.achievements.filter(c=>c.scope==='SHARED');
 expect(shared.find(c=>c.id==='shared_matches')).toMatchObject({progress:{metricValue:46,level:1}});
 expect(shared.find(c=>c.id==='shared_wins')).toMatchObject({progress:{metricValue:55,level:2}});
 expect(shared.find(c=>c.id==='shared_mastery')).toMatchObject({progress:{metricValue:1100,level:4}});
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
