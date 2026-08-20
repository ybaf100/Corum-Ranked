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

let pglite: PGlite;
let database: PgliteDatabase;

beforeAll(async () => {
  pglite = new PGlite();
  database = new PgliteDatabase(pglite);
  const migrationPath = fileURLToPath(
    new URL("../../../migrations/0001_initial_ranked.sql", import.meta.url),
  );
  await pglite.exec(await readFile(migrationPath, "utf8"));
}, 20_000);

afterAll(async () => {
  await pglite.close();
});

class SequenceIds implements IdGenerator {
  private value = 1_000;
  public next(): string {
    this.value += 1;
    return `00000000-0000-4000-8002-${String(this.value).padStart(12, "0")}`;
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

describe("two-client authoritative match flow", () => {
  it("runs ready, private bans, two rounds, and applies MMR exactly once", async () => {
    const baseDocument = configDocumentFixture("match-flow");
    const document = {
      ...baseDocument,
      operational: {
        ...baseDocument.operational,
        timeouts: {
          ...baseDocument.operational.timeouts!,
          reconnectGraceSeconds: 10_000,
        },
      },
    };
    let liveDocument = document;
    const config = {
      getSnapshot: () => ({ ...structuredClone(liveDocument), fetchedAt: "2026-08-20T02:00:00Z" }),
    } as RankedConfigService;
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
    const queue = new QueueService(
      database,
      clock,
      ids,
      new SeededRandom(90),
      config,
      sessions,
      tokens,
    );
    const matches = new MatchService(
      database,
      clock,
      ids,
      new SeededRandom(91),
      new MatchAccessService(tokens, clock),
      sessions,
      new OutboxService(ids),
      new InMemoryMatchRuntimeState(),
    );
    const playerA = await createContext("9101", sessions, tokens);
    const playerB = await createContext("9102", sessions, tokens);
    await queue.join(playerA, { installedMods: structuredClone(cleanMods) });
    await queue.join(playerB, { installedMods: structuredClone(cleanMods) });
    const statusA = (await queue.status(playerA)) as {
      matchId: string;
      matchToken: string;
      side: "A";
    };
    const statusB = (await queue.status(playerB)) as {
      matchId: string;
      matchToken: string;
      side: "B";
    };
    expect(statusA.matchId).toBe(statusB.matchId);
    const matchId = statusA.matchId;

    let state = (await matches.ready(matchId, statusA.matchToken, playerA, {
      installedMods: structuredClone(cleanMods),
    })) as Record<string, any>;
    expect(state.state).toBe("MATCHED");
    state = (await matches.ready(matchId, statusB.matchToken, playerB, {
      installedMods: structuredClone(cleanMods),
    })) as Record<string, any>;
    expect(state.state).toBe("BAN_PHASE");
    expect(state.candidateMaps).toHaveLength(5);
    const sharedBan = state.candidateMaps[0].canonicalLevelId as string;

    await matches.submitBan(matchId, statusA.matchToken, playerA, {
      canonicalLevelId: sharedBan,
    });
    state = (await matches.submitBan(matchId, statusB.matchToken, playerB, {
      canonicalLevelId: sharedBan,
    })) as Record<string, any>;
    expect(state.state).toBe("ROUND_PREPARE");
    expect(state.currentRound.roundNumber).toBe(1);
    expect(state.currentRound.map).toBeTruthy();
    expect(state.selectedRoundMaps).toBeUndefined();
    expect(state.candidateMaps).toBeNull();

    const winRoundWithTwoClears = async (roundNumber: number) => {
      await matches.ready(matchId, statusA.matchToken, playerA, {
        installedMods: structuredClone(cleanMods),
      });
      const playing = (await matches.ready(matchId, statusB.matchToken, playerB, {
        installedMods: structuredClone(cleanMods),
      })) as Record<string, any>;
      expect(playing.state).toBe("ROUND_PLAYING");
      expect(playing.currentRound.roundNumber).toBe(roundNumber);
      const levelId = playing.currentRound.map.playableLevelId as string;
      expect(levelId).toBe(playing.currentRound.map.alternateLevelId);

      clock.advanceSeconds(1);
      const firstStart = await matches.startAttempt(matchId, statusA.matchToken, playerA, {
        levelId,
        clientEventId: `r${roundNumber}-a1-start`,
      });
      clock.advanceSeconds(1);
      await matches.endAttempt(matchId, statusA.matchToken, playerA, {
        levelId,
        attemptId: firstStart.attemptId!,
        clientEventId: `r${roundNumber}-a1-end`,
        progressPercent: 100,
        cleared: true,
      });
      clock.advanceSeconds(1);
      const secondStart = await matches.startAttempt(matchId, statusA.matchToken, playerA, {
        levelId,
        clientEventId: `r${roundNumber}-a2-start`,
      });
      clock.advanceSeconds(1);
      await matches.endAttempt(matchId, statusA.matchToken, playerA, {
        levelId,
        attemptId: secondStart.attemptId!,
        clientEventId: `r${roundNumber}-a2-end`,
        progressPercent: 100,
        cleared: true,
      });
    };

    await winRoundWithTwoClears(1);
    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.state).toBe("ROUND_RESULT");
    expect(state.currentRound.outcome).toMatchObject({ winner: "A", reason: "TWO_CLEAR_ZERO" });
    expect(state.spectator).toEqual({ active: false });

    clock.advanceSeconds(6);
    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.state).toBe("ROUND_PREPARE");
    expect(state.currentRound.roundNumber).toBe(2);
    expect(state.currentRound.banner).toBe("MATCH_POINT");
    expect(state.currentRound.clears).toEqual({ A: 0, B: 0 });

    await matches.ready(matchId, statusA.matchToken, playerA, {
      installedMods: structuredClone(cleanMods),
    });
    const roundTwoPlaying = (await matches.ready(matchId, statusB.matchToken, playerB, {
      installedMods: structuredClone(cleanMods),
    })) as Record<string, any>;
    expect(roundTwoPlaying.state).toBe("ROUND_PLAYING");
    expect(roundTwoPlaying.currentRound.clears).toEqual({ A: 0, B: 0 });
    const roundTwoLevelId = roundTwoPlaying.currentRound.map.playableLevelId as string;
    expect(roundTwoLevelId).toBe(roundTwoPlaying.currentRound.map.alternateLevelId);
    const snapshottedQualifying = roundTwoPlaying.currentRound.map.qualifyingPercent as number;
    const snapshottedPlayableLevelId = roundTwoLevelId;
    liveDocument = {
      ...document,
      maps: document.maps.map((map) => ({
        ...map,
        alternateLevelId: `8${map.canonicalLevelId}`,
        qualifyingPercent: 99,
      })),
    };
    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.currentRound.map.qualifyingPercent).toBe(snapshottedQualifying);
    expect(state.currentRound.map.playableLevelId).toBe(snapshottedPlayableLevelId);

    await expect(
      matches.startAttempt(matchId, statusB.matchToken, playerB, {
        levelId: roundTwoPlaying.currentRound.map.canonicalLevelId,
        clientEventId: "r2-wrong-map-start",
      }),
    ).rejects.toThrow("Attempt levelId does not match the Round snapshot");

    clock.advanceSeconds(1);
    const b1 = await matches.startAttempt(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      clientEventId: "r2-b1-start",
    });
    clock.advanceSeconds(1);
    await matches.endAttempt(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      attemptId: b1.attemptId!,
      clientEventId: "r2-b1-end",
      progressPercent: 100,
      cleared: true,
    });
    clock.advanceSeconds(1);
    const a1 = await matches.startAttempt(matchId, statusA.matchToken, playerA, {
      levelId: roundTwoLevelId,
      clientEventId: "r2-a1-start",
    });
    clock.advanceSeconds(1);
    await matches.endAttempt(matchId, statusA.matchToken, playerA, {
      levelId: roundTwoLevelId,
      attemptId: a1.attemptId!,
      clientEventId: "r2-a1-end",
      progressPercent: 100,
      cleared: true,
    });
    clock.advanceSeconds(1);
    const b2 = await matches.startAttempt(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      clientEventId: "r2-b2-start",
    });
    clock.advanceSeconds(1);
    await matches.updateAttemptProgress(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      attemptId: b2.attemptId!,
      progressPercent: 42,
    });
    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.spectator).toEqual({ active: false });
    expect(state.spectator).not.toHaveProperty("currentProgress");

    clock.advanceSeconds(1);
    const a2 = await matches.startAttempt(matchId, statusA.matchToken, playerA, {
      levelId: roundTwoLevelId,
      clientEventId: "r2-a2-start",
    });
    clock.advanceSeconds(1);
    await matches.endAttempt(matchId, statusA.matchToken, playerA, {
      levelId: roundTwoLevelId,
      attemptId: a2.attemptId!,
      clientEventId: "r2-a2-end",
      progressPercent: 100,
      cleared: true,
    });

    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.state).toBe("LAST_ATTEMPT_WINDOW");
    expect(state.currentRound.clears).toEqual({ A: 2, B: 1 });
    expect(state.spectator).toMatchObject({
      active: true,
      opponentName: "Player9102",
      currentProgress: 42,
    });
    const scoreBeforeTelemetry = structuredClone(state.currentRound.scores);
    const targetView = (await matches.state(
      matchId,
      statusB.matchToken,
      playerB,
    )) as Record<string, any>;
    expect(targetView.spectator).toEqual({ active: false });

    const blockedTriggerStart = await matches.startAttempt(
      matchId,
      statusA.matchToken,
      playerA,
      { levelId: roundTwoLevelId, clientEventId: "r2-a-spectator-start" },
    );
    expect(blockedTriggerStart).toMatchObject({
      accepted: false,
      reason: "LAST_ATTEMPT_TARGET_ONLY",
    });

    const rowsBeforeProgress = await database.query<{ count: number }>(
      "SELECT COUNT(*)::int AS count FROM ranked_attempts",
    );
    const versionBeforeProgress = await database.query<{ state_version: number }>(
      "SELECT state_version FROM ranked_matches WHERE id = $1",
      [matchId],
    );
    clock.advanceSeconds(1);
    const progressResult = await matches.updateAttemptProgress(
      matchId,
      statusB.matchToken,
      playerB,
      { levelId: roundTwoLevelId, attemptId: b2.attemptId!, progressPercent: 73 },
    );
    expect(progressResult).toMatchObject({ accepted: true, stored: true });
    const rowsAfterProgress = await database.query<{ count: number }>(
      "SELECT COUNT(*)::int AS count FROM ranked_attempts",
    );
    const versionAfterProgress = await database.query<{ state_version: number }>(
      "SELECT state_version FROM ranked_matches WHERE id = $1",
      [matchId],
    );
    expect(rowsAfterProgress.rows[0]?.count).toBe(rowsBeforeProgress.rows[0]?.count);
    expect(versionAfterProgress.rows[0]?.state_version).toBe(
      versionBeforeProgress.rows[0]?.state_version,
    );
    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.spectator.currentProgress).toBe(73);
    expect(state.currentRound.scores).toEqual(scoreBeforeTelemetry);

    clock.advanceSeconds(1);
    await matches.updateAttemptProgress(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      attemptId: b2.attemptId!,
      progressPercent: 83,
    });
    clock.advanceSeconds(1);
    await matches.endAttempt(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      attemptId: b2.attemptId!,
      clientEventId: "r2-b2-end",
      progressPercent: 83,
      cleared: false,
    });
    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.spectator).toMatchObject({ active: true, currentProgress: null });
    await expect(
      matches.updateAttemptProgress(matchId, statusB.matchToken, playerB, {
        levelId: roundTwoLevelId,
        attemptId: b2.attemptId!,
        progressPercent: 99,
      }),
    ).rejects.toThrow("Attempt progress requires the active server attempt");

    clock.advanceSeconds(1);
    const b3 = await matches.startAttempt(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      clientEventId: "r2-b3-start",
    });
    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.spectator.currentProgress).toBe(0);
    clock.advanceSeconds(1);
    await matches.updateAttemptProgress(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      attemptId: b3.attemptId!,
      progressPercent: 91,
    });
    clock.advanceSeconds(10);
    await matches.endAttempt(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      attemptId: b3.attemptId!,
      clientEventId: "r2-b3-end",
      progressPercent: 100,
      cleared: true,
    });

    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.state).toBe("MATCH_RESULT");
    expect(state.result.winnerSide).toBe("A");
    expect(state.currentRound.outcome).toMatchObject({
      result: "DRAW",
      reason: "LAST_ATTEMPT_CLEAR",
    });
    expect(state.spectator).toEqual({ active: false });

    const afterFirstPoll = await database.query<{
      player_id: string;
      placement_games: number;
      hidden_mmr: number;
      visible_ranked_score: number;
      wins: number;
      losses: number;
    }>(`SELECT player_id, placement_games, hidden_mmr, visible_ranked_score, wins, losses
        FROM ranked_profiles ORDER BY player_id`);
    await matches.state(matchId, statusB.matchToken, playerB);
    const afterRepeatedPoll = await database.query<{
      player_id: string;
      placement_games: number;
      hidden_mmr: number;
      visible_ranked_score: number;
      wins: number;
      losses: number;
    }>(`SELECT player_id, placement_games, hidden_mmr, visible_ranked_score, wins, losses
        FROM ranked_profiles ORDER BY player_id`);
    expect(afterRepeatedPoll.rows).toEqual(afterFirstPoll.rows);
    expect(afterRepeatedPoll.rows.every((profile) => Number(profile.placement_games) === 1)).toBe(true);
    expect(afterRepeatedPoll.rows.reduce((sum, profile) => sum + Number(profile.wins), 0)).toBe(1);
    expect(afterRepeatedPoll.rows.reduce((sum, profile) => sum + Number(profile.losses), 0)).toBe(1);
    expect(
      afterRepeatedPoll.rows.every(
        (profile) => Number(profile.visible_ranked_score) === Number(profile.hidden_mmr),
      ),
    ).toBe(true);

    const attempts = await database.query<{ count: number }>(
      "SELECT COUNT(*)::int AS count FROM ranked_attempts",
    );
    expect(attempts.rows[0]?.count).toBe(7);
    const mapPersistence = await database.query<{
      canonical_level_id: string;
      alternate_level_id: string | null;
      playable_level_id: string;
      level_id: string;
    }>(
      `SELECT canonical_level_id, alternate_level_id, playable_level_id, level_id
       FROM ranked_rounds WHERE match_id = $1 ORDER BY round_number`,
      [matchId],
    );
    expect(mapPersistence.rows).toHaveLength(2);
    expect(
      mapPersistence.rows.every(
        (round) =>
          round.playable_level_id === round.alternate_level_id &&
          round.level_id === round.playable_level_id &&
          round.canonical_level_id !== round.playable_level_id,
      ),
    ).toBe(true);
    const wrongPlayedIds = await database.query<{ count: number }>(
      `SELECT COUNT(*)::int AS count
       FROM ranked_attempts attempt
       JOIN ranked_rounds round ON round.id = attempt.round_id
       WHERE round.match_id = $1 AND attempt.played_level_id <> round.playable_level_id`,
      [matchId],
    );
    expect(wrongPlayedIds.rows[0]?.count).toBe(0);

    const relayEvents = await database.query<{ event_type: string; count: number }>(
      `SELECT event_type, COUNT(*)::int AS count
       FROM ranked_outbox_events
       GROUP BY event_type
       ORDER BY event_type`,
    );
    expect(relayEvents.rows).toEqual([
      { event_type: "CLEAR_EVENT", count: 6 },
      { event_type: "LAST_ATTEMPT", count: 1 },
      { event_type: "MATCH_RESULT", count: 1 },
      { event_type: "ROUND_RESULT", count: 2 },
      { event_type: "ROUND_START", count: 2 },
    ]);
  });
});
