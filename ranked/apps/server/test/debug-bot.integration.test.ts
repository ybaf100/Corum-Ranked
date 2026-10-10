import { enterPreparedMatch } from "./native-entry-helper.js";
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
import { AchievementService } from "../src/achievement/achievement.service.js";

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
    version: "v0.5.0-beta.14",
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
  {
    id: "geode.node-ids",
    version: "v1.23.3",
    enabled: true,
    loaded: true,
    internal: false,
    system: false,
  },
];

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
        EASY: { mmrOffset: -250, qualifyingChance: 0.3, clearChance: 0, paceMultiplier: 0.96 },
        NORMAL: { mmrOffset: 0, qualifyingChance: 0.6, clearChance: 0.1, paceMultiplier: 1.00 },
        HARD: { mmrOffset: 250, qualifyingChance: 0.8, clearChance: 0.2, paceMultiplier: 1.05 },
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
    matches = new MatchService(
      database,
      clock,
      ids,
      random,
      new MatchAccessService(tokens, clock),
      sessions,
      new OutboxService(ids),
      new AchievementService(database, clock, ids),
      new InMemoryMatchRuntimeState(),
    );
    queue = new QueueService(database, clock, ids, random, config, sessions, tokens, matches);
    debug = new DebugBotService(environment, database, clock, random, queue, matches, config, tokens);
  });

  const contextFor = async (accountId: string): Promise<RankedSessionContext> => {
    const created = await sessions.create({
      gdAccountId: accountId,
      gdUsername: `Debug${accountId}`,
      clientVersion: "v0.5.0-beta.14",
      clientInstanceId: "test-process-instance",
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

  it.each(["RANDOM", "NO_BAN"] as const)("starts Death Match directly without bans/rounds (%s) and waits for client preparation", async (botBan) => {
    const player = await contextFor(botBan === "RANDOM" ? "9344" : "9345");
    const created = await debug.create(player, { password: "2008", difficulty: "NORMAL", scenario: "TRIGGER_DEATHMATCH",
      botBan, sendDiscordEvents: false, installedMods: structuredClone(cleanMods) });
    let state = await matches.state(created.matchId, created.matchToken, player) as Record<string, any>;
    expect(state.state).toBe('DEATHMATCH_PREPARE');
    expect(state.currentRound).toBeNull();
    expect(state.rounds).toEqual([]);
    expect(state.series).toMatchObject({ phase: 'DEATHMATCH_REQUIRED', roundResults: [], roundWins: { A: 0, B: 0 } });
    expect(state.deathmatch.sequence).toBe(1);
    const initialMap = state.deathmatch.map.playableLevelId;
    const initial = await database.query<{ ready_a_at: unknown; ready_b_at: unknown; ban_a_confirmed_at: unknown; ban_b_confirmed_at: unknown }>(
      'SELECT ready_a_at, ready_b_at, ban_a_confirmed_at, ban_b_confirmed_at FROM ranked_matches WHERE id = $1', [created.matchId]);
    expect(initial.rows[0]).toMatchObject({ ready_a_at: null, ban_a_confirmed_at: null, ban_b_confirmed_at: null });
    expect(initial.rows[0]!.ready_b_at).not.toBeNull();

    // No synthetic human READY may bypass the menu's ten-second resource gate,
    // including after the driver is recovered without its in-memory state.
    clock.advance(9900);
    (debug as any).drivers.clear();
    await debug.tickOnce();
    state = await matches.state(created.matchId, created.matchToken, player) as Record<string, any>;
    expect(state.state).toBe('DEATHMATCH_PREPARE');
    expect(state.deathmatch.map.playableLevelId).toBe(initialMap);
    clock.advance(100);
    await matches.ready(created.matchId, created.matchToken, player, { installedMods: structuredClone(cleanMods) });
    state = await matches.state(created.matchId, created.matchToken, player) as Record<string, any>;
    expect(state.state).toBe('DEATHMATCH_ENTERING');
    expect(state.rounds).toEqual([]);
    expect(state.entry).toMatchObject({ roundNumber: 0, deathmatchSequence: 1, playableLevelId: initialMap });
    await Promise.all([matches.forceDebugDeathmatch(created.matchId), matches.forceDebugDeathmatch(created.matchId)]);
    const counts = await database.query<{ rounds: number; deathmatches: number }>(
      `SELECT (SELECT COUNT(*) FROM ranked_rounds WHERE match_id = $1)::int AS rounds,
              (SELECT COUNT(*) FROM ranked_deathmatches WHERE match_id = $1)::int AS deathmatches`, [created.matchId]);
    expect(counts.rows[0]).toEqual({ rounds: 0, deathmatches: 1 });
    const body = { roundNumber: state.entry.roundNumber, deathmatchSequence: state.entry.deathmatchSequence,
      playableLevelId: state.entry.playableLevelId };
    expect((await matches.startAttempt(created.matchId, created.matchToken, player,
      { levelId: body.playableLevelId, clientEventId: 'before-native-deathmatch-entry' })).accepted).toBe(false);
    await matches.entered(created.matchId, created.matchToken, player, body);
    clock.advance(3000);
    state = await matches.state(created.matchId, created.matchToken, player) as Record<string, any>;
    expect(state.state).toBe('DEATHMATCH_PLAYING');
    expect((await matches.startAttempt(created.matchId, created.matchToken, player,
      { levelId: body.playableLevelId, clientEventId: 'native-client-deathmatch-first' })).accepted).toBe(true);
    await debug.tickOnce();
    const driver = (debug as any).drivers.get(created.matchId);
    expect(driver.attemptId).toBeTruthy();
    for (let tick = 0; tick < 20 && driver.attemptProgress === 0; tick += 1) { clock.advance(125); await debug.tickOnce(); }
    expect(driver.attemptProgress).toBeGreaterThan(0);
    await matches.surrender(created.matchId, created.matchToken, player,
      { scope: 'ROUND', roundNumber: 0, deathmatchSequence: 1, clientEventId: 'direct-deathmatch-surrender' });
    state = await matches.state(created.matchId, created.matchToken, player) as Record<string, any>;
    expect(state.state).toBe('MATCH_RESULT');
    expect(state.series).toMatchObject({ phase: 'MATCH_RESULT', winner: 'B', roundResults: [] });
    expect(state.rounds).toEqual([]);
  });

  it("recovers a Death Match creation interrupted before its first driver transition", async () => {
    const player = await contextFor('9360');
    const creation = await queue.createDebugBotMatch(player, { installedMods: structuredClone(cleanMods) },
      { difficulty: 'NORMAL', scenario: 'TRIGGER_DEATHMATCH', botBan: 'RANDOM', sendDiscordEvents: false, mmrOffset: 0 });
    expect((await matches.state(creation.matchId, creation.playerMatchToken, player)).state).toBe('MATCHED');
    (debug as any).drivers.clear();
    await debug.tickOnce();
    const state = await matches.state(creation.matchId, creation.playerMatchToken, player) as Record<string, any>;
    expect(state.state).toBe('DEATHMATCH_PREPARE');
    expect(state.rounds).toEqual([]);
    expect(state.deathmatch.sequence).toBe(1);
  });

  it("refuses direct Death Match entry for other debug scenarios and real PvP", async () => {
    const player = await contextFor('9361');
    const normal = await debug.create(player, { password: '2008', difficulty: 'NORMAL', scenario: 'NORMAL_MATCH',
      botBan: 'NO_BAN', sendDiscordEvents: false, installedMods: structuredClone(cleanMods) });
    await expect(matches.forceDebugDeathmatch(normal.matchId)).rejects.toThrow('Debug Bot Death Match scenario');
    expect((await matches.state(normal.matchId, normal.matchToken, player)).state).toBe('MATCHED');
    const a = await contextFor('9362');
    const b = await contextFor('9363');
    await queue.join(a, { installedMods: structuredClone(cleanMods) });
    await queue.join(b, { installedMods: structuredClone(cleanMods) });
    const pvp = await database.query<{ id: string }>(
      "SELECT id FROM ranked_matches WHERE match_type = 'RANKED_PVP' AND (player_a_id = $1 OR player_b_id = $1)", [a.playerId]);
    expect(pvp.rows).toHaveLength(1);
    await expect(matches.forceDebugDeathmatch(pvp.rows[0]!.id)).rejects.toThrow('Debug Bot Death Match scenario');
  });

  it("retires the surrendered round's Bot context and starts/progresses a new R2 Attempt", async () => {
    const player = await contextFor("9343");
    const created = await debug.create(player, { password: "2008", difficulty: "NORMAL", scenario: "NORMAL_MATCH",
      botBan: "NO_BAN", sendDiscordEvents: false, installedMods: structuredClone(cleanMods) });
    await matches.ready(created.matchId, created.matchToken, player, { installedMods: structuredClone(cleanMods) });
    await debug.tickOnce();
    await matches.submitBan(created.matchId, created.matchToken, player, { canonicalLevelId: null });
    await debug.tickOnce();
    await matches.ready(created.matchId, created.matchToken, player, { installedMods: structuredClone(cleanMods) });
    let state = await matches.state(created.matchId, created.matchToken, player);
    await matches.entered(created.matchId, created.matchToken, player, {
      roundNumber: state.entry!.roundNumber, deathmatchSequence: state.entry!.deathmatchSequence, playableLevelId: state.entry!.playableLevelId });
    clock.advance(3000);await debug.tickOnce();
    const driver = (debug as any).drivers.get(created.matchId);
    const oldAttempt = driver.attemptId;expect(oldAttempt).toBeTruthy();
    await matches.surrender(created.matchId, created.matchToken, player,
      { scope: "ROUND", roundNumber: 1, deathmatchSequence: 0, clientEventId: "r1-surrender" });
    state = await matches.state(created.matchId, created.matchToken, player);expect(state.state).toBe("ROUND_RESULT");
    clock.advance(10000);await debug.tickOnce();
    state = await matches.state(created.matchId, created.matchToken, player);expect(state.state).toBe("ROUND_PREPARE");
    expect(driver.attemptId).toBeNull();
    await matches.ready(created.matchId, created.matchToken, player, { installedMods: structuredClone(cleanMods) });
    state = await matches.state(created.matchId, created.matchToken, player);
    await matches.entered(created.matchId, created.matchToken, player, {
      roundNumber: state.entry!.roundNumber, deathmatchSequence: state.entry!.deathmatchSequence, playableLevelId: state.entry!.playableLevelId });
    clock.advance(3000);await debug.tickOnce();
    expect(driver.attemptId).toBeTruthy();expect(driver.attemptContextKey).toBe("R2");
    // Domain IDs (e.g. B-1) are round-scoped and may repeat. The new
    // authoritative row/start event, not string inequality, proves R2 started.
    const fresh = await database.query<{ domain_attempt_id: string }>(
      `SELECT a.domain_attempt_id FROM ranked_attempts a JOIN ranked_rounds r ON r.id = a.round_id
       WHERE r.match_id = $1 AND r.round_number = 2 AND a.ended_at IS NULL`, [created.matchId]);
    expect(fresh.rows).toHaveLength(1);expect(fresh.rows[0]!.domain_attempt_id).toBe(driver.attemptId);
    clock.advance(500);await debug.tickOnce();expect(driver.attemptProgress).toBeGreaterThan(0);
    const old = await database.query<{ ended_at: unknown }>(
      `SELECT a.ended_at FROM ranked_attempts a JOIN ranked_rounds r ON r.id = a.round_id
       WHERE r.match_id = $1 AND r.round_number = 1 AND a.domain_attempt_id = $2`, [created.matchId, oldAttempt]);
    expect(old.rows[0]!.ended_at).not.toBeNull();
    await matches.surrender(created.matchId, created.matchToken, player,
      { scope: "MATCH", roundNumber: 2, deathmatchSequence: 0, clientEventId: "cleanup-surrender" });
    await debug.tickOnce();
  });

  const completeThreeClears = async (
    creation: Awaited<ReturnType<QueueService["createDebugBotMatch"]>>,
    playerWins: boolean,
    roundNumber: number,
  ) => {
    const state = await matches.state(
      creation.matchId,
      creation.playerMatchToken,
      creation.playerContext,
    ) as Record<string, any>;
    const levelId = state.currentRound.map.playableLevelId as string;
    const context = playerWins ? creation.playerContext : creation.botContext;
    const token = playerWins ? creation.playerMatchToken : creation.botMatchToken;
    for (let index = 0; index < document.operational.rules.clearTarget; index += 1) {
      const started = await matches.startAttempt(creation.matchId, token, context, {
        levelId,
        clientEventId: `rating-${creation.matchId}-r${roundNumber}-${index}-start`,
      });
      await matches.endAttempt(creation.matchId, token, context, {
        levelId,
        attemptId: started.attemptId!,
        clientEventId: `rating-${creation.matchId}-r${roundNumber}-${index}-end`,
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

    let state = await matches.ready(creation.matchId, creation.playerMatchToken, creation.playerContext, {
      installedMods: structuredClone(cleanMods),
    }) as Record<string, any>;
    // DEBUG_BOT has no real B-side client. A single player Ready must advance
    // immediately; this catches the production READY deadlock directly.
    expect(state.state).toBe("BAN_PHASE");
    expect(state.ready).toEqual({ A: true, B: true });
    await matches.submitBan(creation.matchId, creation.playerMatchToken, creation.playerContext, { canonicalLevelId: null });
    state = await matches.submitBan(creation.matchId, creation.botMatchToken, creation.botContext, { canonicalLevelId: null }) as Record<string, any>;
    expect(state.currentRound.map.playableLevelId).toBe(state.currentRound.map.alternateLevelId);
    expect(state.currentRound.map.playableLevelId).not.toBe(state.currentRound.map.canonicalLevelId);

    for (let round = 1; round <= 2; round += 1) {
      await matches.ready(
        creation.matchId,
        creation.playerMatchToken,
        creation.playerContext,
        { installedMods: structuredClone(cleanMods) },
      );
      const readyState = await enterPreparedMatch(matches, creation.matchId,
        creation.playerMatchToken, creation.playerContext, creation.botMatchToken, creation.botContext,
        (seconds) => clock.advance(seconds * 1000));
      expect(readyState.state).toBe("ROUND_PLAYING");
      expect(readyState.ready).toEqual({ A: true, B: true });
      if (round === 1) {
        const playing = await matches.state(creation.matchId, creation.playerMatchToken, creation.playerContext) as Record<string, any>;
        await expect(matches.startAttempt(
          creation.matchId,
          creation.playerMatchToken,
          creation.playerContext,
          { levelId: playing.currentRound.map.canonicalLevelId, clientEventId: "wrong-level" },
        )).rejects.toThrow("playableLevelId");
      }
      await completeThreeClears(creation, playerWins, round);
      const scored = await matches.state(
        creation.matchId,
        creation.playerMatchToken,
        creation.playerContext,
      ) as Record<string, any>;
      expect(scored.currentRound.scores[playerWins ? "A" : "B"]).toBe(750);

      // alpha.10+ gives the trailing side the same 10-second LAST ATTEMPT
      // start window even when it has zero Clears. The rated-match helper must
      // let that authoritative window expire before attempting to ready the
      // following round. Otherwise the next ready call correctly returns 409.
      //
      // Refresh the synthetic Bot heartbeat BEFORE the fake-clock jump. In
      // production DebugBotService polls as the Bot every tick; refreshing only
      // after the jump is too late because advanceLockedMatch() evaluates the
      // reconnect timeout before state() can write the new heartbeat. On Round 2
      // that race can incorrectly forfeit the Bot and turn an intended player
      // loss into a Player-A win (+MMR).
      await matches.state(creation.matchId, creation.botMatchToken, creation.botContext);
      clock.advance(
        document.operational.rules.lastAttemptWindowSeconds * 1_000 +
          ATTEMPT_TRANSPORT_GRACE_MS +
          1,
      );
      await matches.state(creation.matchId, creation.playerMatchToken, creation.playerContext);
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
      placement_results: unknown;
      promotion_source_tier: DisplayTier | null;
      promotion_target_tier: DisplayTier | null;
      demotion_warning: boolean;
      initial_csmp_tier: CsmpTier;
      initial_seed_mmr: number;
      seed_applied_at: Date | string;
    }>(
      `SELECT hidden_mmr, visible_ranked_score, placement_games, placement_results,
              wins, losses, displayed_tier, promotion_source_tier,
              promotion_target_tier, demotion_warning,
              initial_csmp_tier, initial_seed_mmr, seed_applied_at
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
      scoreA: playerWins ? 1_500 : 0,
      scoreB: playerWins ? 0 : 1_500,
      clearsA: playerWins ? 6 : 0,
      clearsB: playerWins ? 0 : 6,
      resultStreakA: 0,
      resultStreakB: 0,
      maximumGainSide: playerWins ? "A" : "B",
    }, document.operational.mmrPolicy!);
    return {
      before: player.hiddenMmr,
      after: profile.rows[0]!,
      match: match.rows[0]!,
      expected,
      creation,
      state,
    };
  };

  it("creates an EASY bot match when Hidden MMR falls below the visible LP floor", async () => {
    const player = await contextFor("9320");
    await database.query(
      `UPDATE ranked_profiles
       SET hidden_mmr = -21, visible_ranked_score = 0
       WHERE player_id = $1`,
      [player.playerId],
    );

    const creation = await queue.createDebugBotMatch(
      { ...player, hiddenMmr: -21 },
      { installedMods: structuredClone(cleanMods) },
      {
        difficulty: "EASY",
        scenario: "NORMAL_MATCH",
        botBan: "NO_BAN",
        sendDiscordEvents: false,
        mmrOffset: -250,
      },
    );

    expect(creation.botContext.hiddenMmr).toBe(-271);
    expect(creation.botContext.displayedTier).toBe("RED");

    const botProfile = await database.query<{
      hidden_mmr: number;
      visible_ranked_score: number;
    }>(
      "SELECT hidden_mmr, visible_ranked_score FROM ranked_profiles WHERE player_id = $1",
      [creation.botContext.playerId],
    );
    expect(Number(botProfile.rows[0]?.hidden_mmr)).toBe(-271);
    expect(Number(botProfile.rows[0]?.visible_ranked_score)).toBe(0);

    const match = await database.query<{
      effective_rating_average: number;
      effective_tier: string;
      mmr_a_before: number;
      mmr_b_before: number;
    }>(
      `SELECT effective_rating_average, effective_tier, mmr_a_before, mmr_b_before
       FROM ranked_matches WHERE id = $1`,
      [creation.matchId],
    );
    expect(Number(match.rows[0]?.mmr_a_before)).toBe(-21);
    expect(Number(match.rows[0]?.mmr_b_before)).toBe(-271);
    expect(Number(match.rows[0]?.effective_rating_average)).toBe(-146);
    expect(match.rows[0]?.effective_tier).toBe("RED");
  });

  it("rejects an incorrect server-side password without creating a match", async () => {
    const player = await contextFor("9301");
    const matchesBefore = await database.query<{ count: number }>(
      "SELECT COUNT(*)::int AS count FROM ranked_matches",
    );

    await expect(debug.create(player, {
      password: "wrong",
      difficulty: "NORMAL",
      scenario: "NORMAL_MATCH",
      botBan: "NO_BAN",
      sendDiscordEvents: false,
      installedMods: structuredClone(cleanMods),
    })).rejects.toThrow("Incorrect password");

    const matchesAfter = await database.query<{ count: number }>(
      "SELECT COUNT(*)::int AS count FROM ranked_matches",
    );
    expect(matchesAfter.rows[0]?.count).toBe(matchesBefore.rows[0]?.count);
  });

  it.each([
    ["FORCE_WIN", "A", 1, 0],
    ["FORCE_LOSE", "B", 0, 1],
  ] as const)("applies %s immediately through the authoritative result path", async (
    scenario,
    winnerSide,
    expectedWins,
    expectedLosses,
  ) => {
    const accountId = scenario === "FORCE_WIN" ? "9310" : "9311";
    const player = await contextFor(accountId);
    const created = await debug.create(player, {
      password: "2008",
      difficulty: "NORMAL",
      scenario,
      botBan: "NO_BAN",
      sendDiscordEvents: false,
      installedMods: structuredClone(cleanMods),
    });
    const state = await matches.state(created.matchId, created.matchToken, player) as Record<string, any>;
    expect(state).toMatchObject({ state: "MATCH_RESULT", result: { winnerSide } });
    expect(state.result.perfectWinSide).toBeNull();
    expect(JSON.stringify(state)).not.toMatch(/hiddenMmr|hidden_mmr|mmrDelta|mmr_[ab]_/);
    const stored = await database.query<{ wins: number; losses: number; placement_results: unknown }>(
      "SELECT wins, losses, placement_results FROM ranked_profiles WHERE player_id = $1",
      [player.playerId],
    );
    expect(Number(stored.rows[0]?.wins)).toBe(expectedWins);
    expect(Number(stored.rows[0]?.losses)).toBe(expectedLosses);
    expect(stored.rows[0]?.placement_results).toEqual([]);

    const achievementMetrics = await database.query<{
      ranked_wins: number;
      ranked_matches: number;
    }>(
      `SELECT ranked_wins, ranked_matches
       FROM ranked_achievement_metrics WHERE player_id = $1`,
      [player.playerId],
    );
    expect(Number(achievementMetrics.rows[0]?.ranked_matches)).toBe(1);
    expect(Number(achievementMetrics.rows[0]?.ranked_wins)).toBe(expectedWins);
  });

  it("applies maximum Ranked MMR and PERFECT WIN for a natural no-loss three-Clear victory", async () => {
    const result = await runRatedMatch("9302", true);
    expect(Number(result.after.hidden_mmr)).toBeGreaterThan(result.before);
    expect(Number(result.after.hidden_mmr)).toBe(result.expected.ratingAfterA);
    expect(Number(result.after.visible_ranked_score)).toBeGreaterThan(2_500);
    expect(Number(result.after.placement_games)).toBe(5);
    expect(result.after.placement_results).toEqual([]);
    expect(result.after.displayed_tier).toBe("BRONZE");
    expect(Number(result.after.wins)).toBe(1);
    expect(Number(result.after.losses)).toBe(0);
    expect(result.after.initial_csmp_tier).toBe("BRONZE");
    expect(Number(result.after.initial_seed_mmr)).toBe(2_500);
    expect(result.after.seed_applied_at).toBeTruthy();
    expect(result.match.match_type).toBe("DEBUG_BOT");
    expect(Number(result.match.mmr_delta_a)).toBe(result.expected.deltaA);
    expect(Number(result.match.mmr_a_after)).toBe(result.expected.ratingAfterA);
    expect(result.match.result_applied_at).toBeTruthy();
    expect(result.state.result.perfectWinSide).toBe("A");

    const secondSession = await contextFor("9302");
    expect(secondSession.hiddenMmr).toBe(Number(result.after.hidden_mmr));
    expect(secondSession.placementGames).toBe(5);
  });

  it("applies the same rating path, tier demotion, and Discord OFF on player loss", async () => {
    const result = await runRatedMatch("9303", false, {
      hiddenMmr: 2_001,
      placementGames: 5,
      displayedTier: "BRONZE",
    });
    expect(Number(result.after.hidden_mmr)).toBeLessThan(result.before);
    expect(Number(result.after.hidden_mmr)).toBe(result.expected.ratingAfterA);
    expect(Number(result.after.visible_ranked_score)).toBe(2_000);
    expect(Number(result.after.placement_games)).toBe(5);
    expect(Number(result.after.wins)).toBe(0);
    expect(Number(result.after.losses)).toBe(1);
    expect(result.after.displayed_tier).toBe("BRONZE");
    expect(result.after.demotion_warning).toBe(true);
    const outbox = await database.query<{ count: number }>(
      "SELECT COUNT(*)::int AS count FROM ranked_outbox_events WHERE aggregate_id = $1",
      [result.creation.matchId],
    );
    expect(outbox.rows[0]?.count).toBe(0);
  });

  it("awards the existing normal LP maximum to the PERFECT WIN winner", async () => {
    const result = await runRatedMatch("9306", true, {
      hiddenMmr: 2_500,
      placementGames: 5,
      displayedTier: "BRONZE",
    });
    expect(result.state.result.perfectWinSide).toBe("A");
    expect(result.state.result.rankedScoreDelta.A).toBe(38);
    expect(Number(result.after.visible_ranked_score)).toBe(2_538);
  });

  it("starts a promotion series at cutoff-1 without counting the trigger win", async () => {
    const result = await runRatedMatch("9305", true, {
      hiddenMmr: 2_999,
      placementGames: 5,
      displayedTier: "BRONZE",
    });
    expect(Number(result.after.hidden_mmr)).toBe(result.expected.ratingAfterA);
    expect(Number(result.after.hidden_mmr)).toBeGreaterThanOrEqual(3_000);
    expect(Number(result.after.visible_ranked_score)).toBe(2_999);
    expect(result.after.displayed_tier).toBe("BRONZE");
    expect(result.after.promotion_source_tier).toBe("BRONZE");
    expect(result.after.promotion_target_tier).toBe("SILVER");
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
    const entering = await matches.state(created.matchId, created.matchToken, player);
    expect(entering.state).toBe("ROUND_ENTERING");
    await matches.entered(created.matchId, created.matchToken, player, {
      roundNumber: entering.entry!.roundNumber, deathmatchSequence: entering.entry!.deathmatchSequence,
      playableLevelId: entering.entry!.playableLevelId,
    });
    clock.advance(3000);
    await debug.tickOnce();
    let state = await matches.state(created.matchId, created.matchToken, player) as Record<string, any>;
    expect(state.state).toBe("LAST_ATTEMPT_WINDOW");
    expect(state.currentRound.clears).toEqual({ A: 3, B: 2 });
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
