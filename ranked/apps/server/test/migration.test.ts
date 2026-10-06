import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, describe, expect, it } from "vitest";

let database: PGlite | null = null;

const migrationNames = [
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
  "0011_debug_spectate.sql",
    "0012_gameplay_controls.sql",
] as const;

const migrationSql = async (migrationName: string): Promise<string> => {
  const migrationPath = fileURLToPath(
    new URL(`../../../migrations/${migrationName}`, import.meta.url),
  );
  return readFile(migrationPath, "utf8");
};

beforeAll(async () => {
  database = new PGlite();
  for (const migrationName of migrationNames) {
    await database.exec(await migrationSql(migrationName));
  }
}, 60_000);

afterAll(async () => {
  await database?.close();
  database = null;
});

describe("PostgreSQL migration", () => {
  it("executes and creates every authoritative Ranked table", async () => {
    const result = await database!.query<{ tablename: string }>(
      "SELECT tablename FROM pg_tables WHERE schemaname = 'public' ORDER BY tablename",
    );
    expect(result.rows.map((row) => row.tablename)).toEqual([
      "debug_spectate_frames",
      "debug_spectate_recordings",
      "ranked_achievement_events",
      "ranked_achievement_metrics",
      "ranked_achievement_unlocks",
      "ranked_ap_ledger",
      "ranked_attempt_start_leases",
      "ranked_attempts",
      "ranked_config_snapshots",
      "ranked_deathmatch_attempts",
      "ranked_deathmatches",
      "ranked_match_tokens",
      "ranked_matches",
      "ranked_outbox_events",
      "ranked_placement_attempts",
      "ranked_placement_maps",
      "ranked_placement_runs",
      "ranked_players",
      "ranked_profiles",
      "ranked_queue_entries",
      "ranked_rounds",
      "ranked_runtime_features",
      "ranked_sessions",
    ]);
  });

  it("adds idempotent Achievement/AP/title persistence", async () => {
    const columns = await database!.query<{ column_name: string }>(`
      SELECT column_name FROM information_schema.columns
      WHERE table_schema = 'public' AND table_name = 'ranked_profiles'
    `);
    expect(columns.rows.map((row) => row.column_name)).toEqual(expect.arrayContaining([
      "total_ap", "equipped_achievement_id", "equipped_achievement_level",
    ]));

    const playerId = "00000000-0000-4000-8000-000000000010";
    await database!.query(
      "INSERT INTO ranked_players (id, gd_account_id, gd_username) VALUES ($1, $2, $3)",
      [playerId, 130, "achievement-player"],
    );
    await database!.query(
      "INSERT INTO ranked_profiles (player_id, hidden_mmr, initial_csmp_tier, initial_seed_mmr, seed_applied_at) VALUES ($1, 500, 'NONE', 500, CURRENT_TIMESTAMP)",
      [playerId],
    );
    await database!.query(
      `INSERT INTO ranked_achievement_unlocks
         (player_id, achievement_id, achievement_level, reward_ap, unlocked_at, source_event_key)
       VALUES ($1, 'ranked_wins', 1, 5, CURRENT_TIMESTAMP, 'test')`,
      [playerId],
    );
    await database!.query(
      `INSERT INTO ranked_ap_ledger
         (id, player_id, achievement_id, achievement_level, amount, awarded_at)
       VALUES ('00000000-0000-4000-8000-000000000011', $1, 'ranked_wins', 1, 5, CURRENT_TIMESTAMP)`,
      [playerId],
    );
    await expect(database!.query(
      `INSERT INTO ranked_ap_ledger
         (id, player_id, achievement_id, achievement_level, amount, awarded_at)
       VALUES ('00000000-0000-4000-8000-000000000012', $1, 'ranked_wins', 1, 75, CURRENT_TIMESTAMP)`,
      [playerId],
    )).rejects.toThrow();
  });

  it("contains every schema object required by alpha.37 readiness", async () => {
    const result = await database!.query<{
      lease_table: boolean;
      match_type: boolean;
      match_type_default: string | null;
      round_playable: boolean;
      attempt_played: boolean;
      deathmatch_played: boolean;
    }>(`
      SELECT
        to_regclass('public.ranked_attempt_start_leases') IS NOT NULL AS lease_table,
        EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='ranked_matches' AND column_name='match_type') AS match_type,
        (SELECT column_default FROM information_schema.columns WHERE table_schema='public' AND table_name='ranked_matches' AND column_name='match_type') AS match_type_default,
        EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='ranked_rounds' AND column_name='playable_level_id') AS round_playable,
        EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='ranked_attempts' AND column_name='played_level_id') AS attempt_played,
        EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name='ranked_deathmatch_attempts' AND column_name='played_level_id') AS deathmatch_played
    `);
    expect(result.rows[0]).toMatchObject({
      lease_table: true,
      match_type: true,
      round_playable: true,
      attempt_played: true,
      deathmatch_played: true,
    });
    expect(result.rows[0]?.match_type_default).toContain("RANKED_PVP");
  });

  it("adds v0.5 LP/series state without exposing hidden MMR through the public view", async () => {
    const columns = await database!.query<{ table_name: string; column_name: string }>(`
      SELECT table_name, column_name
      FROM information_schema.columns
      WHERE table_schema = 'public'
        AND table_name IN ('ranked_profiles', 'ranked_matches', 'ranked_public_match_history')
      ORDER BY table_name, ordinal_position
    `);
    const names = (table: string) => columns.rows
      .filter((row) => row.table_name === table)
      .map((row) => row.column_name);
    expect(names("ranked_profiles")).toEqual(expect.arrayContaining([
      "placement_results",
      "result_streak",
      "promotion_source_tier",
      "promotion_results",
      "demotion_warning",
    ]));
    expect(names("ranked_matches")).toEqual(expect.arrayContaining([
      "ranked_score_a_before",
      "ranked_score_delta_a",
      "ranked_score_a_after",
      "rating_presentation",
    ]));
    expect(names("ranked_public_match_history")).toEqual(expect.arrayContaining([
      "ranked_score_delta_a",
      "ranked_score_a_after",
    ]));
    expect(names("ranked_public_match_history").some((name) => name.startsWith("mmr_"))).toBe(false);
  });

  it("keeps post-baseline migrations idempotent for Render restarts", async () => {
    for (const migrationName of migrationNames.slice(1)) {
      await expect(database!.exec(await migrationSql(migrationName))).resolves.toBeDefined();
    }
  });

  it("enforces all-or-none initial seed persistence", async () => {
    await database!.query(
      "INSERT INTO ranked_players (id, gd_account_id, gd_username) VALUES ($1, $2, $3)",
      ["00000000-0000-4000-8000-000000000001", 123, "test-player"],
    );
    await expect(
      database!.query(
        "INSERT INTO ranked_profiles (player_id, hidden_mmr) VALUES ($1, $2)",
        ["00000000-0000-4000-8000-000000000001", 2500],
      ),
    ).rejects.toThrow();
  });

  it("accepts normalized operator-defined CSMP seed tiers", async () => {
    await database!.query(
      "INSERT INTO ranked_players (id, gd_account_id, gd_username) VALUES ($1, $2, $3)",
      ["00000000-0000-4000-8000-000000000002", 124, "dynamic-tier-player"],
    );
    await expect(
      database!.query(
        `INSERT INTO ranked_profiles (
           player_id, hidden_mmr, visible_ranked_score, initial_csmp_tier,
           initial_seed_mmr, seed_applied_at
         ) VALUES ($1, $2, 0, $3, $2, CURRENT_TIMESTAMP)`,
        ["00000000-0000-4000-8000-000000000002", 5000, "DIAMOND"],
      ),
    ).resolves.toBeDefined();
    await expect(
      database!.query(
        `UPDATE ranked_profiles SET initial_csmp_tier = ' diamond '
         WHERE player_id = $1`,
        ["00000000-0000-4000-8000-000000000002"],
      ),
    ).rejects.toThrow();
  });
});
