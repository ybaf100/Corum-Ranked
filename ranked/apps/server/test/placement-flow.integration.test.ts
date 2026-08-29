import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { SeededRandom, type CsmpTier } from "@corum-ranked/rules";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import type { IdGenerator, ServerClock } from "../src/common/runtime.module.js";
import { TokenService } from "../src/common/token.service.js";
import type { CsmpTierSource } from "../src/config/csmp-tier.source.js";
import type { RankedConfigService } from "../src/config/ranked-config.service.js";
import { PlacementService } from "../src/placement/placement.service.js";
import { SessionService } from "../src/session/session.service.js";
import type { CreateSessionDto } from "../src/session/session.dto.js";
import type { RankedSessionContext } from "../src/session/session.types.js";
import { configDocumentFixture, environmentFixture } from "./fixtures.js";
import { PgliteDatabase } from "./pglite-database.js";

let pglite: PGlite;
let database: PgliteDatabase;

beforeAll(async () => {
  pglite = new PGlite();
  database = new PgliteDatabase(pglite);
  for (const migrationName of [
    "0001_initial_ranked.sql",
    "0002_attempt_start_leases.sql",
    "0002_debug_bot_match.sql",
    "0003_debug_rating_playable_maps.sql",
    "0004_alpha37_schema_hardening.sql",
    "0005_v050_rating_system.sql",
    "0006_v050_dynamic_csmp_tiers.sql",
    "0007_solo_stage_placement.sql",
  ]) {
    const migrationPath = fileURLToPath(
      new URL(`../../../migrations/${migrationName}`, import.meta.url),
    );
    await pglite.exec(await readFile(migrationPath, "utf8"));
  }
}, 60_000);

afterAll(async () => {
  await pglite.close();
});

class SequenceIds implements IdGenerator {
  private value = 10_000;
  public next(): string {
    this.value += 1;
    return `00000000-0000-4000-8020-${String(this.value).padStart(12, "0")}`;
  }
}

class MutableClock implements ServerClock {
  private current = new Date("2026-08-30T00:00:00.000Z");
  public now(): Date { return new Date(this.current); }
  public advanceSeconds(seconds: number): void {
    this.current = new Date(this.current.getTime() + seconds * 1_000);
  }
}

class FixedCsmpSource implements CsmpTierSource {
  public async fetchCurrentTier(): Promise<CsmpTier> { return "BRONZE"; }
}

const cleanMods: CreateSessionDto["installedMods"] = [
  {
    id: "hwanhee1.corum_ranked",
    version: "0.5.0-beta.8",
    enabled: true,
    loaded: true,
    internal: false,
    system: false,
  },
  {
    id: "syzzi.click_between_frames",
    version: "1.5.0",
    enabled: true,
    loaded: true,
    internal: false,
    system: false,
    settings: {
      "soft-toggle": false,
      "click-on-steps": false,
      "physics-bypass": false,
    },
  },
];

describe("server-authoritative solo Stage Placement", () => {
  it("resumes every persisted boundary and finalizes ten Maps exactly once", async () => {
    const document = configDocumentFixture("placement-flow");
    const snapshot = { ...structuredClone(document), fetchedAt: "2026-08-30T00:00:00.000Z" };
    const config = { getSnapshot: () => structuredClone(snapshot) } as unknown as RankedConfigService;
    const ids = new SequenceIds();
    const clock = new MutableClock();
    const tokens = new TokenService(environmentFixture());
    const sessions = new SessionService(
      database,
      new FixedCsmpSource(),
      clock,
      ids,
      config,
      tokens,
    );
    const placement = new PlacementService(
      database,
      clock,
      ids,
      new SeededRandom(500),
      config,
      sessions,
    );
    const created = await sessions.create({
      gdAccountId: "9901",
      gdUsername: "PlacementPlayer",
      clientVersion: "v0.5.0-beta.8",
      installedMods: structuredClone(cleanMods),
    });
    const contextRow = await database.query<{
      session_id: string;
      player_id: string;
      gd_username: string;
      displayed_tier: RankedSessionContext["displayedTier"];
      hidden_mmr: number;
      placement_games: number;
    }>(
      `SELECT s.id AS session_id, p.id AS player_id, p.gd_username,
              r.displayed_tier, r.hidden_mmr, r.placement_games
       FROM ranked_sessions s
       JOIN ranked_players p ON p.id = s.player_id
       JOIN ranked_profiles r ON r.player_id = p.id
       WHERE s.token_hash = $1`,
      [tokens.hash(created.sessionToken)],
    );
    const row = contextRow.rows[0]!;
    const session: RankedSessionContext = {
      sessionId: row.session_id,
      playerId: row.player_id,
      gdAccountId: "9901",
      gdUsername: row.gd_username,
      displayedTier: row.displayed_tier,
      hiddenMmr: Number(row.hidden_mmr),
      placementGames: Number(row.placement_games),
    };

    let state = await placement.start(session, {
      startingPool: 3,
      installedMods: structuredClone(cleanMods),
    }) as Record<string, any>;
    const runId = state.runId as string;
    expect(state).toMatchObject({ state: "MAP_PREPARE", currentStageIndex: 1, currentMapIndex: 1 });
    expect(await placement.active(session)).toMatchObject({
      active: true,
      action: "RESUME_PLACEMENT",
      runId,
    });

    state = await placement.ready(runId, session, {
      installedMods: structuredClone(cleanMods),
    }) as Record<string, any>;
    const firstLevel = state.currentMap.playableLevelId as string;
    const first = await placement.startAttempt(runId, session, {
      levelId: firstLevel,
      clientEventId: "first-50-start",
    });
    const progress50 = await placement.updateAttemptProgress(runId, session, {
      levelId: firstLevel,
      attemptId: first.attemptId,
      progressPercent: 50,
    });
    expect(progress50).toMatchObject({ mapScore: 0, mapGrade: "D", gradeChanged: false });
    const firstEnd = await placement.endAttempt(runId, session, {
      levelId: firstLevel,
      attemptId: first.attemptId,
      clientEventId: "first-50-end",
      progressPercent: 50,
      cleared: false,
    });
    expect(firstEnd).toMatchObject({ mapScore: 75, mapGrade: "D", gradeChanged: false });

    const active = await placement.startAttempt(runId, session, {
      levelId: firstLevel,
      clientEventId: "active-disconnect-start",
    });
    const activeProgress = await placement.updateAttemptProgress(runId, session, {
      levelId: firstLevel,
      attemptId: active.attemptId,
      progressPercent: 75,
    });
    expect(activeProgress).toMatchObject({ mapScore: 75, mapGrade: "D", gradeChanged: false });
    clock.advanceSeconds(311);

    // A new service instance models a process restart. The active Attempt ID
    // and aggregate state come exclusively from PostgreSQL.
    const recovered = new PlacementService(
      database,
      clock,
      ids,
      new SeededRandom(501),
      config,
      sessions,
    );
    state = await recovered.state(runId, session) as Record<string, any>;
    expect(state).toMatchObject({
      state: "MAP_SETTLING",
      activeAttempt: { attemptId: active.attemptId },
      currentMap: { mapScore: 75, mapGrade: "D" },
    });
    const heartbeatAfterRecovery = await database.query<{ last_heartbeat_at: string }>(
      "SELECT last_heartbeat_at FROM ranked_placement_runs WHERE id = $1",
      [runId],
    );
    expect(new Date(heartbeatAfterRecovery.rows[0]!.last_heartbeat_at).toISOString())
      .toBe(clock.now().toISOString());
    const settled = await recovered.endAttempt(runId, session, {
      levelId: firstLevel,
      attemptId: active.attemptId,
      clientEventId: "active-disconnect-end",
      progressPercent: 75,
      cleared: false,
    });
    expect(settled).toMatchObject({
      mapScore: 225,
      mapGrade: "C",
      gradeChanged: true,
      gradeTransition: "UP",
      mapCompleted: true,
    });
    expect(await recovered.state(runId, session)).toMatchObject({ state: "MAP_RESULT" });

    const completeThreeClears = async (): Promise<Record<string, any>> => {
      let current = await recovered.ready(runId, session, {
        installedMods: structuredClone(cleanMods),
      }) as Record<string, any>;
      const levelId = current.currentMap.playableLevelId as string;
      for (let clear = 0; clear < 3; clear += 1) {
        const started = await recovered.startAttempt(runId, session, {
          levelId,
          clientEventId: `s${current.currentStageIndex}-m${current.currentMapIndex}-c${clear}-start`,
        });
        const progress = await recovered.updateAttemptProgress(runId, session, {
          levelId,
          attemptId: started.attemptId,
          progressPercent: 100,
        });
        expect(progress.gradeChanged).toBe(false);
        await recovered.endAttempt(runId, session, {
          levelId,
          attemptId: started.attemptId,
          clientEventId: `s${current.currentStageIndex}-m${current.currentMapIndex}-c${clear}-end`,
          progressPercent: 100,
          cleared: true,
        });
      }
      current = await recovered.state(runId, session) as Record<string, any>;
      expect(current).toMatchObject({
        state: "MAP_RESULT",
        currentMap: { mapScore: 750, mapGrade: "S", clearCount: 3 },
      });
      return current;
    };

    // First Map result survives reconnect; move to Map 2 and finish Stage 1.
    await recovered.continueResult(runId, session);
    await completeThreeClears();
    state = await recovered.continueResult(runId, session) as Record<string, any>;
    expect(state).toMatchObject({ state: "STAGE_RESULT", currentStageIndex: 1 });
    expect(state.stageResults[0]).toMatchObject({
      mapScores: [225, 750],
      mapGrades: ["C", "S"],
      stageScore: 487.5,
      stageGrade: "B",
      poolDelta: 0,
      nextPool: 3,
    });

    // STAGE_RESULT also resumes after a process restart because no presentation
    // timer owns the transition.
    const stageRecovered = new PlacementService(
      database,
      clock,
      ids,
      new SeededRandom(502),
      config,
      sessions,
    );
    expect(await stageRecovered.state(runId, session)).toMatchObject({ state: "STAGE_RESULT" });
    await stageRecovered.continueResult(runId, session);

    for (let stage = 2; stage <= 5; stage += 1) {
      await completeThreeClears();
      await recovered.continueResult(runId, session);
      await completeThreeClears();
      state = await recovered.continueResult(runId, session) as Record<string, any>;
      expect(state).toMatchObject({ state: "STAGE_RESULT", currentStageIndex: stage });
      if (stage < 5) await recovered.continueResult(runId, session);
    }

    const finalized = await recovered.continueResult(runId, session) as Record<string, any>;
    expect(finalized).toMatchObject({
      state: "PLACEMENT_RESULT",
      finalResult: { placed: true },
    });
    expect(finalized.finalResult).not.toHaveProperty("placementRating");
    expect(finalized.finalResult).not.toHaveProperty("hiddenMmr");
    expect(finalized.finalResult.visibleRankedScore).toBeGreaterThanOrEqual(0);
    const beforeRepeat = await database.query<{
      finalized_at: string;
      hidden_mmr: number;
      visible_ranked_score: number;
      placement_games: number;
      wins: number;
      losses: number;
    }>(
      `SELECT pr.finalized_at, rp.hidden_mmr, rp.visible_ranked_score,
              rp.placement_games, rp.wins, rp.losses
       FROM ranked_placement_runs pr
       JOIN ranked_profiles rp ON rp.player_id = pr.player_id
       WHERE pr.id = $1`,
      [runId],
    );
    await recovered.continueResult(runId, session);
    const afterRepeat = await database.query<typeof beforeRepeat.rows[number]>(
      `SELECT pr.finalized_at, rp.hidden_mmr, rp.visible_ranked_score,
              rp.placement_games, rp.wins, rp.losses
       FROM ranked_placement_runs pr
       JOIN ranked_profiles rp ON rp.player_id = pr.player_id
       WHERE pr.id = $1`,
      [runId],
    );
    expect(afterRepeat.rows[0]).toEqual(beforeRepeat.rows[0]);
    expect(afterRepeat.rows[0]).toMatchObject({ placement_games: 5, wins: 0, losses: 0 });
    expect(await recovered.active(session)).toMatchObject({
      active: true,
      action: "RESUME_PLACEMENT",
      state: "PLACEMENT_RESULT",
    });
    expect(await recovered.acknowledgeResult(runId, session)).toMatchObject({
      acknowledged: true,
    });
    expect(await recovered.active(session)).toMatchObject({
      active: false,
      action: "RANKED_QUEUE",
    });
    const evidence = await database.query<{ maps: number; attempts: number }>(
      `SELECT
         (SELECT COUNT(*)::int FROM ranked_placement_maps WHERE run_id = $1) AS maps,
         (SELECT COUNT(*)::int FROM ranked_placement_attempts WHERE run_id = $1) AS attempts`,
      [runId],
    );
    expect(evidence.rows[0]).toEqual({ maps: 10, attempts: 29 });
  }, 30_000);
});
