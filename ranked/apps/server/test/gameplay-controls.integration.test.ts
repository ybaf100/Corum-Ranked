import { enterPreparedMatch } from "./native-entry-helper.js";
import { readFile, readdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, afterEach, beforeAll, describe, expect, it, vi } from "vitest";
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
  for (const migrationName of (await readdir(new URL("../../../migrations/", import.meta.url))).filter(n => n.endsWith(".sql")).sort()) {
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
  prepareProfile = true,
): Promise<RankedSessionContext> => {
  const created = await sessions.create({
    gdAccountId: accountId,
    gdUsername: `Player${accountId}`,
    clientVersion: "v0.1.0",
    clientInstanceId: "test-process-instance",
    installedMods: structuredClone(cleanMods),
  });
  if (prepareProfile) await database.query(
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
const playingMatch = async (enterScene = true, reconnectGraceSeconds = 10_000) => {
  const clock = new MutableClock();
  // Match config is immutable by generation; fixtures with different reconnect
  // policies must not accidentally reuse an earlier fixture's 10,000s snapshot.
  const base = configDocumentFixture(`gameplay-controls-${account}`);
  const document = { ...base, operational: { ...base.operational,
    failurePolicy: { ...base.operational.failurePolicy!, restartRecoveryAction: "RESUME" as const },
    timeouts: { ...base.operational.timeouts!, reconnectGraceSeconds } } };
  const config = { withAdmission: async <T>(operation: () => Promise<T>): Promise<T> => operation(),
      getSnapshot: () => ({ ...structuredClone(document), fetchedAt: clock.now().toISOString() }) } as unknown as RankedConfigService;
  const tokens = new TokenService(environmentFixture());
  const sessions = new SessionService(database, new FixedCsmpSource(), clock, ids, config, tokens);
  const makeService = () => new MatchService(database, clock, ids, new SeededRandom(456),
    new MatchAccessService(tokens, clock), sessions, new OutboxService(ids),
    new AchievementService(database, clock, ids), new InMemoryMatchRuntimeState());
  const matches: any = makeService();
  const queue = new QueueService(database, clock, ids, new SeededRandom(123), config, sessions, tokens, matches);
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
  return { matches, makeService, queue, sessions, tokens, clock, document, a, b, id, ta: qa.matchToken, tb: qb.matchToken, state, startRound };
};

describe("queue admission after an abandoned match", () => {
  afterEach(async () => {
    // These rematch cases intentionally stop while queued. Each fixture starts
    // a fresh clock; do not pair a later fixture with a prior future-dated row.
    await database.query("DELETE FROM ranked_queue_entries WHERE status = 'QUEUED'");
  });
  it.each([91, 3601])("both disconnected players can rematch after %s seconds without polling their old match", async elapsed => {
    const f = await playingMatch(true, 20);
    const profilesBefore = await database.query("SELECT player_id, hidden_mmr, visible_ranked_score FROM ranked_profiles WHERE player_id IN ($1, $2) ORDER BY player_id", [f.a.playerId, f.b.playerId]);
    f.clock.advanceSeconds(elapsed); // process crash: no Close and no match/session heartbeat
    const a = await createContext(f.a.gdAccountId, f.sessions, f.tokens, false);
    const b = await createContext(f.b.gdAccountId, f.sessions, f.tokens, false);
    expect(a.sessionId).not.toBe(f.a.sessionId); expect(b.sessionId).not.toBe(f.b.sessionId);
    expect((await database.query("SELECT state FROM ranked_matches WHERE id = $1", [f.id])).rows[0]).toEqual({ state: "ROUND_PLAYING" });
    // Reproduce the old path: new sessions alone do not retire the match, and
    // Queue's unchanged active-match guard still rejects BOTH participants.
    const oldPath = vi.spyOn(f.matches, "settleActiveMatchesForPlayer").mockResolvedValue(undefined);
    try {
      await expect(f.queue.join(a, { installedMods: structuredClone(cleanMods) })).rejects.toThrow("Player already has an active Ranked match");
      await expect(f.queue.join(b, { installedMods: structuredClone(cleanMods) })).rejects.toThrow("Player already has an active Ranked match");
    } finally { oldPath.mockRestore(); }
    expect(await f.queue.join(a, { installedMods: structuredClone(cleanMods) })).toMatchObject({ status: "QUEUED" });
    const paired = await f.queue.join(b, { installedMods: structuredClone(cleanMods) });
    expect(paired.status).toBe("MATCHED"); expect(paired.matchId).not.toBe(f.id);
    const old = (await database.query("SELECT state, cancellation_reason, result_applied_at FROM ranked_matches WHERE id = $1", [f.id])).rows[0];
    expect(old).toMatchObject({ state: "CANCELLED", cancellation_reason: "RECONNECT_TIMEOUT", result_applied_at: null });
    expect(await database.query("SELECT player_id, hidden_mmr, visible_ranked_score FROM ranked_profiles WHERE player_id IN ($1, $2) ORDER BY player_id", [f.a.playerId, f.b.playerId])).toEqual(profilesBefore);
    const active = await database.query("SELECT id FROM ranked_matches WHERE (player_a_id IN ($1, $2) OR player_b_id IN ($1, $2)) AND state NOT IN ('MATCH_RESULT', 'CANCELLED')", [f.a.playerId, f.b.playerId]);
    expect(active.rows).toEqual([{ id: paired.matchId }]);
  });
  it("also releases both players after explicit Close without rating a cancelled match", async () => {
    const f = await playingMatch(true, 20);
    await f.sessions.close(f.a); await f.sessions.close(f.b);
    f.clock.advanceSeconds(20);
    const a = await createContext(f.a.gdAccountId, f.sessions, f.tokens, false);
    const b = await createContext(f.b.gdAccountId, f.sessions, f.tokens, false);
    expect(await f.queue.join(a, { installedMods: structuredClone(cleanMods) })).toMatchObject({ status: "QUEUED" });
    expect(await f.queue.join(b, { installedMods: structuredClone(cleanMods) })).toMatchObject({ status: "MATCHED" });
    expect((await database.query("SELECT state, result_applied_at FROM ranked_matches WHERE id = $1", [f.id])).rows[0]).toEqual({ state: "CANCELLED", result_applied_at: null });
  });
  it("simultaneous rematch requests retire the old match once and create only one replacement", async () => {
    const f = await playingMatch(true, 20);
    f.clock.advanceSeconds(91);
    const a = await createContext(f.a.gdAccountId, f.sessions, f.tokens, false);
    const b = await createContext(f.b.gdAccountId, f.sessions, f.tokens, false);
    const before = (await database.query<{ state_version: number }>("SELECT state_version FROM ranked_matches WHERE id = $1", [f.id])).rows[0]!.state_version;
    await Promise.all([f.queue.join(a, { installedMods: structuredClone(cleanMods) }), f.queue.join(b, { installedMods: structuredClone(cleanMods) })]);
    const active = await database.query<{ id: string }>("SELECT id FROM ranked_matches WHERE (player_a_id = $1 OR player_b_id = $1) AND state NOT IN ('MATCH_RESULT', 'CANCELLED')", [a.playerId]);
    expect(active.rows).toHaveLength(1); expect(active.rows[0]!.id).not.toBe(f.id);
    expect((await database.query("SELECT state, state_version FROM ranked_matches WHERE id = $1", [f.id])).rows[0]).toEqual({ state: "CANCELLED", state_version: before + 1 });
    await expect(f.queue.join(a, { installedMods: structuredClone(cleanMods) })).rejects.toThrow("active Ranked match");
    expect(await f.queue.status(a)).toMatchObject({ status: "MATCHED", matchId: active.rows[0]!.id }); // rejected admission cannot erase the match
  });
  it("does not revive match heartbeats or allow a second match inside the reconnect grace", async () => {
    const f = await playingMatch(true, 20);
    await f.matches.heartbeat(f.id, f.ta, f.a); await f.matches.heartbeat(f.id, f.tb, f.b);
    const before = await database.query("SELECT last_heartbeat_a_at, last_heartbeat_b_at FROM ranked_matches WHERE id = $1", [f.id]);
    f.clock.advanceSeconds(19.999);
    await expect(f.queue.join(f.a, { installedMods: structuredClone(cleanMods) })).rejects.toThrow("active Ranked match");
    await expect(f.queue.join(f.b, { installedMods: structuredClone(cleanMods) })).rejects.toThrow("active Ranked match");
    expect(await database.query("SELECT last_heartbeat_a_at, last_heartbeat_b_at FROM ranked_matches WHERE id = $1", [f.id])).toEqual(before);
    f.clock.advanceSeconds(0.001);
    expect(await f.queue.join(f.a, { installedMods: structuredClone(cleanMods) })).toMatchObject({ status: "QUEUED" });
    expect((await database.query("SELECT state FROM ranked_matches WHERE id = $1", [f.id])).rows[0]).toEqual({ state: "CANCELLED" });
  });
  it("settles a single disconnected player's forfeit once under the existing policy", async () => {
    const f = await playingMatch(true, 20);
    await f.matches.heartbeat(f.id, f.ta, f.a); await f.matches.heartbeat(f.id, f.tb, f.b);
    f.clock.advanceSeconds(19); await f.matches.heartbeat(f.id, f.tb, f.b);
    f.clock.advanceSeconds(1);
    expect(await f.queue.join(f.a, { installedMods: structuredClone(cleanMods) })).toMatchObject({ status: "QUEUED" });
    const result = await f.matches.state(f.id, f.tb, f.b);
    expect(result.state).toBe("MATCH_RESULT"); expect(result.result!.winnerSide).toBe("B");
    const once = await database.query("SELECT mmr_delta_a, mmr_delta_b, result_applied_at FROM ranked_matches WHERE id = $1", [f.id]);
    await f.queue.join(f.a, { installedMods: structuredClone(cleanMods) });
    expect(await database.query("SELECT mmr_delta_a, mmr_delta_b, result_applied_at FROM ranked_matches WHERE id = $1", [f.id])).toEqual(once);
  });
  it("preserves the prior full-match Pause expiry instead of overwriting it with a disconnect cancellation", async () => {
    const f = await playingMatch(true, 20);
    await f.matches.changePause(f.id, f.ta, f.a, { leaseId: "abandoned-paused", paused: true });
    f.clock.advanceSeconds(30);
    await f.queue.join(f.a, { installedMods: structuredClone(cleanMods) });
    const result = await f.matches.state(f.id, f.tb, f.b);
    expect(result.state).toBe("MATCH_RESULT"); expect(result.result!.winnerSide).toBe("B");
    expect(result.surrender).toMatchObject({ side: "A", scope: "MATCH", reason: "PAUSE_BUDGET_EXHAUSTED" });
  });
  it("settles the old match before starting another Debug Bot match", async () => {
    const f = await playingMatch(false, 20);
    f.clock.advanceSeconds(91);
    const a = await createContext(f.a.gdAccountId, f.sessions, f.tokens, false);
    const bot = await f.queue.createDebugBotMatch(a, { installedMods: structuredClone(cleanMods) }, {
      difficulty: "EASY", scenario: "NORMAL_MATCH", botBan: "NO_BAN", sendDiscordEvents: false, mmrOffset: -250,
    });
    expect(bot.matchId).not.toBe(f.id);
    expect((await database.query("SELECT state FROM ranked_matches WHERE id = $1", [f.id])).rows[0]).toEqual({ state: "CANCELLED" });
  });
});

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
    const result = await f.makeService().state(f.id, f.ta, f.a);
    expect(result.pause).toMatchObject({ remainingMs: 0, active: false });
    expect(result.state).toBe("MATCH_RESULT");
    expect(result.result!.winnerSide).toBe("B");
    expect(result.surrender).toMatchObject({ side: "A", scope: "MATCH", roundNumber: 2, reason: "PAUSE_BUDGET_EXHAUSTED" });
    await f.matches.changePause(f.id, f.ta, f.a, { leaseId: "pause-2", paused: false });
    expect((await f.makeService().state(f.id, f.ta, f.a)).pause.remainingMs).toBe(0);
    expect((await f.matches.state(f.id, f.tb, f.b)).pause.remainingMs).toBe(30000);
  });
  it("forfeits on the exact deadline even if Resume arrives before a state poll; late ACK/Clear cannot alter ratings", async () => {
    const f = await playingMatch(), levelId = f.state.currentRound.map.playableLevelId;
    const earned = await f.matches.startAttempt(f.id, f.ta, f.a, { levelId, clientEventId: "timeout-earned" });
    await f.matches.endAttempt(f.id, f.ta, f.a, { levelId, attemptId: earned.attemptId!, clientEventId: "timeout-earned-end", progressPercent: 60, cleared: false });
    const score = (await f.matches.state(f.id, f.ta, f.a)).currentRound!.scores.A;
    const run = await f.matches.startAttempt(f.id, f.ta, f.a, { levelId, clientEventId: "timeout-open" });
    await f.matches.changePause(f.id, f.ta, f.a, { leaseId: "timeout-race", paused: true });
    f.clock.advanceSeconds(29.999);
    expect((await f.matches.state(f.id, f.ta, f.a)).state).toBe("ROUND_PLAYING");
    f.clock.advanceSeconds(0.001);
    await f.matches.changePause(f.id, f.ta, f.a, { leaseId: "timeout-race", paused: false });
    const result = await f.matches.state(f.id, f.ta, f.a);
    expect(result.state).toBe("MATCH_RESULT"); expect(result.result!.winnerSide).toBe("B");
    expect(result.currentRound!.scores.A).toBe(score); expect(result.result!.perfectWinSide).toBeNull();
    expect(result.surrender).toMatchObject({ scope: "MATCH", reason: "PAUSE_BUDGET_EXHAUSTED" });
    const once = await database.query("SELECT mmr_delta_a, mmr_delta_b, result_applied_at FROM ranked_matches WHERE id = $1", [f.id]);
    const ack = await f.matches.surrender(f.id, f.ta, f.a, { scope: "MATCH", clientEventId: "automatic-client-ack", roundNumber: 1, deathmatchSequence: 0 });
    expect(ack.surrender).toEqual(result.surrender); expect(ack.result).toEqual(result.result);
    const late = await f.matches.endAttempt(f.id, f.ta, f.a, { levelId, attemptId: run.attemptId!, clientEventId: "timeout-late-clear", progressPercent: 100, cleared: true });
    expect(late.accepted).toBe(false);
    expect(await database.query("SELECT mmr_delta_a, mmr_delta_b, result_applied_at FROM ranked_matches WHERE id = $1", [f.id])).toEqual(once);
    expect((await f.makeService().state(f.id, f.tb, f.b)).surrender).toEqual(result.surrender);
  });
  it("a timely Resume closes the lease and cannot auto-forfeit while normal gameplay continues", async () => {
    const f = await playingMatch();
    await f.matches.changePause(f.id, f.tb, f.b, { leaseId: "timely-resume", paused: true });
    f.clock.advanceSeconds(29);
    expect(await f.matches.changePause(f.id, f.tb, f.b, { leaseId: "timely-resume", paused: false })).toMatchObject({ remainingMs: 1000, active: false });
    f.clock.advanceSeconds(5);
    expect((await f.matches.state(f.id, f.ta, f.a)).state).toBe("ROUND_PLAYING");
    await f.matches.changePause(f.id, f.tb, f.b, { leaseId: "last-second", paused: true });
    f.clock.advanceSeconds(1);
    const result = await f.matches.state(f.id, f.ta, f.a);
    expect(result.state).toBe("MATCH_RESULT"); expect(result.result!.winnerSide).toBe("A");
    expect(result.surrender).toMatchObject({ side: "B", scope: "MATCH" });
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
  it("Death Match Pause timeout also forfeits the whole match and cannot require another decisive attempt", async () => {
    const f = await playingMatch();
    for (let round = 1; round <= 3; ++round) {
      f.clock.advanceSeconds(f.document.operational.rules.roundSeconds + f.document.operational.rules.finalAttemptWindowSeconds + 5);
      const state = await f.matches.state(f.id, f.ta, f.a);
      if (round < 3) { f.clock.advanceSeconds(f.document.operational.timeouts!.roundResultSeconds + 1); await f.matches.state(f.id, f.ta, f.a); await f.startRound(); }
      else expect(state.state).toBe("DEATHMATCH_PREPARE");
    }
    expect((await f.startRound()).state).toBe("DEATHMATCH_PLAYING");
    await f.matches.changePause(f.id, f.ta, f.a, { leaseId: "dm-timeout", paused: true });
    f.clock.advanceSeconds(30);
    const result = await f.matches.state(f.id, f.ta, f.a);
    expect(result.state).toBe("MATCH_RESULT"); expect(result.result!.winnerSide).toBe("B");
    expect(result.surrender).toMatchObject({ scope: "MATCH", deathmatchSequence: 1, reason: "PAUSE_BUDGET_EXHAUSTED" });
    expect((await database.query(`SELECT a.* FROM ranked_deathmatch_attempts a JOIN ranked_deathmatches d ON d.id = a.deathmatch_id WHERE d.match_id = $1`, [f.id])).rows).toHaveLength(0);
  });
});
