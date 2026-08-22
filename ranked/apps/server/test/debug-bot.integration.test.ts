import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import {
  SeededRandom,
  calculateMmrUpdate,
  type CsmpTier,
  type DisplayTier,
} from "@corum-ranked/rules";
import type { IdGenerator, ServerClock } from "../src/common/runtime.module.js";
import { TokenService } from "../src/common/token.service.js";
import type { CsmpTierSource } from "../src/config/csmp-tier.source.js";
import type { RankedConfigService } from "../src/config/ranked-config.service.js";
import type { ServerEnvironment } from "../src/config/server-environment.js";
import { DebugBotService } from "../src/debug-bot/debug-bot.service.js";
import { MatchAccessService } from "../src/match/match-access.service.js";
import { ATTEMPT_TRANSPORT_GRACE_MS } from "../src/match/attempt-timing.js";
import { InMemoryMatchRuntimeState } from "../src/match/match-runtime-state.js";
import { MatchService } from "../src/match/match.service.js";
import { QueueService, type DebugBotMatchSettings } from "../src/queue/queue.service.js";
import { OutboxService } from "../src/relay/outbox.service.js";
import type { CreateSessionDto } from "../src/session/session.dto.js";
import { SessionService } from "../src/session/session.service.js";
import type { RankedSessionContext } from "../src/session/session.types.js";
import { configDocumentFixture, environmentFixture } from "./fixtures.js";
import { PgliteDatabase } from "./pglite-database.js";

let pglite: PGlite;
let database: PgliteDatabase;

class SequenceIds implements IdGenerator {
  private value = 8_000;
  public next(): string {
    this.value += 1;
    return `00000000-0000-4000-8030-${String(this.value).padStart(12, "0")}`;
  }
}

class MutableClock implements ServerClock {
  private milliseconds = Date.parse("2026-08-20T04:00:00.000Z");
  public now(): Date {
    return new Date(this.milliseconds);
  }
  public advance(milliseconds: number): void {
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
    version: "v0.4.0-alpha.15",
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

beforeAll(async () => {
  pglite = new PGlite();
  database = new PgliteDatabase(pglite);
  for (const migrationName of ["0001_initial_ranked.sql", "0002_attempt_start_leases.sql"]) {
    const path = fileURLToPath(new URL(`../../../migrations/${migrationName}`, import.meta.url));
    await pglite.exec(await readFile(path, "utf8"));
  }
}, 60_000);

afterAll(async () => {
  await pglite.close();
});

describe("development-only Bot Match using the production Ranked engine", () => {
  const ids = new SequenceIds();
  const clock = new MutableClock();
  const random = new SeededRandom(20260820);
  const document = configDocumentFixture("debug-bot");
  const snapshot = { ...document, fetchedAt: "2026-08-20T04:00:00.000Z" };
  const config = { getSnapshot: () => structuredClone(snapshot) } as RankedConfigService;
  const environment: ServerEnvironment = {
    ...environmentFixture(),
    debugBot: {
      password: "2008",
      tickMs: 125,
      attemptDelayMs: 200,
      difficulties: {
        EASY: { mmrOffset: -250, qualifyingChance: 0.3, clearChance: 0, progressPerSecond: 20 },
        NORMAL: { mmrOffset: 0, qualifyingChance: 0.6, clearChance: 0.1, progressPerSecond: 25 },
        HARD: { mmrOffset: 250, qualifyingChance: 0.8, clearChance: 0.2, progressPerSecond: 35 },
      },
    },
  };
  let tokens: TokenService;
  let sessions: SessionService;
  let queue: QueueService;
  let matches: MatchService;
  let debug: DebugBotService;

  beforeAll(() => {
    tokens = new TokenService(environment);
    sessions = new SessionService(database, new FixedCsmpSource(), clock, ids, config, tokens);
    queue = new QueueService(database, clock, ids, random, config, sessions, tokens);
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
    debug = new DebugBotService(environment, database, clock, random, queue, matches, config, tokens);
  });

  const contextFor = async (accountId: string): Promise<RankedSessionContext> => {
    const created = await sessions.create({
      gdAccountId: accountId,
      gdUsername: `Debug${accountId}`,
      clientVersion: "v0.4.0-alpha.15",
      installedMods: structuredClone(cleanMods),
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
              r.displayed_tier, r.hidden_mmr, r.placement_games
       FROM ranked_sessions s
       JOIN ranked_players p ON p.id = s.player_id
       JOIN ranked_profiles r ON r.player_id = p.id
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

  const completeTwoClears = async (
    creation: Awaited<ReturnType<QueueService["createDebugBotMatch"]>>,
    playerWins: boolean,
  ) => {
    const state = await matches.state(
      creation.matchId,
      creation.playerMatchToken,
      creation.playerContext,
    ) as Record<string, any>;
    const levelId = state.currentRound.map.playableLevelId as string;
    const context = playerWins ? creation.playerContext : creation.botContext;
    const token = playerWins ? creation.playerMatchToken : creation.botMatchToken;
    for (let index = 0; index < 2; index += 1) {
      const started = await matches.startAttempt(creation.matchId, token, context, {
        levelId,
        clientEventId: `rating-${creation.matchId}-${index}-start`,
      });
      await matches.endAttempt(creation.matchId, token, context, {
        levelId,
        attemptId: started.attemptId!,
        clientEventId: `rating-${creation.matchId}-${index}-end`,
        progressPercent: 100,
        cleared: true,
      });
    }
  };

  const runRatedMatch = async (
    accountId: string,
    playerWins: boolean,
    profileOverride?: {
      hiddenMmr: number;
      placementGames: number;
      displayedTier: DisplayTier;
    },
  ) => {
    let player = await contextFor(accountId);
    if (profileOverride) {
      await database.query(
        `UPDATE ranked_profiles
         SET hidden_mmr = $2, visible_ranked_score = $2,
             placement_games = $3, displayed_tier = $4
         WHERE player_id = $1`,
        [
          player.playerId,
          profileOverride.hiddenMmr,
          profileOverride.placementGames,
          profileOverride.displayedTier,
        ],
      );
      player = {
        ...player,
        hiddenMmr: profileOverride.hiddenMmr,
        placementGames: profileOverride.placementGames,
        displayedTier: profileOverride.displayedTier,
      };
    }
    const settings: DebugBotMatchSettings = {
      difficulty: playerWins ? "EASY" : "HARD",
      scenario: "NORMAL_MATCH",
      botBan: "NO_BAN",
      sendDiscordEvents: false,
      mmrOffset: playerWins ? -250 : 250,
    };
    const creation = await queue.createDebugBotMatch(
      player,
      { installedMods: structuredClone(cleanMods) },
      settings,
    );
    const queueRows = await database.query("SELECT * FROM ranked_queue_entries WHERE player_id = $1", [player.playerId]);
    expect(queueRows.rowCount).toBe(0);

    await matches.ready(creation.matchId, creation.playerMatchToken, creation.playerContext, {
      installedMods: structuredClone(cleanMods),
    });
    let state = await matches.ready(creation.matchId, creation.botMatchToken, creation.botContext, {
      installedMods: structuredClone(cleanMods),
    }) as Record<string, any>;
    expect(state.state).toBe("BAN_PHASE");
    await matches.submitBan(creation.matchId, creation.playerMatchToken, creation.playerContext, { canonicalLevelId: null });
    state = await matches.submitBan(creation.matchId, creation.botMatchToken, creation.botContext, { canonicalLevelId: null }) as Record<string, any>;
    expect(state.currentRound.map.playableLevelId).toBe(state.currentRound.map.alternateLevelId);
    expect(state.currentRound.map.playableLevelId).not.toBe(state.currentRound.map.canonicalLevelId);

    for (let round = 1; round <= 2; round += 1) {
      await matches.ready(creation.matchId, creation.playerMatchToken, creation.playerContext, {
        installedMods: structuredClone(cleanMods),
      });
      await matches.ready(creation.matchId, creation.botMatchToken, creation.botContext, {
        installedMods: structuredClone(cleanMods),
      });
      if (round === 1) {
        const playing = await matches.state(creation.matchId, creation.playerMatchToken, creation.playerContext) as Record<string, any>;
        await expect(matches.startAttempt(
          creation.matchId,
          creation.playerMatchToken,
          creation.playerContext,
          { levelId: playing.currentRound.map.canonicalLevelId, clientEventId: "wrong-level" },
        )).rejects.toThrow("playableLevelId");
      }
      await completeTwoClears(creation, playerWins);
      const scored = await matches.state(
        creation.matchId,
        creation.playerMatchToken,
        creation.playerContext,
      ) as Record<string, any>;
      expect(scored.currentRound.scores[playerWins ? "A" : "B"]).toBe(400);

      // alpha.10+ gives the trailing side the same 10-second LAST ATTEMPT
      // start window even when it has zero Clears. The rated-match helper must
      // let that authoritative window expire before attempting to ready the
      // following round. Otherwise the next ready call correctly returns 409.
      clock.advance(
        document.operational.rules.lastAttemptWindowSeconds * 1_000 +
          ATTEMPT_TRANSPORT_GRACE_MS +
          1,
      );
      await matches.state(creation.matchId, creation.playerMatchToken, creation.playerContext);
      // The fake clock jumps across the LAST ATTEMPT transport grace in one step.
      // In production the DebugBotService polls as the bot every tick, which refreshes
      // its heartbeat. Mirror that here so this rating-flow test does not accidentally
      // turn into a reconnect-timeout/forfeit test when transport grace grows.
      await matches.state(creation.matchId, creation.botMatchToken, creation.botContext);

      if (round === 1) {
        clock.advance((document.operational.timeouts?.roundResultSeconds ?? 5) * 1_000 + 1_000);
        await matches.state(creation.matchId, creation.playerMatchToken, creation.playerContext);
      }
    }
    state = await matches.state(creation.matchId, creation.playerMatchToken, creation.playerContext) as Record<string, any>;
    expect(state).toMatchObject({ state: "MATCH_RESULT", matchType: "DEBUG_BOT", debug: true });
    const profile = await database.query<{
      hidden_mmr: number;
      visible_ranked_score: number;
      placement_games: number;
      wins: number;
      losses: number;
      displayed_tier: DisplayTier;
      initial_csmp_tier: CsmpTier;
      initial_seed_mmr: number;
      seed_applied_at: Date | string;
    }>(
      `SELECT hidden_mmr, visible_ranked_score, placement_games, wins, losses,
              displayed_tier, initial_csmp_tier, initial_seed_mmr, seed_applied_at
       FROM ranked_profiles WHERE player_id = $1`,
      [player.playerId],
    );
    const match = await database.query<{
      match_type: string;
      mmr_delta_a: number;
      mmr_a_after: number;
      result_applied_at: Date | string;
    }>(
      `SELECT match_type, mmr_delta_a, mmr_a_after, result_applied_at
       FROM ranked_matches WHERE id = $1`,
      [creation.matchId],
    );
    const expected = calculateMmrUpdate({
      ratingA: player.hiddenMmr,
      ratingB: creation.botContext.hiddenMmr,
      placementGamesA: player.placementGames,
      placementGamesB: creation.botContext.placementGames,
      winner: playerWins ? "A" : "B",
    }, document.operational.mmrPolicy!);
    return {
      before: player.hiddenMmr,
      after: profile.rows[0]!,
      match: match.rows[0]!,
      expected,
      creation,
    };
  };

  it("rejects an incorrect server-side password without creating a match", async () => {
    const player = await contextFor("9301");
    await expect(debug.create(player, {
      password: "wrong",
      difficulty: "NORMAL",
      scenario: "NORMAL_MATCH",
      botBan: "NO_BAN",
      sendDiscordEvents: false,
      installedMods: structuredClone(cleanMods),
    })).rejects.toThrow("Incorrect password");
    const matchesCount = await database.query<{ count: number }>("SELECT COUNT(*)::int AS count FROM ranked_matches");
    expect(matchesCount.rows[0]?.count).toBe(0);
  });

  it("applies ordinary Ranked MMR, score, placement, stats, and history on player victory", async () => {
    const result = await runRatedMatch("9302", true);
    expect(Number(result.after.hidden_mmr)).toBeGreaterThan(result.before);
    expect(Number(result.after.hidden_mmr)).toBe(result.expected.ratingAfterA);
    expect(Number(result.after.visible_ranked_score)).toBe(Number(result.after.hidden_mmr));
    expect(Number(result.after.placement_games)).toBe(1);
    expect(Number(result.after.wins)).toBe(1);
    expect(Number(result.after.losses)).toBe(0);
    expect(result.after.initial_csmp_tier).toBe("BRONZE");
    expect(Number(result.after.initial_seed_mmr)).toBe(2_500);
    expect(result.after.seed_applied_at).toBeTruthy();
    expect(result.match.match_type).toBe("DEBUG_BOT");
    expect(Number(result.match.mmr_delta_a)).toBe(result.expected.deltaA);
    expect(Number(result.match.mmr_a_after)).toBe(result.expected.ratingAfterA);
    expect(result.match.result_applied_at).toBeTruthy();

    const secondSession = await contextFor("9302");
    expect(secondSession.hiddenMmr).toBe(Number(result.after.hidden_mmr));
    expect(secondSession.placementGames).toBe(1);
  });

  it("applies the same rating path, tier demotion, and Discord OFF on player loss", async () => {
    const result = await runRatedMatch("9303", false, {
      hiddenMmr: 2_001,
      placementGames: 5,
      displayedTier: "BRONZE",
    });
    expect(Number(result.after.hidden_mmr)).toBeLessThan(result.before);
    expect(Number(result.after.hidden_mmr)).toBe(result.expected.ratingAfterA);
    expect(Number(result.after.visible_ranked_score)).toBe(Number(result.after.hidden_mmr));
    expect(Number(result.after.placement_games)).toBe(6);
    expect(Number(result.after.wins)).toBe(0);
    expect(Number(result.after.losses)).toBe(1);
    expect(result.after.displayed_tier).toBe("AQUA");
    const outbox = await database.query<{ count: number }>(
      "SELECT COUNT(*)::int AS count FROM ranked_outbox_events WHERE aggregate_id = $1",
      [result.creation.matchId],
    );
    expect(outbox.rows[0]?.count).toBe(0);
  });

  it("applies ordinary tier promotion when a Bot Match crosses a boundary", async () => {
    const result = await runRatedMatch("9305", true, {
      hiddenMmr: 2_999,
      placementGames: 5,
      displayedTier: "BRONZE",
    });
    expect(Number(result.after.hidden_mmr)).toBe(result.expected.ratingAfterA);
    expect(Number(result.after.hidden_mmr)).toBeGreaterThanOrEqual(3_000);
    expect(result.after.displayed_tier).toBe("SILVER");
  });

  it("uses the real LAST ATTEMPT spectator state for the mandatory debug scenario", async () => {
    const player = await contextFor("9304");
    const created = await debug.create(player, {
      password: "2008",
      difficulty: "NORMAL",
      scenario: "TRIGGER_LAST_ATTEMPT",
      botBan: "NO_BAN",
      sendDiscordEvents: false,
      installedMods: structuredClone(cleanMods),
    });
    await matches.ready(created.matchId, created.matchToken, player, {
      installedMods: structuredClone(cleanMods),
    });
    await debug.tickOnce();
    await matches.submitBan(created.matchId, created.matchToken, player, { canonicalLevelId: null });
    await debug.tickOnce();
    await matches.ready(created.matchId, created.matchToken, player, {
      installedMods: structuredClone(cleanMods),
    });
    await debug.tickOnce();
    let state = await matches.state(created.matchId, created.matchToken, player) as Record<string, any>;
    expect(state.state).toBe("LAST_ATTEMPT_WINDOW");
    expect(state.currentRound.clears).toEqual({ A: 2, B: 1 });
    expect(state.spectator).toMatchObject({ active: true, opponentName: "BOT NORMAL" });
    const scoreBeforeTelemetry = structuredClone(state.currentRound.scores);
    let sawLiveProgress = false;
    for (let tick = 0; tick < 20 && !sawLiveProgress; tick += 1) {
      clock.advance(125);
      await debug.tickOnce();
      state = await matches.state(created.matchId, created.matchToken, player) as Record<string, any>;
      sawLiveProgress = Number(state.spectator?.currentProgress ?? 0) > 0;
    }
    expect(sawLiveProgress).toBe(true);
    expect(state.currentRound.scores).toEqual(scoreBeforeTelemetry);
    const activeBotAttempt = await database.query<{ progress_percent: number | null }>(
      `SELECT a.progress_percent
       FROM ranked_attempts a
       JOIN ranked_rounds r ON r.id = a.round_id
       WHERE r.match_id = $1 AND a.ended_at IS NULL`,
      [created.matchId],
    );
    expect(activeBotAttempt.rows[0]?.progress_percent).toBeNull();
    await expect(matches.startAttempt(created.matchId, created.matchToken, player, {
      levelId: state.currentRound.map.playableLevelId,
      clientEventId: "spectator-blocked",
    })).resolves.toMatchObject({ accepted: false, reason: "LAST_ATTEMPT_TARGET_ONLY" });

    // Regression: if the Bot's accepted final attempt is still alive when the
    // 10-second start window expires, MatchService correctly enters
    // ROUND_SETTLING. DebugBotService must continue ticking that already-started
    // attempt there; alpha.30 stopped the simulator at this phase and trapped the
    // match forever.
    clock.advance(11_000);
    await debug.tickOnce();
    state = await matches.state(created.matchId, created.matchToken, player) as Record<string, any>;
    expect(state.state).not.toBe("ROUND_SETTLING");
    const orphanedBotAttempt = await database.query<{ count: number }>(
      `SELECT COUNT(*)::int AS count
       FROM ranked_attempts a
       JOIN ranked_rounds r ON r.id = a.round_id
       WHERE r.match_id = $1
         AND a.player_id = (SELECT player_b_id FROM ranked_matches WHERE id = $1)
         AND a.ended_at IS NULL`,
      [created.matchId],
    );
    expect(Number(orphanedBotAttempt.rows[0]?.count ?? 0)).toBe(0);
  });
});
