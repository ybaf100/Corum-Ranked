import { readFile, readdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { SeededRandom } from "@corum-ranked/rules";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { AchievementService } from "../src/achievement/achievement.service.js";
import type { RankedConfigService } from "../src/config/ranked-config.service.js";
import { TokenService } from "../src/common/token.service.js";
import { PlacementService } from "../src/placement/placement.service.js";
import { SessionService } from "../src/session/session.service.js";
import type { RankedSessionContext } from "../src/session/session.types.js";
import { configDocumentFixture, environmentFixture } from "./fixtures.js";
import { PgliteDatabase } from "./pglite-database.js";

let pglite: PGlite;
let database: PgliteDatabase;
beforeAll(async () => {
  pglite = new PGlite(); database = new PgliteDatabase(pglite);
  const directory = fileURLToPath(new URL("../../../migrations/", import.meta.url));
  for (const name of (await readdir(directory)).filter(name => name.endsWith(".sql")).sort()) {
    await pglite.exec(await readFile(`${directory}/${name}`, "utf8"));
  }
}, 60000);
afterAll(async () => { await pglite.close(); });
let fixtureIndex = 0;
const mods = [
  { id: "hwanhee1.corum_ranked", version: "0.5.0-beta.14", enabled: true, loaded: true, internal: false, system: false },
  { id: "syzzi.click_between_frames", version: "1.5.0", enabled: true, loaded: true, internal: false, system: false,
    settings: { "soft-toggle": false, "click-on-steps": false, "physics-bypass": false } },
  { id: "geode.node-ids", version: "v1.23.3", enabled: true, loaded: true, internal: false, system: false },
];
async function fixture(stageCount: 3 | 5, pool: 1 | 6) {
  const index = ++fixtureIndex; let sequence = 80000 + index * 1000;
  const ids = { next: () => `00000000-0000-4000-8021-${String(++sequence).padStart(12, "0")}` };
  let time = Date.parse("2026-08-30T00:00:00Z");
  const clock = { now: () => new Date(time), advance: (seconds: number) => { time += seconds * 1000; } };
  const document = configDocumentFixture(`placement-reform-${index}`);
  const bands = document.operational.tierBands.map((band, i) => ({ ...band,
    minInclusive: i * 300, maxExclusive: i === 4 ? null : (i + 1) * 300 }));
  const snapshot = { ...structuredClone(document), operational: { ...document.operational, tierBands: bands },
    fetchedAt: clock.now().toISOString() };
  const config = { getSnapshot: () => structuredClone(snapshot) } as unknown as RankedConfigService;
  const tokens = new TokenService(environmentFixture());
  const sessions = new SessionService(database, { fetchCurrentTier: async () => "BRONZE" as const }, clock, ids, config, tokens);
  const created = await sessions.create({ gdAccountId: String(29000 + index), gdUsername: `Reform${index}`,
    clientVersion: "v0.5.0-beta.14", clientInstanceId: `reform-${index}`, installedMods: structuredClone(mods) });
  const rows = await database.query<{ session_id: string; player_id: string }>(
    "SELECT id AS session_id, player_id FROM ranked_sessions WHERE token_hash = $1", [tokens.hash(created.sessionToken)]);
  const session: RankedSessionContext = { sessionId: rows.rows[0]!.session_id, playerId: rows.rows[0]!.player_id,
    gdAccountId: String(29000 + index), gdUsername: `Reform${index}`, displayedTier: "UNRANKED", hiddenMmr: 0, placementGames: 0 };
  const makeService = () => new PlacementService(database, clock, ids, new SeededRandom(index), config, sessions,
    new AchievementService(database, clock, ids));
  let service = makeService();
  const initial = await service.start(session, { startingPool: pool, stageCount, installedMods: structuredClone(mods) });
  const runId = initial.runId;
  const prepare = async () => {
    await service.ready(runId, session, { installedMods: structuredClone(mods) }); clock.advance(10);
    return service.state(runId, session);
  };
  let event = 0;
  const skipBody = (state: Awaited<ReturnType<PlacementService["state"]>>) => ({
    stageIndex: state.currentStageIndex, mapIndex: state.currentMapIndex,
    levelId: state.currentMap!.playableLevelId, clientEventId: `skip-${++event}`,
  });
  const finishMaps = async (clears: boolean, beforeFinalize?: () => Promise<void>) => {
    for (let stage = 1; stage <= stageCount; ++stage) {
      for (let map = 1; map <= 2; ++map) {
        const state = await prepare();
        if (clears) {
          for (let n = 0; n < 2; ++n) {
            const start = await service.startAttempt(runId, session, {
              levelId: state.currentMap!.playableLevelId, clientEventId: `start-${++event}` });
            await service.endAttempt(runId, session, { levelId: state.currentMap!.playableLevelId,
              attemptId: start.attemptId, clientEventId: `end-${++event}`, progressPercent: 100, cleared: true });
          }
        } else await service.skipMap(runId, session, skipBody(state));
        await service.continueResult(runId, session);
      }
      if (stage === stageCount && beforeFinalize) await beforeFinalize();
      await service.continueResult(runId, session);
    }
    return service.state(runId, session);
  };
  return { session, runId, initial, clock, snapshot, prepare, skipBody, finishMaps,
    get service() { return service; }, recover: () => { service = makeService(); } };
}

describe("Placement normalization and exact map skip", () => {
  it.each([[3, 1, 200], [3, 6, 550], [5, 1, 250], [5, 6, 750]] as const)(
    "%s stages pool %s finalizes at %s LP and exactly the same MMR", async (stages, pool, lp) => {
      const f = await fixture(stages, pool); expect(f.initial.ratingPolicyVersion).toBe("NORMALIZED_V1");
      f.recover(); const final = await f.finishMaps(pool === 6);
      expect(final.finalResult).toMatchObject({ visibleRankedScore: lp, placed: true, skipped: false });
      const persisted = await database.query<{ placement_rating: number; final_hidden_mmr: number; hidden_mmr: number; visible_ranked_score: number }>(
        `SELECT r.placement_rating::float AS placement_rating, r.final_hidden_mmr, p.hidden_mmr, p.visible_ranked_score
         FROM ranked_placement_runs r JOIN ranked_profiles p ON p.player_id = r.player_id WHERE r.id = $1`, [f.runId]);
      expect(persisted.rows[0]).toMatchObject({ placement_rating: pool === 1 ? -750 : 5500,
        final_hidden_mmr: lp, hidden_mmr: lp, visible_ranked_score: lp });
      const repeated = await f.service.continueResult(f.runId, f.session);
      expect(repeated.finalResult).toEqual(final.finalResult);
    }, 30000);

  it("saves the frozen attempt, skips one map, and cannot replay into the next map or another player", async () => {
    const f = await fixture(3, 1); const state = await f.prepare(); const body = f.skipBody(state);
    const attempt = await f.service.startAttempt(f.runId, f.session, { levelId: body.levelId, clientEventId: "active" });
    await f.service.updateAttemptProgress(f.runId, f.session, { levelId: body.levelId, attemptId: attempt.attemptId, progressPercent: 75 });
    await expect(f.service.skipMap(f.runId, f.session, body)).rejects.toThrow("Sync the active");
    await f.service.endAttempt(f.runId, f.session, { levelId: body.levelId, attemptId: attempt.attemptId,
      clientEventId: "frozen-end", progressPercent: 75, cleared: false });
    const skipped = await f.service.skipMap(f.runId, f.session, body);
    expect(skipped).toMatchObject({ accepted: true, state: "MAP_RESULT", currentStageIndex: 1, currentMapIndex: 1,
      currentMap: { mapScore: 150, mapGrade: "C" } });
    await f.service.continueResult(f.runId, f.session); f.recover();
    const replay = await f.service.skipMap(f.runId, f.session, body);
    expect(replay).toMatchObject({ duplicate: true, state: "MAP_PREPARE", currentStageIndex: 1, currentMapIndex: 2 });
    expect(replay.currentMap?.mapScore).toBe(0);
    await expect(f.service.skipMap(f.runId, f.session, { ...body, mapIndex: 2 })).rejects.toThrow("another map");
    await expect(f.service.skipMap(f.runId, { ...f.session, playerId: "00000000-0000-4000-8021-000000001111" } as RankedSessionContext, body)).rejects.toThrow();
  });

  it("applies the tier boundary adjustment before storing either LP or MMR", async () => {
    const f = await fixture(5, 6);
    const final = await f.finishMaps(true, async () => {
      // Persisted completed-map evidence yields raw R=3625 -> 600 LP.
      await database.query("UPDATE ranked_placement_maps SET pool = 5, map_score = 150, map_grade = 'C' WHERE run_id = $1", [f.runId]);
    });
    expect(final.finalResult).toMatchObject({ visibleRankedScore: 584, displayedTier: "AQUA" });
    const profile = await database.query<{ hidden_mmr: number }>("SELECT hidden_mmr FROM ranked_profiles WHERE player_id = $1", [f.session.playerId]);
    expect(profile.rows[0]?.hidden_mmr).toBe(584);
  }, 30000);

  it("retains the original run policy after config changes and server recovery", async () => {
    const f = await fixture(3, 6);
    f.snapshot.operational = { ...f.snapshot.operational, placementPolicy: { ...f.snapshot.operational.placementPolicy!,
      poolRatingAnchors: [0, 10, 20, 30, 40, 50] } };
    f.recover(); const final = await f.finishMaps(true);
    expect(final.finalResult?.visibleRankedScore).toBe(550);
  }, 30000);

  it("keeps legacy in-progress runs on the original cap and does not re-rate completed LP", async () => {
    const f = await fixture(3, 6);
    await database.query("UPDATE ranked_placement_runs SET rating_policy_version = 'LEGACY_CAP_V1' WHERE id = $1", [f.runId]);
    f.recover(); const final = await f.finishMaps(true);
    expect(final.ratingPolicyVersion).toBe("LEGACY_CAP_V1");
    expect(final.finalResult?.visibleRankedScore).toBe(584);
    await database.query("UPDATE ranked_profiles SET hidden_mmr = 1234, visible_ranked_score = 888 WHERE player_id = $1", [f.session.playerId]);
    await f.service.continueResult(f.runId, f.session);
    const row = await database.query<{ hidden_mmr: number; visible_ranked_score: number }>("SELECT hidden_mmr, visible_ranked_score FROM ranked_profiles WHERE player_id = $1", [f.session.playerId]);
    expect(row.rows[0]).toEqual({ hidden_mmr: 1234, visible_ranked_score: 888 });
  }, 30000);
});
