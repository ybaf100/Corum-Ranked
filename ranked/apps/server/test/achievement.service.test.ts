import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, beforeEach, describe, expect, it } from "vitest";
import type { IdGenerator, ServerClock } from "../src/common/runtime.module.js";
import { AchievementService } from "../src/achievement/achievement.service.js";
import type { DatabasePort, SqlExecutor, SqlResult } from "../src/database/database.port.js";
import { PgliteDatabase } from "./pglite-database.js";

const migrations = [
  "0001_initial_ranked.sql",
  "0002_debug_bot_match.sql",
  "0002_attempt_start_leases.sql",
  "0003_debug_rating_playable_maps.sql",
  "0004_alpha37_schema_hardening.sql",
  "0005_v050_rating_system.sql",
  "0006_v050_dynamic_csmp_tiers.sql",
  "0007_solo_stage_placement.sql",
  "0008_placement_beta9.sql",
  "0009_placement_stage_count.sql",
  "0010_achievements_v1.sql",
] as const;

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
  for (const migration of migrations) {
    const path = fileURLToPath(new URL(`../../../migrations/${migration}`, import.meta.url));
    await pglite.exec(await readFile(path, "utf8"));
  }
  service = new AchievementService(database, new FixedClock(), new SequenceIds());
}, 60_000);

afterAll(async () => {
  await pglite.close();
});

beforeEach(async () => {
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
    expect(notices[playerA]?.map((notice) => notice.displayLabel)).toEqual(["I", "II", "III"]);

    const pending = await service.state(playerA);
    expect(pending.totalAp).toBe(0);
    expect(pending.unclaimedRewardCount).toBe(3);
    const winner = pending.achievements.find((item) => item.id === "ranked_wins");
    expect(winner).toMatchObject({ claimableRewards: 3, claimableAp: 35 });

    const claimed = await service.claim(playerA, "ranked_wins");
    expect(claimed).toMatchObject({ claimedAmount: 35, claimedRewards: 3, totalAp: 35, unclaimedRewardCount: 0 });
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

  it("backfills a pre-solo-Placement profile but never treats a persisted skip as completion", async () => {
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
      .toMatchObject({ unlocked: true, claimableRewards: 1 });

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
