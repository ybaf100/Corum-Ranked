import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, describe, expect, it } from "vitest";

let database: PGlite | null = null;

beforeAll(async () => {
  const initialMigrationPath = fileURLToPath(
    new URL("../../../migrations/0001_initial_ranked.sql", import.meta.url),
  );
  const debugMigrationPath = fileURLToPath(
    new URL("../../../migrations/0002_debug_bot_match.sql", import.meta.url),
  );
  const playableMapMigrationPath = fileURLToPath(
    new URL("../../../migrations/0003_debug_rating_playable_maps.sql", import.meta.url),
  );
  database = new PGlite();
  await database.exec(await readFile(initialMigrationPath, "utf8"));
  await database.exec(await readFile(debugMigrationPath, "utf8"));
  await database.exec(await readFile(playableMapMigrationPath, "utf8"));
}, 20_000);

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

  it("adds explicit debug/map columns plus Ranked history and leaderboard views", async () => {
    const columns = await database!.query<{ column_name: string }>(
      `SELECT column_name
       FROM information_schema.columns
       WHERE table_name = 'ranked_matches'
         AND column_name IN ('match_type', 'debug_config', 'debug_discord_events')
       ORDER BY column_name`,
    );
    expect(columns.rows.map((row) => row.column_name)).toEqual([
      "debug_config",
      "debug_discord_events",
      "match_type",
    ]);
    const view = await database!.query<{ viewname: string }>(
      "SELECT viewname FROM pg_views WHERE schemaname = 'public' AND viewname = 'ranked_public_match_history'",
    );
    expect(view.rows[0]?.viewname).toBe("ranked_public_match_history");
    const leaderboard = await database!.query<{ viewname: string }>(
      "SELECT viewname FROM pg_views WHERE schemaname = 'public' AND viewname = 'ranked_leaderboard'",
    );
    expect(leaderboard.rows[0]?.viewname).toBe("ranked_leaderboard");
    const roundColumns = await database!.query<{ column_name: string }>(
      `SELECT column_name
       FROM information_schema.columns
       WHERE table_name = 'ranked_rounds'
         AND column_name IN ('canonical_level_id', 'alternate_level_id', 'playable_level_id')
       ORDER BY column_name`,
    );
    expect(roundColumns.rows.map((row) => row.column_name)).toEqual([
      "alternate_level_id",
      "canonical_level_id",
      "playable_level_id",
    ]);
  });
});
