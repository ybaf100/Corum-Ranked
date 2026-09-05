import { randomUUID } from "node:crypto";
import { readFile, readdir } from "node:fs/promises";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import type { RankedConfigService } from "../src/config/ranked-config.service.js";
import { RankedConfigController } from "../src/config/ranked-config.controller.js";
import { LeaderboardService } from "../src/leaderboard/leaderboard.service.js";
import { configDocumentFixture } from "./fixtures.js";
import { PgliteDatabase } from "./pglite-database.js";

let pg: PGlite;
let database: PgliteDatabase;
const snapshotId = randomUUID();
let account = 50_000;
const configUnavailable = { getSnapshotOrNull: () => null } as RankedConfigService;

beforeAll(async () => {
  pg = new PGlite();
  database = new PgliteDatabase(pg);
  const directory = new URL("../../../migrations/", import.meta.url);
  for (const name of (await readdir(directory)).filter((name) => name.endsWith(".sql")).sort()) {
    await pg.exec(await readFile(new URL(name, directory), "utf8"));
  }
  await pg.query("INSERT INTO ranked_config_snapshots (id, generation, rules_version, source_payload, fetched_at) VALUES ($1, 'web-test', 'test', '{}'::jsonb, CURRENT_TIMESTAMP)", [snapshotId]);
}, 60_000);
afterAll(async () => { await pg.close(); });

async function profile(name: string, tier = "UNRANKED", lp: number | null = null, games = 0) {
  const playerId = randomUUID();
  await pg.query("INSERT INTO ranked_players (id, gd_account_id, gd_username) VALUES ($1, $2, $3)", [playerId, ++account, name]);
  await pg.query(`INSERT INTO ranked_profiles
    (player_id, hidden_mmr, initial_csmp_tier, initial_seed_mmr, seed_applied_at, displayed_tier, visible_ranked_score, placement_games, placement_results)
    VALUES ($1, 500, 'NONE', 500, CURRENT_TIMESTAMP, $2, $3, $4, '["WIN", "LOSS"]'::jsonb)`, [playerId, tier, lp, games]);
  return playerId;
}

async function run(playerId: string, totalStages: 3 | 5, state: string, completedStages: number, completedMaps: number, createdAt: string) {
  const runId = randomUUID();
  const mapIndex = state === "STAGE_RESULT" ? 2 : 1;
  await pg.query(`INSERT INTO ranked_placement_runs
    (id, player_id, config_snapshot_id, config_generation, state, starting_pool, stage_count,
     current_stage_index, current_map_index, stage_results, last_heartbeat_at, created_at)
    VALUES ($1, $2, $3, 'web-test', $4, 2, $5, 2, $6, $7::jsonb, CURRENT_TIMESTAMP, $8)`,
    [runId, playerId, snapshotId, state, totalStages, mapIndex, JSON.stringify(Array.from({ length: completedStages }, (_, i) => ({ stageIndex: i + 1, stageGrade: "B" }))), createdAt]);
  for (let index = 0; index < Math.max(completedMaps, 3); index++) {
    await pg.query(`INSERT INTO ranked_placement_maps
      (id, run_id, stage_index, map_index, level_id, canonical_level_id, playable_level_id,
       title, creator, difficulty, pool, qualifying_percent, state)
      VALUES ($1, $2, $3, $4, $5, $5, $5, 'Map', 'Creator', '5', 2, 20, $6)`,
      [randomUUID(), runId, Math.floor(index / 2) + 1, index % 2 + 1, String(100 + index), index < completedMaps ? "MAP_RESULT" : "MAP_PLAYING"]);
  }
  return runId;
}

// Exercise the production SQL against migrated PostgreSQL, including the lateral
// join, persisted Stage count, completed Map counts and legacy profile fields.
describe("public Web data", () => {
  it("reads the latest three-stage Run with config unavailable and excludes private run data", async () => {
    const playerId = await profile("solo-three", "UNRANKED", null, 4);
    await run(playerId, 5, "CANCELLED", 0, 0, "2026-09-01T00:00:00Z");
    await run(playerId, 3, "MAP_PLAYING", 1, 2, "2026-09-02T00:00:00Z");
    const result = await new LeaderboardService(database, configUnavailable).list();
    const player = result.players.find((p) => p.player === "solo-three")!;
    expect(result.serverOpen).toBe(false);
    expect(player.placement).toEqual({ state: "MAP_PLAYING", totalStages: 3, currentStageIndex: 2, currentMapIndex: 1, completedStages: 1, completedMaps: 2 });
    expect(player.tier).toBe("UNRANKED");
    expect(player.visibleRankedScore).toBeNull();
    expect(JSON.stringify(result)).not.toMatch(/hidden.?mmr|placement.?rating|runId|playerId|sessionToken|config_snapshot/i);
  });

  it("preserves a five-stage Stage Result without advancing its current stage", async () => {
    const playerId = await profile("solo-five");
    await run(playerId, 5, "STAGE_RESULT", 2, 4, "2026-09-02T00:00:00Z");
    const result = await new LeaderboardService(database, configUnavailable).list();
    expect(result.players.find((p) => p.player === "solo-five")!.placement).toEqual({ state: "STAGE_RESULT", totalStages: 5, currentStageIndex: 2, currentMapIndex: 2, completedStages: 2, completedMaps: 4 });
  });

  it("distinguishes not-started players and keeps finalized LP regardless of legacy counters", async () => {
    await profile("not-started", "UNRANKED", null, 4);
    await profile("finalized-red", "RED", 150, 0);
    const document = configDocumentFixture();
    const config = { getSnapshotOrNull: () => ({ ...document, fetchedAt: "2026-09-05T00:00:00Z" }) } as RankedConfigService;
    const result = await new LeaderboardService(database, config).list();
    expect(result.players.find((p) => p.player === "not-started")!.placement).toEqual({ state: "NOT_STARTED" });
    expect(result.players.find((p) => p.player === "finalized-red")).toMatchObject({ tier: "RED", visibleRankedScore: 150, placement: null });
  });

  it("serves only public active-map fields from a snapshot while Ranked is closed", () => {
    const document = configDocumentFixture();
    const maps = document.maps.map((map, index) => ({ ...map, active: index !== 0 }));
    const snapshot = { ...document, maps, operational: { ...document.operational, enabled: false }, fetchedAt: "2026-09-05T00:00:00Z" };
    const config = { getSnapshot: () => snapshot } as unknown as RankedConfigService;
    const result = new RankedConfigController(config).getPublicMaps() as { maps: Array<Record<string, unknown>> };
    expect(result.maps).toHaveLength(maps.length - 1);
    expect(result.maps[0]).toEqual({ title: maps[1]!.title, difficulty: maps[1]!.difficulty, mapCode: maps[1]!.playableLevelId, pool: maps[1]!.pool, qualifyingPercent: maps[1]!.qualifyingPercent });
    expect(JSON.stringify(result)).not.toMatch(/hidden|mmr|csmp|source_payload|allowedMods|seed|secret/i);
  });
});
