import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { SeededRandom, type CsmpTier } from "@corum-ranked/rules";
import type { IdGenerator, ServerClock } from "../src/common/runtime.module.js";
import { TokenService } from "../src/common/token.service.js";
import type { CsmpTierSource } from "../src/config/csmp-tier.source.js";
import type { RankedConfigService } from "../src/config/ranked-config.service.js";
import { MatchAccessService } from "../src/match/match-access.service.js";
import { InMemoryMatchRuntimeState } from "../src/match/match-runtime-state.js";
import { MatchService } from "../src/match/match.service.js";
import { QueueService } from "../src/queue/queue.service.js";
import { OutboxService } from "../src/relay/outbox.service.js";
import type { CreateSessionDto } from "../src/session/session.dto.js";
import { SessionService } from "../src/session/session.service.js";
import type { RankedSessionContext } from "../src/session/session.types.js";
import { configDocumentFixture, environmentFixture } from "./fixtures.js";
import { PgliteDatabase } from "./pglite-database.js";
import { AchievementService } from "../src/achievement/achievement.service.js";

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
    "0008_placement_beta9.sql",
    "0009_placement_stage_count.sql",
    "0010_achievements_v1.sql",
    "0012_gameplay_controls.sql",
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
  private value = 1_000;
  public next(): string {
    this.value += 1;
    return `00000000-0000-4000-8012-${String(this.value).padStart(12, "0")}`;
  }
}

class MutableClock implements ServerClock {
  private milliseconds = Date.parse("2026-08-20T02:00:00.000Z");
  public now(): Date {
    return new Date(this.milliseconds);
  }
  public advanceSeconds(seconds: number): void {
    this.milliseconds += seconds * 1_000;
  }
}

class FixedCsmpSource implements CsmpTierSource {
  public async fetchCurrentTier(): Promise<CsmpTier> {
    return "BRONZE";
  }
}

const cleanMods: CreateSessionDto["installedMods"] = [
  {
    id: "hwanhee1.corum_ranked",
    version: "0.1.0",
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
  {
    id: "geode.node-ids",
    version: "v1.23.3",
    enabled: true,
    loaded: true,
    internal: false,
    system: false,
  },
];

const createContext = async (
  accountId: string,
  sessions: SessionService,
  tokens: TokenService,
): Promise<RankedSessionContext> => {
  const created = await sessions.create({
    gdAccountId: accountId,
    gdUsername: `Player${accountId}`,
    clientVersion: "v0.1.0",
    installedMods: structuredClone(cleanMods),
  });
  await database.query(
    `UPDATE ranked_profiles
     SET placement_games = 5, displayed_tier = 'BRONZE', visible_ranked_score = hidden_mmr
     WHERE player_id = (
       SELECT player_id FROM ranked_sessions WHERE token_hash = $1
     )`,
    [tokens.hash(created.sessionToken)],
  );
  const result = await database.query<{
    session_id: string;
    player_id: string;
    gd_username: string;
    displayed_tier: RankedSessionContext["displayedTier"];
    hidden_mmr: number;
    placement_games: number;
  }>(
    `SELECT s.id AS session_id, p.id AS player_id, p.gd_username,
            rp.displayed_tier, rp.hidden_mmr, rp.placement_games
     FROM ranked_sessions s
     JOIN ranked_players p ON p.id = s.player_id
     JOIN ranked_profiles rp ON rp.player_id = p.id
     WHERE s.token_hash = $1`,
    [tokens.hash(created.sessionToken)],
  );
  const row = result.rows[0]!;
  return {
    sessionId: row.session_id,
    playerId: row.player_id,
    gdAccountId: accountId,
    gdUsername: row.gd_username,
    displayedTier: row.displayed_tier,
    hiddenMmr: Number(row.hidden_mmr),
    placementGames: Number(row.placement_games),
  };
};


const ids = new SequenceIds();
let account = 95000;
const playingMatch = async () => {
  const clock = new MutableClock();
  const base = configDocumentFixture("gameplay-controls");
  const document = { ...base, operational: { ...base.operational, timeouts: { ...base.operational.timeouts!, reconnectGraceSeconds: 10_000 } } };
  const config = { getSnapshot: () => ({ ...structuredClone(document), fetchedAt: clock.now().toISOString() }) } as unknown as RankedConfigService;
  const tokens = new TokenService(environmentFixture());
  const sessions = new SessionService(database, new FixedCsmpSource(), clock, ids, config, tokens);
  const queue = new QueueService(database, clock, ids, new SeededRandom(123), config, sessions, tokens);
  const makeService = () => new MatchService(database, clock, ids, new SeededRandom(456),
    new MatchAccessService(tokens, clock), sessions, new OutboxService(ids),
    new AchievementService(database, clock, ids), new InMemoryMatchRuntimeState());
  const matches: any = makeService();
  const a = await createContext(String(++account), sessions, tokens), b = await createContext(String(++account), sessions, tokens);
  await queue.join(a, { installedMods: structuredClone(cleanMods) }); await queue.join(b, { installedMods: structuredClone(cleanMods) });
  const qa = await queue.status(a) as { matchId: string; matchToken: string }, qb = await queue.status(b) as { matchId: string; matchToken: string };
  const id = qa.matchId;
  const ready = { installedMods: structuredClone(cleanMods) };
  await matches.ready(id, qa.matchToken, a, ready); await matches.ready(id, qb.matchToken, b, ready);
  await matches.submitBan(id, qa.matchToken, a, {}); await matches.submitBan(id, qb.matchToken, b, {});
  const startRound = async () => {
    await matches.ready(id, qa.matchToken, a, ready);
    return await matches.ready(id, qb.matchToken, b, ready) as any;
  };
  const state = await startRound();
  return { matches, makeService, clock, document, a, b, id, ta: qa.matchToken, tb: qb.matchToken, state, startRound };
};

describe("match-wide pause and explicit concession", () => {
  it("charges 30 seconds per player across rounds, retries, stale Ends and server restart without changing deadlines", async () => {
    const f = await playingMatch(); const deadline = f.state.deadlineAt;
    await f.matches.changePause(f.id, f.ta, f.a, { leaseId: "pause-1", paused: true });
    f.clock.advanceSeconds(12);
    const end = await f.matches.changePause(f.id, f.ta, f.a, { leaseId: "pause-1", paused: false });
    expect(end.remainingMs).toBe(18000);
    expect((await f.matches.state(f.id, f.ta, f.a)).deadlineAt).toBe(deadline);
    expect((await f.matches.changePause(f.id, f.ta, f.a, { leaseId: "pause-1", paused: true })).active).toBe(false);
    await f.matches.surrender(f.id, f.ta, f.a, { scope: "ROUND", clientEventId: "round-1", roundNumber: 1, deathmatchSequence: 0 });
    f.clock.advanceSeconds(f.document.operational.timeouts!.roundResultSeconds + 1);
    expect((await f.matches.state(f.id, f.ta, f.a)).state).toBe("ROUND_PREPARE"); await f.startRound();
    expect((await f.matches.changePause(f.id, f.ta, f.a, { leaseId: "pause-2", paused: true })).remainingMs).toBe(18000);
    await f.matches.changePause(f.id, f.ta, f.a, { leaseId: "pause-1", paused: false });
    f.clock.advanceSeconds(18);
    expect((await f.matches.state(f.id, f.ta, f.a)).pause).toMatchObject({ remainingMs: 0, active: false });
    await f.matches.changePause(f.id, f.ta, f.a, { leaseId: "pause-2", paused: false });
    expect((await f.makeService().state(f.id, f.ta, f.a)).pause.remainingMs).toBe(0);
    expect((await f.matches.state(f.id, f.tb, f.b)).pause.remainingMs).toBe(30000);
  });
  it("round concession preserves committed scores, closes active runs/leases and cannot concede the next round on ACK retry", async () => {
    const f = await playingMatch(); const levelId = f.state.currentRound.map.playableLevelId;
    const start = await f.matches.startAttempt(f.id, f.ta, f.a, { levelId, clientEventId: "earned" });
    await f.matches.endAttempt(f.id, f.ta, f.a, { levelId, attemptId: start.attemptId!, clientEventId: "earned-end", progressPercent: 60, cleared: false });
    const score = (await f.matches.state(f.id, f.ta, f.a)).currentRound!.scores.A;
    await f.matches.startAttempt(f.id, f.ta, f.a, { levelId, clientEventId: "active-a" });
    await f.matches.startAttempt(f.id, f.tb, f.b, { levelId, clientEventId: "active-b" });
    const body = { scope: "ROUND" as const, clientEventId: "round-concede", roundNumber: 1, deathmatchSequence: 0 };
    const conceded = await f.matches.surrender(f.id, f.ta, f.a, body);
    expect(conceded.state).toBe("ROUND_RESULT"); expect(conceded.series.roundWins).toEqual({ A: 0, B: 1 });
    expect(conceded.currentRound!.scores.A).toBe(score); expect(conceded.currentRound!.outcome.reason).toBe("SURRENDER");
    const active = await database.query(`SELECT * FROM ranked_attempts a JOIN ranked_rounds r ON r.id = a.round_id WHERE r.match_id = $1 AND a.ended_at IS NULL`, [f.id]);
    expect(active.rows).toHaveLength(0);
    f.clock.advanceSeconds(f.document.operational.timeouts!.roundResultSeconds + 1);
    await f.matches.state(f.id, f.ta, f.a); await f.startRound();
    const retried = await f.matches.surrender(f.id, f.ta, f.a, body);
    expect(retried.state).toBe("ROUND_PLAYING"); expect(retried.currentRound!.roundNumber).toBe(2);
    await expect(f.matches.surrender(f.id, f.ta, f.a, { ...body, clientEventId: "late-old-context" })).rejects.toThrow("SURRENDER_CONTEXT_CHANGED");
  });
  it("whole-match concession applies normal ratings once and rejects late gameplay/Clear evidence", async () => {
    const f = await playingMatch(); const levelId = f.state.currentRound.map.playableLevelId;
    const run = await f.matches.startAttempt(f.id, f.ta, f.a, { levelId, clientEventId: "active-clear" });
    const body = { scope: "MATCH" as const, clientEventId: "match-concede", roundNumber: 1, deathmatchSequence: 0 };
    const result = await f.matches.surrender(f.id, f.ta, f.a, body);
    expect(result.state).toBe("MATCH_RESULT"); expect(result.result!.winnerSide).toBe("B"); expect(result.result!.perfectWinSide).toBeNull();
    const once = await database.query("SELECT mmr_delta_a, mmr_delta_b, result_applied_at FROM ranked_matches WHERE id = $1", [f.id]);
    expect((await f.matches.surrender(f.id, f.ta, f.a, body)).state).toBe("MATCH_RESULT");
    const late = await f.matches.endAttempt(f.id, f.ta, f.a, { levelId, attemptId: run.attemptId!, clientEventId: "late-clear", progressPercent: 100, cleared: true });
    expect(late.accepted).toBe(false);
    expect(await database.query("SELECT mmr_delta_a, mmr_delta_b, result_applied_at FROM ranked_matches WHERE id = $1", [f.id])).toEqual(once);
    await expect(f.matches.changePause(f.id, "wrong-token", f.a, { leaseId: "unauthorized", paused: true })).rejects.toThrow();
  });
  it("Death Match round concession ends the decisive match without fabricating three Attempts", async () => {
    const f = await playingMatch();
    for (let round = 1; round <= 3; ++round) {
      f.clock.advanceSeconds(f.document.operational.rules.roundSeconds + f.document.operational.rules.finalAttemptWindowSeconds + 5);
      let state = await f.matches.state(f.id, f.ta, f.a);
      if (round < 3) { f.clock.advanceSeconds(f.document.operational.timeouts!.roundResultSeconds + 1); await f.matches.state(f.id, f.ta, f.a); await f.startRound(); }
      else expect(state.state).toBe("DEATHMATCH_PREPARE");
    }
    const dm = await f.startRound(); expect(dm.state).toBe("DEATHMATCH_PLAYING");
    const result = await f.matches.surrender(f.id, f.ta, f.a, { scope: "ROUND", clientEventId: "dm-concede", roundNumber: 3, deathmatchSequence: 1 });
    expect(result.state).toBe("MATCH_RESULT"); expect(result.result!.winnerSide).toBe("B");
    expect((await database.query(`SELECT a.* FROM ranked_deathmatch_attempts a JOIN ranked_deathmatches d ON d.id = a.deathmatch_id WHERE d.match_id = $1`, [f.id])).rows).toHaveLength(0);
  });
});
