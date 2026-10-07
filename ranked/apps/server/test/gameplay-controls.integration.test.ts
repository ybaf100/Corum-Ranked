import { enterPreparedMatch } from "./native-entry-helper.js";
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
import { MatchRecoveryService } from "../src/match/match-recovery.service.js";
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
    "0013_native_scene_entry.sql",
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

const entryBody = (entry: { roundNumber: number; deathmatchSequence: number; playableLevelId: string }) => ({
  roundNumber: entry.roundNumber, deathmatchSequence: entry.deathmatchSequence, playableLevelId: entry.playableLevelId,
});

describe("persisted native scene entry and shared countdown", () => {
  it("waits for both native clients, retries idempotently and starts at the stored clock origin after restart", async () => {
    const f = await playingMatch(false), body = entryBody(f.state.entry);
    expect(f.state.state).toBe("ROUND_ENTERING");
    expect(f.state.entry).toMatchObject({ entered: { A: false, B: false }, startsAt: null });
    const untouched = await database.query("SELECT domain_state, started_at FROM ranked_rounds WHERE match_id = $1", [f.id]);
    expect(untouched.rows[0]).toEqual({ domain_state: null, started_at: null });
    await expect(f.matches.entered(f.id, f.ta, f.a, { ...body, playableLevelId: "wrong-map" })).rejects.toThrow("NATIVE_ENTRY_CONTEXT_CHANGED");
    await expect(f.matches.entered(f.id, "wrong-token", f.a, body)).rejects.toThrow();
    expect(await f.matches.startAttempt(f.id, f.ta, f.a, { levelId: body.playableLevelId, clientEventId: "before-entry" }))
      .toMatchObject({ accepted: false, reason: "ATTEMPT_START_NOT_ACCEPTED_IN_ROUND_ENTERING" });
    expect(await f.matches.startAttemptIntent(f.id, f.ta, f.a, { levelId: body.playableLevelId,
      clientEventId: "intent-before-entry", clientStartedAt: f.clock.now().toISOString() })).toMatchObject({ accepted: false });
    expect(await f.matches.changePause(f.id, f.ta, f.a, { leaseId: "pre-start", paused: true })).toMatchObject({ accepted: false });
    const first = await f.matches.entered(f.id, f.ta, f.a, body);
    expect(first.entry).toMatchObject({ entered: { A: true, B: false }, startsAt: null });
    expect((await f.matches.entered(f.id, f.ta, f.a, body)).stateVersion).toBe(first.stateVersion);
    f.clock.advanceSeconds(20);
    const retryReady = await f.matches.ready(f.id, f.ta, f.a, { installedMods: structuredClone(cleanMods) });
    expect(retryReady.entry).toEqual(first.entry); expect(retryReady.pause.remainingMs).toBe(30000);
    const countdown = await f.matches.entered(f.id, f.tb, f.b, body);
    const startsAtMs = Date.parse(countdown.entry.startsAt);
    expect(startsAtMs - f.clock.now().getTime()).toBe(3000);
    expect((await f.matches.entered(f.id, f.tb, f.b, body)).stateVersion).toBe(countdown.stateVersion);
    expect((await f.matches.entered(f.id, f.ta, f.a, body)).entry.startsAt).toBe(countdown.entry.startsAt);
    f.clock.advanceSeconds(2.999);
    expect((await f.matches.state(f.id, f.ta, f.a)).state).toBe("ROUND_ENTERING");
    expect(await f.matches.startAttempt(f.id, f.ta, f.a, { levelId: body.playableLevelId, clientEventId: "before-countdown" }))
      .toMatchObject({ accepted: false });
    const recovered = await new MatchRecoveryService(database, f.clock).recover();
    expect(recovered.resumed).toBeGreaterThan(0);
    const restarted = f.makeService();
    expect((await restarted.state(f.id, f.ta, f.a)).entry).toEqual(countdown.entry);
    f.clock.advanceSeconds(2);
    const playing = await restarted.state(f.id, f.tb, f.b);
    expect(playing.state).toBe("ROUND_PLAYING");
    expect(Date.parse(playing.deadlineAt!) - startsAtMs).toBe(f.document.operational.rules.roundSeconds * 1000);
    const persisted = await database.query<{ started_at: Date }>("SELECT started_at FROM ranked_rounds WHERE match_id = $1", [f.id]);
    expect(new Date(persisted.rows[0]!.started_at).getTime()).toBe(startsAtMs);
    expect((await restarted.entered(f.id, f.ta, f.a, body)).entry).toEqual(countdown.entry);
    expect(await restarted.startAttemptIntent(f.id, f.ta, f.a, { levelId: body.playableLevelId,
      clientEventId: "backdated-pre-countdown", clientStartedAt: new Date(startsAtMs - 1).toISOString() }))
      .toMatchObject({ accepted: false, reason: "ATTEMPT_INTENT_TIMESTAMP_BEFORE_COUNTDOWN" });
    expect(await restarted.startAttempt(f.id, f.ta, f.a, { levelId: body.playableLevelId, clientEventId: "real-start" }))
      .toMatchObject({ accepted: true, duplicate: false });
  });

  it("accepts pause at the shared GO time without a preceding playing-state poll", async () => {
    const f = await playingMatch(false), body = entryBody(f.state.entry);
    await f.matches.entered(f.id, f.ta, f.a, body);
    const countdown = await f.matches.entered(f.id, f.tb, f.b, body);
    f.clock.advanceSeconds(2.999);
    expect(await f.matches.changePause(f.id, f.ta, f.a, { leaseId: "too-early", paused: true })).toMatchObject({ accepted: false });
    f.clock.advanceSeconds(0.001);
    expect(await f.matches.changePause(f.id, f.ta, f.a, { leaseId: "at-go", paused: true }))
      .toMatchObject({ accepted: true, active: true, remainingMs: 30000 });
    const state = await f.matches.state(f.id, f.ta, f.a);
    expect(state.state).toBe("ROUND_PLAYING");
    expect(Date.parse(state.deadlineAt) - Date.parse(countdown.entry.startsAt)).toBe(f.document.operational.rules.roundSeconds * 1000);
  });

  it("adopts the synthetic bot only after the real A native scene enters", async () => {
    const f = await playingMatch(false), body = entryBody(f.state.entry);
    await database.query("UPDATE ranked_matches SET match_type = 'DEBUG_BOT', debug_config = '{}'::jsonb WHERE id = $1", [f.id]);
    await expect(f.matches.entered(f.id, f.tb, f.b, body)).rejects.toThrow("NATIVE_ENTRY_REQUIRES_REAL_PLAYER");
    f.clock.advanceSeconds(12);
    expect((await f.matches.state(f.id, f.ta, f.a)).entry).toMatchObject({ entered: { A: false, B: false }, startsAt: null });
    const countdown = await f.matches.entered(f.id, f.ta, f.a, body);
    expect(countdown.entry.entered).toEqual({ A: true, B: true });
    expect(Date.parse(countdown.entry.startsAt) - f.clock.now().getTime()).toBe(3000);
    f.clock.advanceSeconds(3);
    expect((await f.matches.state(f.id, f.ta, f.a)).state).toBe("ROUND_PLAYING");
  });

  it("cancels a bounded Round-1 entry wait without scoring an unplayed match", async () => {
    const f = await playingMatch(false);
    await f.matches.entered(f.id, f.ta, f.a, entryBody(f.state.entry));
    f.clock.advanceSeconds(60);
    const state = await f.matches.entered(f.id, f.tb, f.b, entryBody(f.state.entry));
    expect(state.state).toBe("CANCELLED"); expect(state.cancellation.reason).toBe("NATIVE_ENTRY_TIMEOUT");
    const row = await database.query("SELECT result_applied_at FROM ranked_matches WHERE id = $1", [f.id]);
    expect(row.rows[0]).toEqual({ result_applied_at: null });
  });

  it("clears the prior entry certificate and cannot revive it in the next round", async () => {
    const f = await playingMatch(), previous = entryBody(f.state.entry);
    await f.matches.surrender(f.id, f.ta, f.a, { scope: "ROUND", clientEventId: "old-round", roundNumber: 1, deathmatchSequence: 0 });
    f.clock.advanceSeconds(f.document.operational.timeouts!.roundResultSeconds + 1);
    const preparing = await f.matches.state(f.id, f.ta, f.a);
    expect(preparing.state).toBe("ROUND_PREPARE"); expect(preparing.entry).toBeNull();
    await expect(f.matches.entered(f.id, f.ta, f.a, previous)).rejects.toThrow("NATIVE_ENTRY_CONTEXT_CHANGED");
    await f.matches.ready(f.id, f.ta, f.a, { installedMods: structuredClone(cleanMods) });
    const entering = await f.matches.ready(f.id, f.tb, f.b, { installedMods: structuredClone(cleanMods) });
    expect(entering.entry).toMatchObject({ roundNumber: 2, entered: { A: false, B: false }, startsAt: null });
    await expect(f.matches.entered(f.id, f.ta, f.a, previous)).rejects.toThrow("NATIVE_ENTRY_CONTEXT_CHANGED");
    await f.matches.entered(f.id, f.tb, f.b, entryBody(entering.entry));
    f.clock.advanceSeconds(60);
    const forfeited = await f.matches.state(f.id, f.tb, f.b);
    expect(forfeited.state).toBe("MATCH_RESULT"); expect(forfeited.result.winnerSide).toBe("B");
  });
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
const playingMatch = async (enterScene = true) => {
  const clock = new MutableClock();
  const base = configDocumentFixture("gameplay-controls");
  const document = { ...base, operational: { ...base.operational,
    failurePolicy: { ...base.operational.failurePolicy!, restartRecoveryAction: "RESUME" as const },
    timeouts: { ...base.operational.timeouts!, reconnectGraceSeconds: 10_000 } } };
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
    const entering = await matches.ready(id, qb.matchToken, b, ready);
    if (!enterScene) return entering;
    return enterPreparedMatch(matches, id, qa.matchToken, a, qb.matchToken, b,
      (seconds) => clock.advanceSeconds(seconds));
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
