import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, describe, expect, it } from "vitest";

let database: PGlite | null = null;

beforeAll(async () => {
  database = new PGlite();
  for (const migrationName of ["0001_initial_ranked.sql", "0002_attempt_start_leases.sql"]) {
    const migrationPath = fileURLToPath(
      new URL(`../../../migrations/${migrationName}`, import.meta.url),
    );
    await database.exec(await readFile(migrationPath, "utf8"));
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
