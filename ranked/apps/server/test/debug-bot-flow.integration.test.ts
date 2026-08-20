import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { calculateMmrUpdate, SeededRandom, type CsmpTier } from "@corum-ranked/rules";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import type { IdGenerator, ServerClock } from "../src/common/runtime.module.js";
import { TokenService } from "../src/common/token.service.js";
import type { CsmpTierSource } from "../src/config/csmp-tier.source.js";
import type { RankedConfigService } from "../src/config/ranked-config.service.js";
import type { ServerEnvironment } from "../src/config/server-environment.js";
import { DebugBotMatchService } from "../src/debug-bot/debug-bot.service.js";
import type {
  DebugBotDifficulty,
  DebugBotScenario,
} from "../src/debug-bot/debug-bot.types.js";
import { MatchAccessService } from "../src/match/match-access.service.js";
import { InMemoryMatchRuntimeState } from "../src/match/match-runtime-state.js";
import { MatchService } from "../src/match/match.service.js";
import { OutboxService } from "../src/relay/outbox.service.js";
import type { CreateSessionDto } from "../src/session/session.dto.js";
import { SessionService } from "../src/session/session.service.js";
import type { RankedSessionContext } from "../src/session/session.types.js";
import { configDocumentFixture, environmentFixture } from "./fixtures.js";
import { PgliteDatabase } from "./pglite-database.js";

let pglite: PGlite;
let database: PgliteDatabase;
let clock: MutableClock;
let sessions: SessionService;
let tokens: TokenService;
let matches: MatchService;
let debugMatches: DebugBotMatchService;

class SequenceIds implements IdGenerator {
  private value = 20_000;

  public next(): string {
    this.value += 1;
    return `00000000-0000-4000-8005-${String(this.value).padStart(12, "0")}`;
  }
}

class MutableClock implements ServerClock {
  private milliseconds = Date.parse("2026-08-20T18:00:00.000Z");

  public now(): Date {
    return new Date(this.milliseconds);
  }

  public advanceMs(milliseconds: number): void {
    this.milliseconds += milliseconds;
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
    version: "v0.4.0-alpha.3",
    enabled: true,
    loaded: true,
    internal: false,
    system: false,
  },
  {
    id: "syzzi.click_between_frames",
    version: "v1.5.0",
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

const copyMods = (): CreateSessionDto["installedMods"] => structuredClone(cleanMods);

const createContext = async (accountId: string): Promise<RankedSessionContext> => {
  const created = await sessions.create({
    gdAccountId: accountId,
    gdUsername: `Debug${accountId}`,
    clientVersion: "v0.4.0-alpha.3",
    installedMods: copyMods(),
  });
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

interface CreatedDebugMatch {
  readonly matchId: string;
  readonly matchToken: string;
  readonly debug: {
    readonly botRating: number;
    readonly ratingOffset: number;
  };
}

const createDebugMatch = async (
  human: RankedSessionContext,
  scenario: DebugBotScenario,
  difficulty: DebugBotDifficulty = "NORMAL",
): Promise<CreatedDebugMatch> => debugMatches.create(human, {
  password: "2008",
  difficulty,
  scenario,
  botBan: "NO_BAN",
  sendDiscordEvents: false,
});

const stateFor = async (
  created: CreatedDebugMatch,
  human: RankedSessionContext,
): Promise<Record<string, any>> => matches.state(
  created.matchId,
  created.matchToken,
  human,
) as Promise<Record<string, any>>;

const enterRoundOne = async (
  created: CreatedDebugMatch,
  human: RankedSessionContext,
): Promise<Record<string, any>> => {
  await debugMatches.tickMatchNow(created.matchId);
  let state = await matches.ready(created.matchId, created.matchToken, human, {
    installedMods: copyMods(),
  }) as Record<string, any>;
  expect(state.state).toBe("BAN_PHASE");
  expect(state.candidateMaps).toHaveLength(5);
  await debugMatches.tickMatchNow(created.matchId);
  state = await matches.submitBan(created.matchId, created.matchToken, human, {}) as Record<string, any>;
  expect(state.state).toBe("ROUND_PREPARE");
  await debugMatches.tickMatchNow(created.matchId);
  state = await matches.ready(created.matchId, created.matchToken, human, {
    installedMods: copyMods(),
  }) as Record<string, any>;
  expect(state.state).toBe("ROUND_PLAYING");
  return state;
};

const enterNextRound = async (
  created: CreatedDebugMatch,
  human: RankedSessionContext,
): Promise<Record<string, any>> => {
  clock.advanceMs(1_100);
  await debugMatches.tickMatchNow(created.matchId);
  const state = await matches.ready(created.matchId, created.matchToken, human, {
    installedMods: copyMods(),
  }) as Record<string, any>;
  expect(state.state).toBe("ROUND_PLAYING");
  return state;
};

const advanceBotUntil = async (
  created: CreatedDebugMatch,
  human: RankedSessionContext,
  predicate: (state: Record<string, any>) => boolean,
  maximumTicks = 100,
): Promise<Record<string, any>> => {
  let state = await stateFor(created, human);
  for (let tick = 0; tick < maximumTicks && !predicate(state); tick += 1) {
    clock.advanceMs(200);
    await debugMatches.tickMatchNow(created.matchId);
    state = await stateFor(created, human);
  }
  return state;
};

const winCurrentRound = async (
  created: CreatedDebugMatch,
  human: RankedSessionContext,
): Promise<Record<string, any>> => {
  const before = await stateFor(created, human);
  const levelId = before.currentRound.map.playableLevelId as string;
  expect(levelId).toBe(before.currentRound.map.alternateLevelId);
  for (let clear = 1; clear <= 2; clear += 1) {
    const started = await matches.startAttempt(created.matchId, created.matchToken, human, {
      levelId,
      clientEventId: `human-win-r${before.currentRound.roundNumber}-${clear}-start`,
    });
    await matches.endAttempt(created.matchId, created.matchToken, human, {
      levelId,
      attemptId: started.attemptId!,
      clientEventId: `human-win-r${before.currentRound.roundNumber}-${clear}-end`,
      progressPercent: 100,
      cleared: true,
    });
  }
  return stateFor(created, human);
};

beforeAll(async () => {
  pglite = new PGlite();
  database = new PgliteDatabase(pglite);
  const migrationPath = fileURLToPath(
    new URL("../../../migrations/0001_initial_ranked.sql", import.meta.url),
  );
  await pglite.exec(await readFile(migrationPath, "utf8"));

  const base = configDocumentFixture("debug-bot-flow");
  const document = {
    ...base,
    operational: {
      ...base.operational,
      timeouts: {
        ...base.operational.timeouts!,
        readySeconds: 30,
        reconnectGraceSeconds: 100_000,
        roundResultSeconds: 1,
      },
    },
    fetchedAt: "2026-08-20T18:00:00.000Z",
  };
  const config = { getSnapshot: () => structuredClone(document) } as RankedConfigService;
  const ids = new SequenceIds();
  clock = new MutableClock();
  const environment: ServerEnvironment = {
    ...environmentFixture(),
    debugBotMatch: { password: "2008" },
  };
  tokens = new TokenService(environment);
  sessions = new SessionService(
    database,
    new FixedCsmpSource(),
    clock,
    ids,
    config,
    tokens,
  );
  const random = new SeededRandom(8_008);
  matches = new MatchService(
    database,
    clock,
    ids,
    random,
    new MatchAccessService(tokens, clock),
    sessions,
    new OutboxService(ids),
    new InMemoryMatchRuntimeState(),
  );
  debugMatches = new DebugBotMatchService(
    database,
    clock,
    ids,
    random,
    environment,
    config,
    sessions,
    tokens,
    matches,
  );
}, 20_000);

afterAll(async () => {
  debugMatches.onApplicationShutdown();
  await pglite.close();
});

describe("development-only Debug Bot Match", () => {
  it("rejects an incorrect server password", async () => {
    const human = await createContext("98001");
    await expect(debugMatches.create(human, {
      password: "wrong",
      difficulty: "NORMAL",
      scenario: "NORMAL_MATCH",
      botBan: "NO_BAN",
      sendDiscordEvents: false,
    })).rejects.toThrow("Incorrect debug password");
  });

  it("applies a Bot loss through normal MMR, placement, stats, score, and history", async () => {
    const human = await createContext("98002");
    const before = await database.query<Record<string, unknown>>(
      "SELECT * FROM ranked_profiles WHERE player_id = $1",
      [human.playerId],
    );
    const created = await createDebugMatch(human, "FORCE_BOT_TWO_CLEARS");
    expect(created.debug.botRating).toBe(human.hiddenMmr);
    expect(created.debug.ratingOffset).toBe(0);
    expect((await database.query("SELECT * FROM ranked_queue_entries")).rowCount).toBe(0);
    await enterRoundOne(created, human);

    await debugMatches.tickMatchNow(created.matchId);
    let state = await stateFor(created, human);
    expect(state.matchType).toBe("DEBUG_BOT");
    expect(state.players.B.gdUsername).toBe("BOT");
    expect(state.currentRound.clears.B).toBe(1);
    expect(state.currentRound.scores.B).toBeGreaterThan(0);
    await debugMatches.tickMatchNow(created.matchId);
    state = await stateFor(created, human);
    expect(state.currentRound.clears.B).toBe(2);
    expect(state.state).toBe("ROUND_RESULT");

    state = await enterNextRound(created, human);
    expect(state.currentRound.banner).toBe("MATCH_POINT");
    await debugMatches.tickMatchNow(created.matchId);
    await debugMatches.tickMatchNow(created.matchId);
    state = await stateFor(created, human);
    expect(state.state).toBe("MATCH_RESULT");
    expect(state.result.winnerSide).toBe("B");
    const expected = calculateMmrUpdate(
      {
        ratingA: human.hiddenMmr,
        ratingB: created.debug.botRating,
        placementGamesA: 0,
        placementGamesB: 0,
        winner: "B",
      },
      configDocumentFixture().operational.mmrPolicy!,
    );
    expect(state.result.mmrDelta.A).toBe(expected.deltaA);
    expect(state.result.ratingAfter.A).toBe(expected.ratingAfterA);

    const after = await database.query<Record<string, unknown>>(
      "SELECT * FROM ranked_profiles WHERE player_id = $1",
      [human.playerId],
    );
    expect(after.rows[0]).toMatchObject({
      hidden_mmr: expected.ratingAfterA,
      visible_ranked_score: expected.ratingAfterA,
      displayed_tier: "UNRANKED",
      placement_games: 1,
      wins: 0,
      losses: 1,
      match_draws: 0,
      initial_csmp_tier: "BRONZE",
      initial_seed_mmr: human.hiddenMmr,
    });
    expect(after.rows[0]?.seed_applied_at).toEqual(before.rows[0]?.seed_applied_at);
    const attempts = await database.query<{ count: number; score: number }>(
      `SELECT COUNT(*)::int AS count, COALESCE(SUM(a.awarded_score), 0)::int AS score
       FROM ranked_attempts a
       JOIN ranked_rounds r ON r.id = a.round_id
       JOIN ranked_matches m ON m.id = r.match_id
       WHERE m.id = $1 AND a.player_id = m.player_b_id`,
      [created.matchId],
    );
    expect(Number(attempts.rows[0]?.count)).toBe(4);
    expect(Number(attempts.rows[0]?.score)).toBeGreaterThan(0);
    const persistedMaps = await database.query<{
      canonical_level_id: string;
      alternate_level_id: string | null;
      playable_level_id: string;
      played_level_id: string;
    }>(
      `SELECT DISTINCT
         r.canonical_level_id, r.alternate_level_id,
         r.playable_level_id, a.played_level_id
       FROM ranked_attempts a
       JOIN ranked_rounds r ON r.id = a.round_id
       WHERE r.match_id = $1`,
      [created.matchId],
    );
    expect(persistedMaps.rows.length).toBeGreaterThan(0);
    expect(
      persistedMaps.rows.every(
        (map) =>
          map.alternate_level_id === map.playable_level_id &&
          map.played_level_id === map.playable_level_id &&
          map.canonical_level_id !== map.playable_level_id,
      ),
    ).toBe(true);
    expect((await database.query<{ id: string }>(
      "SELECT id FROM ranked_public_match_history WHERE id = $1",
      [created.matchId],
    )).rows.map((row) => row.id)).toEqual([created.matchId]);
    expect((await database.query(
      "SELECT id FROM ranked_outbox_events WHERE aggregate_id = $1",
      [created.matchId],
    )).rowCount).toBe(0);
    expect((await database.query("SELECT * FROM ranked_queue_entries")).rowCount).toBe(0);
  });

  it("applies a Bot win with the shared rating function and updates the leaderboard", async () => {
    const human = await createContext("98007");
    await database.query(
      `UPDATE ranked_profiles
       SET hidden_mmr = 2995, visible_ranked_score = 2995,
           placement_games = 4, displayed_tier = 'UNRANKED'
       WHERE player_id = $1`,
      [human.playerId],
    );
    (human as { hiddenMmr: number }).hiddenMmr = 2995;
    (human as { placementGames: number }).placementGames = 4;
    const created = await createDebugMatch(human, "NORMAL_MATCH", "HARD");
    expect(created.debug.botRating).toBe(human.hiddenMmr + 200);
    expect(created.debug.ratingOffset).toBe(200);
    await enterRoundOne(created, human);
    let state = await winCurrentRound(created, human);
    expect(state.state).toBe("ROUND_RESULT");
    await enterNextRound(created, human);
    state = await winCurrentRound(created, human);
    expect(state.state).toBe("MATCH_RESULT");
    expect(state.result.winnerSide).toBe("A");

    const expected = calculateMmrUpdate(
      {
        ratingA: human.hiddenMmr,
        ratingB: created.debug.botRating,
        placementGamesA: 4,
        placementGamesB: 4,
        winner: "A",
      },
      configDocumentFixture().operational.mmrPolicy!,
    );
    expect(state.result).toMatchObject({
      mmrDelta: { A: expected.deltaA },
      ratingAfter: { A: expected.ratingAfterA },
    });
    const profile = await database.query<Record<string, unknown>>(
      "SELECT * FROM ranked_profiles WHERE player_id = $1",
      [human.playerId],
    );
    expect(profile.rows[0]).toMatchObject({
      hidden_mmr: expected.ratingAfterA,
      visible_ranked_score: expected.ratingAfterA,
      displayed_tier: "SILVER",
      placement_games: 5,
      wins: 1,
      losses: 0,
    });
    const leaderboard = await database.query<Record<string, unknown>>(
      "SELECT * FROM ranked_leaderboard WHERE gd_account_id = $1",
      [human.gdAccountId],
    );
    expect(leaderboard.rows[0]).toMatchObject({
      visible_ranked_score: expected.ratingAfterA,
      wins: 1,
      losses: 0,
    });
  });

  it("shows real spectator telemetry and supports multiple failed LAST ATTEMPT starts", async () => {
    const human = await createContext("98003");
    const created = await createDebugMatch(human, "TRIGGER_LAST_ATTEMPT", "EASY");
    expect(created.debug.ratingOffset).toBe(-200);
    expect(created.debug.botRating).toBe(human.hiddenMmr - 200);
    await enterRoundOne(created, human);
    await debugMatches.tickMatchNow(created.matchId);
    await debugMatches.tickMatchNow(created.matchId);
    await debugMatches.tickMatchNow(created.matchId);
    let state = await stateFor(created, human);
    expect(state.state).toBe("LAST_ATTEMPT_WINDOW");
    expect(state.currentRound.clears).toEqual({ A: 2, B: 1 });
    expect(state.spectator.active).toBe(true);

    await debugMatches.tickMatchNow(created.matchId);
    clock.advanceMs(200);
    await debugMatches.tickMatchNow(created.matchId);
    state = await stateFor(created, human);
    expect(state.spectator.active).toBe(true);
    expect(state.spectator.currentProgress).toBeGreaterThan(0);

    state = await advanceBotUntil(
      created,
      human,
      (current) => current.state === "ROUND_RESULT",
      100,
    );
    expect(state.currentRound.outcome.result).toBe("A");
    expect(state.currentRound.outcome.reason).toBe("LAST_ATTEMPT_EXPIRED");
    const starts = await database.query<{ count: number }>(
      `SELECT COUNT(*)::int AS count
       FROM ranked_attempts a
       JOIN ranked_rounds r ON r.id = a.round_id
       JOIN ranked_matches m ON m.id = r.match_id
       WHERE m.id = $1 AND r.round_number = 1 AND a.player_id = m.player_b_id`,
      [created.matchId],
    );
    expect(Number(starts.rows[0]?.count)).toBeGreaterThanOrEqual(3);
  });

  it("forces a Bot LAST ATTEMPT clear through the normal engine to create a Round Draw", async () => {
    const human = await createContext("98004");
    const created = await createDebugMatch(human, "TRIGGER_ROUND_DRAW");
    await enterRoundOne(created, human);
    await debugMatches.tickMatchNow(created.matchId);
    await debugMatches.tickMatchNow(created.matchId);
    await debugMatches.tickMatchNow(created.matchId);
    const state = await advanceBotUntil(
      created,
      human,
      (current) => current.state === "ROUND_RESULT",
      40,
    );
    expect(state.currentRound.outcome.result).toBe("DRAW");
    expect(state.currentRound.outcome.reason).toBe("LAST_ATTEMPT_CLEAR");
  });

  it("reaches Round 3 with TIEBREAKER and then the real three-attempt Deathmatch flow", async () => {
    const roundThreeHuman = await createContext("98005");
    const roundThree = await createDebugMatch(roundThreeHuman, "TRIGGER_ROUND_THREE");
    await enterRoundOne(roundThree, roundThreeHuman);
    await debugMatches.tickMatchNow(roundThree.matchId);
    await debugMatches.tickMatchNow(roundThree.matchId);
    await enterNextRound(roundThree, roundThreeHuman);
    await debugMatches.tickMatchNow(roundThree.matchId);
    await debugMatches.tickMatchNow(roundThree.matchId);
    const tiebreaker = await enterNextRound(roundThree, roundThreeHuman);
    expect(tiebreaker.currentRound.roundNumber).toBe(3);
    expect(tiebreaker.currentRound.banner).toBe("TIEBREAKER");

    const deathmatchHuman = await createContext("98006");
    const deathmatch = await createDebugMatch(deathmatchHuman, "TRIGGER_DEATHMATCH", "HARD");
    await enterRoundOne(deathmatch, deathmatchHuman);
    await debugMatches.tickMatchNow(deathmatch.matchId);
    await debugMatches.tickMatchNow(deathmatch.matchId);
    await enterNextRound(deathmatch, deathmatchHuman);
    await debugMatches.tickMatchNow(deathmatch.matchId);
    await debugMatches.tickMatchNow(deathmatch.matchId);
    await enterNextRound(deathmatch, deathmatchHuman);
    await debugMatches.tickMatchNow(deathmatch.matchId);
    await debugMatches.tickMatchNow(deathmatch.matchId);
    await debugMatches.tickMatchNow(deathmatch.matchId);
    let state = await advanceBotUntil(
      deathmatch,
      deathmatchHuman,
      (current) => current.state === "ROUND_RESULT",
      40,
    );
    expect(state.currentRound.outcome.result).toBe("DRAW");
    clock.advanceMs(1_100);
    await debugMatches.tickMatchNow(deathmatch.matchId);
    state = await matches.ready(deathmatch.matchId, deathmatch.matchToken, deathmatchHuman, {
      installedMods: copyMods(),
    }) as Record<string, any>;
    expect(state.state).toBe("DEATHMATCH_PLAYING");
    expect(state.deathmatch.sequence).toBe(1);
    const deathmatchLevelId = state.deathmatch.map.playableLevelId as string;
    expect(deathmatchLevelId).toBe(state.deathmatch.map.alternateLevelId);

    for (let attempt = 1; attempt <= 3; attempt += 1) {
      const started = await matches.startAttempt(deathmatch.matchId, deathmatch.matchToken, deathmatchHuman, {
        levelId: deathmatchLevelId,
        clientEventId: `human-deathmatch-${attempt}-start`,
      });
      await matches.endAttempt(deathmatch.matchId, deathmatch.matchToken, deathmatchHuman, {
        levelId: deathmatchLevelId,
        attemptId: started.attemptId!,
        clientEventId: `human-deathmatch-${attempt}-end`,
        progressPercent: 1,
        cleared: false,
      });
    }
    state = await advanceBotUntil(
      deathmatch,
      deathmatchHuman,
      (current) => current.state === "MATCH_RESULT",
      50,
    );
    expect(state.state).toBe("MATCH_RESULT");
    expect(state.result.winnerSide).toBe("B");
    const botAttempts = await database.query<{ count: number }>(
      `SELECT COUNT(*)::int AS count
       FROM ranked_deathmatch_attempts a
       JOIN ranked_deathmatches d ON d.id = a.deathmatch_id
       JOIN ranked_matches m ON m.id = d.match_id
       WHERE m.id = $1 AND a.player_id = m.player_b_id`,
      [deathmatch.matchId],
    );
    expect(Number(botAttempts.rows[0]?.count)).toBe(3);
  }, 20_000);
});
