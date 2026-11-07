import { readFile, readdir } from "node:fs/promises";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import type { RankedConfigService } from "../src/config/ranked-config.service.js";
import { MapPoolService } from "../src/map-pool/map-pool.service.js";
import { PgliteDatabase } from "./pglite-database.js";

const directory = new URL("../../../migrations/", import.meta.url);
let pglite: PGlite, database: PgliteDatabase;
let now = new Date("2026-11-07T12:00:00.000Z");
const clock = { now: () => now };

const uuid = (n: number) => `00000000-0000-4000-8000-${String(n).padStart(12, "0")}`;
const [ace, bee, bot] = [uuid(1), uuid(2), uuid(3)];
const snapshot = uuid(10);
const map = (id: string, title: string, difficulty: string, pool: number, active = true) => ({
  levelId: `9${id}`, canonicalLevelId: id, alternateLevelId: `9${id}`, playableLevelId: `9${id}`, title, creator: "maker",
  // The sheet's difficulty is a numeric Corum rating; the GD label lives in metadata.
  difficulty: String(20 - pool), pool, qualifyingPercent: 40, active,
  metadata: { kind: difficulty.includes("Demon") ? "DEMON" : "NORMAL", stars: 8, gddlTier: null, gddlColor: null, durationSeconds: 84,
    officialLevelId: null, gdDifficulty: difficulty },
});
const maps = [map("101", "Zeta", "Insane", 3), map("102", "Alpha", "Hard", 1), map("103", "Demon One", "Easy Demon", 4), map("104", "Retired", "Hard", 1, false)];
const config = { getSnapshot: () => ({ generation: "g1", maps }) } as unknown as RankedConfigService;

let sequence = 100;
async function match(options: { type?: string; ban?: [string | null, string | null]; mapBan?: boolean; tier?: string } = {}) {
  const id = uuid(++sequence);
  const candidates = maps.slice(0, 3).map((entry) => ({ canonicalLevelId: entry.canonicalLevelId, title: entry.title }));
  await database.query(
    `INSERT INTO ranked_matches(id,player_a_id,player_b_id,config_snapshot_id,mmr_a_before,mmr_b_before,effective_rating_average,
       effective_tier,candidate_maps_snapshot,series_state,state,rules_version,match_type,ban_a_canonical_id,ban_b_canonical_id,
       selected_round_maps_snapshot,debug_config)
     VALUES($1,$2,$3,$4,1000,1000,1000,$5,$6::jsonb,$7::jsonb,'MATCH_RESULT','v1',$8,$9,$10,'[]'::jsonb,$11::jsonb)`,
    [id, ace, options.type === "DEBUG_BOT" ? bot : bee, snapshot, options.tier ?? "SILVER", JSON.stringify(candidates),
      JSON.stringify(options.mapBan === false ? { mapBan: false } : {}), options.type ?? "RANKED_PVP",
      options.ban?.[0] ?? null, options.ban?.[1] ?? null, options.type === "DEBUG_BOT" ? "{}" : null]);
  return id;
}
async function round(matchId: string, number: number, levelId: string, result: string | null, attempts: [string, number, number, boolean][]) {
  const id = uuid(++sequence);
  await database.query(
    `INSERT INTO ranked_rounds(id,match_id,round_number,level_id,canonical_level_id,playable_level_id,title,creator,difficulty,pool,
       qualifying_percent,phase,result)
     VALUES($1,$2,$3,$4,$4,$4,'t','c','Hard',1,40,'ROUND_RESULT',$5)`, [id, matchId, number, levelId, result]);
  let order = 0;
  for (const [player, score, progress, cleared] of attempts) {
    order += 1;
    const ended = new Date(Date.UTC(2026, 10, 7, 10, 0, order)).toISOString();
    await database.query(
      `INSERT INTO ranked_attempts(id,round_id,player_id,attempt_sequence,server_accepted_start_at,ended_at,level_id,played_level_id,
         progress_percent,cleared,awarded_score,domain_attempt_id,client_start_event_id)
       VALUES($1,$2,$3,$4,$5,$5,$6,$6,$7,$8,$9,$10,$10)`,
      [uuid(++sequence), id, player, order, ended, levelId, progress, cleared, score, `attempt-${sequence}`]);
  }
  return id;
}

beforeAll(async () => {
  pglite = new PGlite();
  database = new PgliteDatabase(pglite);
  for (const name of (await readdir(directory)).filter((file) => file.endsWith(".sql")).sort())
    await pglite.exec(await readFile(new URL(name, directory), "utf8"));
  await database.query(`INSERT INTO ranked_players(id,gd_account_id,gd_username,is_bot) VALUES ($1,1,'Ace',FALSE),($2,2,'Bee',FALSE),($3,-3,'Bot',TRUE)`, [ace, bee, bot]);
  await database.query(`INSERT INTO ranked_config_snapshots(id,generation,rules_version,source_payload,fetched_at)
    VALUES ($1,'g1','v1','{}'::jsonb,CURRENT_TIMESTAMP)`, [snapshot]);

  const m1 = await match({ ban: ["103", "103"], tier: "SILVER2" });
  // Zeta: Bee leads 120-0, Ace clears and wins 250-150 (a 150 point comeback).
  await round(m1, 1, "101", "A", [[bee, 120, 80, false], [ace, 0, 10, false], [bee, 30, 30, false], [ace, 250, 100, true]]);
  await round(m1, 2, "102", "DRAW", [[ace, 90, 45, false], [bee, 90, 45, false]]);
  const deathmatch = uuid(++sequence);
  await database.query(
    `INSERT INTO ranked_deathmatches(id,match_id,sequence,map_snapshot,winner_id,started_at,finished_at)
     VALUES($1,$2,1,$3::jsonb,$4,'2026-11-07T10:10:00Z','2026-11-07T10:12:05Z')`,
    [deathmatch, m1, JSON.stringify({ canonicalLevelId: "101" }), bee]);
  let order = 0;
  for (const [player, progress] of [[ace, 42], [bee, 30], [bee, 60]] as const) {
    order += 1;
    await database.query(
      `INSERT INTO ranked_deathmatch_attempts(id,deathmatch_id,player_id,attempt_sequence,server_accepted_start_at,ended_at,level_id,
         played_level_id,progress_percent,cleared,client_start_event_id)
       VALUES($1,$2,$3,$4,$5,$5,'9101','9101',$6,FALSE,$7)`,
      [uuid(++sequence), deathmatch, player, player === ace ? 1 : order - 1, new Date(Date.UTC(2026, 10, 7, 10, 11, order)).toISOString(), progress, `dm-${order}`]);
  }
  const m2 = await match({ ban: ["101", null], tier: "GOLD" });
  // Bee trails 0-60, then clears: another comeback, and a first-attempt clear.
  await round(m2, 1, "101", "B", [[ace, 60, 60, false], [bee, 250, 100, true]]);
  await round(m2, 2, "103", null, [[ace, 250, 100, true]]); // unsettled: never counted
  await match({ ban: ["102", null], mapBan: false }); // ban phase off: no offers
  // Bot matches count like any other match (round, offers, clear, record).
  const debug = await match({ type: "DEBUG_BOT" });
  await round(debug, 1, "101", "A", [[ace, 250, 100, true]]);
});
afterAll(async () => { await pglite.close(); });

describe("Solo map pool", () => {
  it("lists active maps easiest first with appearance, draw and ban rates", async () => {
    const service = new MapPoolService(config, database, clock);
    const list = await service.pool();
    expect(list.maps.map((entry) => entry.title)).toEqual(["Alpha", "Zeta", "Demon One"]);
    expect(list.totals).toEqual({ rounds: 4, draws: 1, drawRate: 25 });
    const zeta = list.maps.find((entry) => entry.canonicalLevelId === "101")!;
    expect(zeta).toMatchObject({ levelId: "9101", rounds: 3, share: 75, draws: 0, drawRate: 0, banOffers: 3, bans: 1, banRate: 33.3, difficultyRank: 5, gdDifficulty: "Insane", rating: 17 });
    const demon = list.maps.find((entry) => entry.canonicalLevelId === "103")!;
    expect(demon).toMatchObject({ rounds: 0, share: 0, drawRate: null, banOffers: 3, bans: 1 });
    expect(list.maps.find((entry) => entry.canonicalLevelId === "102")).toMatchObject({ rounds: 1, draws: 1, drawRate: 100, banOffers: 3, bans: 0 });
    expect(list.mostPlayed).toBe("101");
  });

  it("reports per-map statistics, comebacks and the viewer's own record", async () => {
    const service = new MapPoolService(config, database, clock);
    const detail = await service.detail("101", ace);
    expect(detail).toMatchObject({ shareRank: 1, mapCount: 3, mine: { rounds: 3, wins: 2, losses: 1, draws: 0, bestPercent: 100 } });
    expect(detail.stats).toMatchObject({
      rounds: 3, playerRounds: 6, clearRate: 50, firstAttemptClears: 2,
      comeback: { normal: { eligible: 3, count: 2, rate: 66.7 }, deathmatch: { eligible: 1, count: 1, rate: 100 } },
      deathmatches: 1, fastestClear: { attempt: 1, player: "Ace" }, longestDeathmatch: { seconds: 125, winner: "Bee" },
      tiers: [{ tier: "SILVER", rounds: 2, share: 66.7 }, { tier: "GOLD", rounds: 1, share: 33.3 }],
    });
    await expect(service.detail("104", ace)).rejects.toThrow(/not in the Solo map pool/);
  });

  it("serves the shared cache for 30 seconds", async () => {
    const service = new MapPoolService(config, database, clock);
    const first = await service.pool();
    const extra = await match();
    await round(extra, 1, "102", "A", []);
    expect((await service.pool()).generatedAt).toBe(first.generatedAt);
    now = new Date(now.getTime() + 30_000);
    const next = await service.pool();
    expect(next.totals.rounds).toBe(5);
    expect(next.generatedAt).not.toBe(first.generatedAt);
  });
});
