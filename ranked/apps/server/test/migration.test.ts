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
      "ranked_attempt_start_leases",
      "ranked_attempts",
      "ranked_config_snapshots",
      "ranked_deathmatch_attempts",
      "ranked_deathmatches",
      "ranked_match_tokens",
      "ranked_matches",
      "ranked_outbox_events",
      "ranked_players",
      "ranked_profiles",
      "ranked_queue_entries",
      "ranked_rounds",
      "ranked_sessions",
    ]);
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
});
