import { randomUUID } from "node:crypto";
import { readFile, readdir } from "node:fs/promises";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { LiveService } from "../src/live/live.service.js";
import { PgliteDatabase } from "./pglite-database.js";

let pg: PGlite;
let live: LiveService;
const snapshotId = randomUUID();
let account = 70_000;

beforeAll(async () => {
  pg = new PGlite();
  const directory = new URL("../../../migrations/", import.meta.url);
  for (const name of (await readdir(directory)).filter((name) => name.endsWith(".sql")).sort()) {
    await pg.exec(await readFile(new URL(name, directory), "utf8"));
  }
  await pg.query("INSERT INTO ranked_config_snapshots (id, generation, rules_version, source_payload, fetched_at) VALUES ($1, 'live-test', 'test', '{}'::jsonb, CURRENT_TIMESTAMP)", [snapshotId]);
  live = new LiveService(new PgliteDatabase(pg));
}, 60_000);
afterAll(async () => { await pg.close(); });

async function player(name: string, tier = "BRONZE2", bot = false) {
  const id = randomUUID();
  await pg.query("INSERT INTO ranked_players (id, gd_account_id, gd_username, is_bot) VALUES ($1, $2, $3, $4)", [id, bot ? -(++account) : ++account, name, bot]);
  await pg.query(`INSERT INTO ranked_profiles (player_id, hidden_mmr, initial_csmp_tier, initial_seed_mmr, seed_applied_at, displayed_tier, visible_ranked_score)
    VALUES ($1, 777, 'NONE', 500, CURRENT_TIMESTAMP, $2, 700)`, [id, tier]);
  return id;
}

async function soloMatch(a: string, b: string, state: string, options: { createdAgo?: string; heartbeatAgo?: string | null; type?: string; format?: string } = {}) {
  const id = randomUUID();
  const created = `CURRENT_TIMESTAMP - INTERVAL '${options.createdAgo ?? "10 minutes"}'`;
  const heartbeat = options.heartbeatAgo === null ? "NULL" : `CURRENT_TIMESTAMP - INTERVAL '${options.heartbeatAgo ?? "5 seconds"}'`;
  await pg.query(`INSERT INTO ranked_matches (id, player_a_id, player_b_id, config_snapshot_id, mmr_a_before, mmr_b_before,
      effective_rating_average, effective_tier, candidate_maps_snapshot, series_state, current_round_number,
      round_wins_a, round_wins_b, state, rules_version, match_type, debug_config, created_at, started_at, last_heartbeat_a_at, deadline_at)
    VALUES ($1, $2, $3, $4, 777, 650, 713.5, 'BRONZE', '[]'::jsonb, $5::jsonb, 2, 1, 0, $6, 'test', $7, CASE WHEN $7 = 'DEBUG_BOT' THEN '{}'::jsonb END,
      ${created}, ${created}, ${heartbeat}, CURRENT_TIMESTAMP + INTERVAL '2 minutes')`,
    [id, a, b, snapshotId, JSON.stringify({ format: options.format ?? "BO3", roundResults: ["A"] }), state, options.type ?? "RANKED_PVP"]);
  await pg.query(`INSERT INTO ranked_rounds (id, match_id, round_number, level_id, canonical_level_id, playable_level_id, title, creator,
      difficulty, pool, qualifying_percent, phase, score_a, score_b, clears_a, clears_b)
    VALUES ($1, $2, 2, '1', '1', '1', 'Live Map', 'Maker', '12', 4, 25, 'ROUND_PLAYING', 312.5, 150, 1, 0)`, [randomUUID(), id]);
  return id;
}

describe("public live board", () => {
  it("lists fresh Solo matches with only on-screen information", async () => {
    const a = await player("live-a", "BRONZE2");
    const b = await player("live-b", "BRONZE3");
    await soloMatch(a, b, "ROUND_PLAYING");
    const stale = await player("stale-a"); const staleB = await player("stale-b");
    await soloMatch(stale, staleB, "ROUND_PLAYING", { createdAgo: "30 minutes", heartbeatAgo: "20 minutes" });
    const done = await player("done-a"); const doneB = await player("done-b");
    await soloMatch(done, doneB, "MATCH_RESULT");

    const snapshot = await live.snapshot();
    expect(snapshot.solo.matches).toHaveLength(1);
    const match = snapshot.solo.matches[0]!;
    expect(match.players.map((p) => [p.name, p.tier])).toEqual([["live-a", "BRONZE2"], ["live-b", "BRONZE3"]]);
    expect(match).toMatchObject({ state: "ROUND_PLAYING", format: "BO3", round: 2, roundWins: [1, 0], botMatch: false,
      map: { title: "Live Map", difficulty: "12", pool: 4 }, roundScore: { scores: [312.5, 150], clears: [1, 0] } });
    expect(JSON.stringify(snapshot)).not.toMatch(/777|hidden|mmr|session|token|"id"/i);
  });

  it("shows debug bot matches whose bot side never sends heartbeats", async () => {
    const human = await player("bot-host", "RED2");
    const bot = await player("Debug Bot", "RED2", true);
    await soloMatch(human, bot, "ROUND_PLAYING", { createdAgo: "20 minutes", heartbeatAgo: "10 seconds", type: "DEBUG_BOT", format: "SINGLE" });
    const match = (await live.snapshot()).solo.matches.find((m) => m.players[1]!.name === "Debug Bot");
    expect(match).toMatchObject({ botMatch: true, format: "SINGLE" });
    expect(match!.players[1]!.isBot).toBe(true);
  });

  it("counts only fresh queue entries and returns empty Survival lists", async () => {
    const waiting = await player("queued");
    const session = randomUUID();
    await pg.query("INSERT INTO ranked_sessions (id, player_id, token_hash, expires_at) VALUES ($1, $2, $3, CURRENT_TIMESTAMP + INTERVAL '1 hour')", [session, waiting, randomUUID()]).catch(() => undefined);
    const snapshot = await live.snapshot();
    expect(snapshot.survival.matches).toEqual([]);
    expect(typeof snapshot.solo.queued).toBe("number");
    expect(snapshot.survival.queued).toBe(0);
  });

  it("summarises a running Survival match without live ranks", async () => {
    const matchId = randomUUID();
    await pg.query(`INSERT INTO sur_matches (id, creation_key, request_hash, state, revision, config_generation, config_snapshot,
        average_lp, difficulty_group, current_round_key, snapshot, created_at, updated_at)
      VALUES ($1, $2, 'h', 'PLAYING', 3, 'g', '{}'::jsonb, 150, 'RED_BRONZE', 'r2', '{}'::jsonb, CURRENT_TIMESTAMP - INTERVAL '5 minutes', CURRENT_TIMESTAMP)`,
      [matchId, randomUUID()]);
    const humans = [await player("sur-alive"), await player("sur-out")];
    for (const [index, status] of ["ACTIVE", "ELIMINATED", "ACTIVE", "ACTIVE"].entries()) {
      const human = index < 2;
      await pg.query(`INSERT INTO sur_match_players (match_id, participant_id, player_id, player_kind, player_name, lp_snapshot, status)
        VALUES ($1, $2, $3, $4, $5, 150, $6)`, [matchId, `p${index}`, human ? humans[index] : null, human ? "HUMAN" : "BOT", human ? ["sur-alive", "sur-out"][index] : `bot${index}`, status]);
    }
    await pg.query(`INSERT INTO sur_rounds (match_id, round_key, ordinal, phase, target_qualifiers, map_generation, level_id, map_snapshot, starts_at, ends_at)
      VALUES ($1, 'r2', 2, 'ROUND_2', 6, 1, 40000001, $2::jsonb, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP + INTERVAL '180 seconds')`,
      [matchId, JSON.stringify({ name: "Sky Map", creator: "Maker", levelId: 40000001 })]);
    const [match] = (await live.snapshot()).survival.matches;
    expect(match).toMatchObject({ state: "PLAYING", phase: "ROUND_2", targetQualifiers: 6, entrants: 4, alive: 3, humans: 2,
      aliveHumans: ["sur-alive"], map: { title: "Sky Map", creator: "Maker" } });
    expect(JSON.stringify(match)).not.toMatch(/progress|rank|lp|participant/i);
  });
});
