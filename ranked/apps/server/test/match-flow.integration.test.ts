import { enterPreparedMatch } from "./native-entry-helper.js";
import { readFile, readdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { SeededRandom, scoreAttempt, expandSoloTierBands, type CsmpTier } from "@corum-ranked/rules";
import type { IdGenerator, ServerClock } from "../src/common/runtime.module.js";
import { TokenService } from "../src/common/token.service.js";
import type { CsmpTierSource } from "../src/config/csmp-tier.source.js";
import { RankedConfigService } from "../src/config/ranked-config.service.js";
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
      // Exercise the real Corum case where Qualifying can be decimal. Clear
      // Keep decimal qualifying in the match snapshot even though Clear is now
      // a fixed 200 points and 70-99% receives the x1.5 multiplier.
      // for the client instead of silently truncating to an integer.
      maps: baseDocument.maps.map((map) => ({ ...map, qualifyingPercent: 20.1,
        metadata: { kind:"NORMAL" as const,stars:8,gddlTier:null,gddlColor:null,durationSeconds:84,officialLevelId:null } })),
      operational: {
        ...baseDocument.operational,
        timeouts: {
          ...baseDocument.operational.timeouts!,
          reconnectGraceSeconds: 10_000,
        },
      },
    };
    let liveDocument = document;
    const config = new RankedConfigService({ fetchConfig: async () => structuredClone(liveDocument) }, environmentFixture(), database);
    expect(await config.refresh()).toBe(true);
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
    const matchRandom = new SeededRandom(91);
    let matches = new MatchService(
      database,
      clock,
      ids,
      matchRandom,
      new MatchAccessService(tokens, clock),
      sessions,
      new OutboxService(ids),
      new AchievementService(database, clock, ids),
      new InMemoryMatchRuntimeState(),
    );
    const queue = new QueueService(
      database, clock, ids, new SeededRandom(90), config, sessions, tokens, matches,
    );
    const playerA = await createContext("9101", sessions, tokens);
    const playerB = await createContext("9102", sessions, tokens);
    const unrestrictedMods=[cleanMods[0]!,{id:"other.mod",version:"1",enabled:true,loaded:true,internal:false,system:false}];
    await expect(queue.join(playerA,{installedMods:unrestrictedMods})).rejects.toMatchObject({status:403});
    Object.assign(liveDocument.operational,{modRestrictionEnabled:false});expect(await config.refresh()).toBe(true);
    await queue.join(playerA, { installedMods: unrestrictedMods });
    await queue.join(playerB, { installedMods: unrestrictedMods });
    Object.assign(liveDocument.operational,{modRestrictionEnabled:true});expect(await config.refresh()).toBe(true);
    await expect(queue.join(playerA,{installedMods:unrestrictedMods})).rejects.toMatchObject({status:403});
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
    // Operations may close Solo after assignment. The admitted snapshot must
    // still run both rounds, settle LP/MMR and preserve its achievement results.
    liveDocument = { ...document, operational: { ...document.operational, enabled: false } };
    expect(await config.refresh()).toBe(true);
    expect(config.getStatus().queueEnabled).toBe(false);
    await expect(queue.join(playerA, { installedMods: structuredClone(cleanMods) })).rejects.toThrow("not operationally configured");

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

    const stateAfterABan = (await matches.submitBan(matchId, statusA.matchToken, playerA, {
      canonicalLevelId: sharedBan,
    })) as Record<string, any>;
    expect(stateAfterABan.state).toBe("BAN_PHASE");
    expect(stateAfterABan.banStatus).toEqual({
      confirmed: true,
      canonicalLevelId: sharedBan,
    });
    expect(stateAfterABan.bans).toBeNull();

    const stateSeenByB = (await matches.state(
      matchId,
      statusB.matchToken,
      playerB,
    )) as Record<string, any>;
    expect(stateSeenByB.banStatus).toEqual({
      confirmed: false,
      canonicalLevelId: null,
    });
    expect(stateSeenByB.bans).toBeNull();

    state = (await matches.submitBan(matchId, statusB.matchToken, playerB, {
      canonicalLevelId: sharedBan,
    })) as Record<string, any>;
    expect(state.state).toBe("ROUND_PREPARE");
    expect(state.currentRound.roundNumber).toBe(1);
    expect(state.currentRound.map).toBeTruthy();
    expect(state.currentRound.map.canonicalLevelId).not.toBe(sharedBan);
    expect(state.currentRound.map.metadata).toMatchObject({kind:"NORMAL",stars:8,durationSeconds:84});
    expect(state.bans).toEqual({ A: sharedBan, B: sharedBan });
    expect(state.selectedRoundMaps).toBeUndefined();
    expect(state.candidateMaps).toBeNull();
    expect(Date.parse(state.deadlineAt as string) - clock.now().getTime()).toBe(60_000);

    // Regression for beta.10: the visible ten-second PVP countdown must not be
    // the authoritative resource timeout. A map that completes around T=0 still
    // needs time for the client's Ready HTTP request to reach the server.
    clock.advanceSeconds(10);
    state = (await matches.state(
      matchId,
      statusA.matchToken,
      playerA,
    )) as Record<string, any>;
    expect(state.state).toBe("ROUND_PREPARE");

    const winRoundWithThreeClears = async (roundNumber: number) => {
      await matches.ready(matchId, statusA.matchToken, playerA, {
        installedMods: structuredClone(cleanMods),
      });
      await matches.ready(matchId, statusB.matchToken, playerB, {
        installedMods: structuredClone(cleanMods),
      });
      const playing = await enterPreparedMatch(matches, matchId, statusA.matchToken, playerA,
        statusB.matchToken, playerB, (seconds) => clock.advanceSeconds(seconds));
      expect(playing.state).toBe("ROUND_PLAYING");
      expect(playing.currentRound.roundNumber).toBe(roundNumber);
      const roundStartedAtMs = clock.now().getTime();
      const levelId = playing.currentRound.map.playableLevelId as string;
      const qualifying = Number(playing.currentRound.map.qualifyingPercent);
      // Two opponent Clears keep the committed gap below 250 while the
      // winner's third Clear is waiting behind its pre-deadline Start lease.
      for (let index = 0; index < 2; ++index) {
        const b = await matches.startAttempt(matchId, statusB.matchToken, playerB,
          { levelId, clientEventId: `r${roundNumber}-lease-b${index}-start` });
        await matches.endAttempt(matchId, statusB.matchToken, playerB,
          { levelId, attemptId: b.attemptId!, clientEventId: `r${roundNumber}-lease-b${index}-end`, progressPercent: 100, cleared: true });
      }


      clock.advanceSeconds(1);
      const firstStartedAt = clock.now().toISOString();
      const firstIntent = await matches.startAttemptIntent(matchId, statusA.matchToken, playerA, {
        levelId,
        clientEventId: `r${roundNumber}-a1-start`,
        clientStartedAt: firstStartedAt,
      });
      expect(firstIntent).toMatchObject({ accepted: true });
      const firstStart = await matches.startAttempt(matchId, statusA.matchToken, playerA, {
        levelId,
        clientEventId: `r${roundNumber}-a1-start`,
        clientStartedAt: firstStartedAt,
      });
      const livePercent = Math.min(99, Math.max(Math.ceil(qualifying), 1));
      const progressAck = await matches.updateAttemptProgress(matchId, statusA.matchToken, playerA, {
        levelId,
        attemptId: firstStart.attemptId!,
        progressPercent: livePercent,
      });
      if (!("roundSnapshot" in progressAck)) {
        throw new Error("Expected round progress acknowledgement during ROUND_PLAYING");
      }
      expect(progressAck.roundSnapshot.displayScores.A).toBe(
        scoreAttempt(livePercent, false, qualifying),
      );
      clock.advanceSeconds(1);
      const firstEnd = await matches.endAttempt(matchId, statusA.matchToken, playerA, {
        levelId,
        attemptId: firstStart.attemptId!,
        clientEventId: `r${roundNumber}-a1-end`,
        progressPercent: livePercent,
        cleared: false,
      });
      if (!("roundSnapshot" in firstEnd)) {
        throw new Error("Expected round attempt-end acknowledgement during ROUND_PLAYING");
      }
      expect(firstEnd.roundSnapshot.clears.A).toBe(0);
      expect(firstEnd.roundSnapshot.scores.A).toBe(
        scoreAttempt(livePercent, false, qualifying),
      );

      clock.advanceSeconds(1);
      const secondStart = await matches.startAttempt(matchId, statusA.matchToken, playerA, {
        levelId,
        clientEventId: `r${roundNumber}-a2-start`,
      });
      clock.advanceSeconds(1);
      const secondEnd = await matches.endAttempt(matchId, statusA.matchToken, playerA, {
        levelId,
        attemptId: secondStart.attemptId!,
        clientEventId: `r${roundNumber}-a2-end`,
        progressPercent: 100,
        cleared: true,
      });
      if (!("roundSnapshot" in secondEnd)) {
        throw new Error("Expected first Clear acknowledgement during ROUND_PLAYING");
      }
      expect(secondEnd.roundSnapshot.clears.A).toBe(1);
      expect(secondEnd.roundSnapshot.scores.A).toBeCloseTo(
        scoreAttempt(livePercent, false, qualifying) + 250,
        6,
      );

      clock.advanceSeconds(1);
      const thirdStart = await matches.startAttempt(matchId, statusA.matchToken, playerA, {
        levelId,
        clientEventId: `r${roundNumber}-a3-start`,
      });
      clock.advanceSeconds(1);
      const thirdEnd = await matches.endAttempt(matchId, statusA.matchToken, playerA, {
        levelId,
        attemptId: thirdStart.attemptId!,
        clientEventId: `r${roundNumber}-a3-end`,
        progressPercent: 100,
        cleared: true,
      });
      if (!("roundSnapshot" in thirdEnd)) {
        throw new Error("Expected second Clear acknowledgement during ROUND_PLAYING");
      }
      expect(thirdEnd.roundSnapshot.clears.A).toBe(2);

      // Regression for the real alpha.32 Round-2 Score 0 race: the visual
      // third Clear can start before the final start deadline while its
      // authoritative Start is still queued behind an earlier End/ACK. Publish
      // the lease first, move the server clock beyond the deadline, poll once,
      // then let the serialized Start/End catch up. The Round must not finalize
      // to Score 0 in between.
      const finalStartDeadlineMs = roundStartedAtMs +
        (document.operational.rules.roundSeconds +
          document.operational.rules.finalAttemptWindowSeconds) * 1_000;
      const millisUntilOneSecondBeforeDeadline = finalStartDeadlineMs - 1_000 - clock.now().getTime();
      if (millisUntilOneSecondBeforeDeadline > 0) {
        clock.advanceSeconds(millisUntilOneSecondBeforeDeadline / 1_000);
      }
      const fourthStartedAt = clock.now().toISOString();
      const fourthIntent = await matches.startAttemptIntent(matchId, statusA.matchToken, playerA, {
        levelId,
        clientEventId: `r${roundNumber}-a4-start`,
        clientStartedAt: fourthStartedAt,
      });
      expect(fourthIntent).toMatchObject({ accepted: true });

      clock.advanceSeconds(3);
      const heldForLease = (await matches.state(
        matchId,
        statusA.matchToken,
        playerA,
      )) as Record<string, any>;
      expect(heldForLease.state).not.toBe("ROUND_RESULT");

      const fourthStart = await matches.startAttempt(matchId, statusA.matchToken, playerA, {
        levelId,
        clientEventId: `r${roundNumber}-a4-start`,
        clientStartedAt: fourthStartedAt,
      });
      expect(fourthStart).toMatchObject({ accepted: true });
      clock.advanceSeconds(1);
      const fourthEnd = await matches.endAttempt(matchId, statusA.matchToken, playerA, {
        levelId,
        attemptId: fourthStart.attemptId!,
        clientEventId: `r${roundNumber}-a4-end`,
        progressPercent: 100,
        cleared: true,
      });
      expect(fourthEnd).toMatchObject({ accepted: true, awardedScore: 250 });
      if (!("roundSnapshot" in fourthEnd)) {
        throw new Error("Expected third Clear acknowledgement after delayed Start Lease reconciliation");
      }
      expect(fourthEnd.roundSnapshot.clears.A).toBe(3);
    };

    await winRoundWithThreeClears(1);
    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    // The third Clear arrived after the fixed final deadline: do not create
    // another ten seconds. With no remaining opponent Attempt, settle now.
    expect(state.state).toBe("ROUND_RESULT");
    expect(state.currentRound.lastAttemptWindow).toMatchObject({ triggerSide: "A", targetSide: "B" });
    expect(state.currentRound.outcome).toMatchObject({ winner: "A", reason: "LAST_ATTEMPT_EXPIRED" });

    clock.advanceSeconds(6);
    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.state).toBe("ROUND_PREPARE");
    expect(state.currentRound.roundNumber).toBe(2);
    expect(state.currentRound.banner).toBe("MATCH_POINT");
    expect(state.currentRound.scores).toEqual({ A: 0, B: 0 });
    expect(state.currentRound.displayScores).toEqual({ A: 0, B: 0 });
    expect(state.currentRound.clears).toEqual({ A: 0, B: 0 });
    expect(state.opponentProgress).toBeNull();

    await matches.ready(matchId, statusA.matchToken, playerA, {
      installedMods: structuredClone(cleanMods),
    });
    await matches.ready(matchId, statusB.matchToken, playerB, {
      installedMods: structuredClone(cleanMods),
    });
    const roundTwoPlaying = await enterPreparedMatch(matches, matchId, statusA.matchToken, playerA,
      statusB.matchToken, playerB, (seconds) => clock.advanceSeconds(seconds));
    expect(roundTwoPlaying.state).toBe("ROUND_PLAYING");
    expect(roundTwoPlaying.currentRound.clears).toEqual({ A: 0, B: 0 });
    const snapshottedQualifying = roundTwoPlaying.currentRound.map.qualifyingPercent as number;
    const roundTwoLevelId = roundTwoPlaying.currentRound.map.playableLevelId as string;
    liveDocument = {
      ...document,
      maps: document.maps.map((map) => ({ ...map, qualifyingPercent: 99,
        metadata: {...map.metadata,stars:6,durationSeconds:999} })),
      operational: { ...document.operational, enabled: false, tierBands: document.operational.tierBands.map((band) => ({
        ...band, minInclusive: band.tier === "RED" ? band.minInclusive : band.minInclusive + 10,
        maxExclusive: band.maxExclusive === null ? null : band.maxExclusive + 10,
      })) },
    };
    expect(await config.refresh()).toBe(true);
    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.currentRound.map.qualifyingPercent).toBe(snapshottedQualifying);
    expect(state.currentRound.map.metadata).toMatchObject({stars:8,durationSeconds:84});

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
    await matches.endAttempt(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      attemptId: b2.attemptId!,
      clientEventId: "r2-b2-end",
      progressPercent: 100,
      cleared: true,
    });
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

    clock.advanceSeconds(1);
    const b3 = await matches.startAttempt(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      clientEventId: "r2-b3-start",
    });
    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.opponentProgress).toBe(0);
    clock.advanceSeconds(1);
    await matches.updateAttemptProgress(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      attemptId: b3.attemptId!,
      progressPercent: 42,
    });
    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.opponentProgress).toBe(42);
    expect(state.spectator).toEqual({ active: false });
    expect(state.spectator).not.toHaveProperty("currentProgress");

    clock.advanceSeconds(1);
    const a3 = await matches.startAttempt(matchId, statusA.matchToken, playerA, {
      levelId: roundTwoLevelId,
      clientEventId: "r2-a3-start",
    });
    clock.advanceSeconds(1);
    await matches.endAttempt(matchId, statusA.matchToken, playerA, {
      levelId: roundTwoLevelId,
      attemptId: a3.attemptId!,
      clientEventId: "r2-a3-end",
      progressPercent: 100,
      cleared: true,
    });

    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.state).toBe("LAST_ATTEMPT_WINDOW");
    expect(state.currentRound.clears).toEqual({ A: 3, B: 2 });
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
    expect(targetView.opponentProgress).toBe(100);
    expect(targetView.opponentFinished).toBe(true);

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
      { levelId: roundTwoLevelId, attemptId: b3.attemptId!, progressPercent: 73 },
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
    expect(state.currentRound.displayScores.B).toBe(scoreBeforeTelemetry.B + scoreAttempt(73, false, snapshottedQualifying));

    clock.advanceSeconds(1);
    await matches.updateAttemptProgress(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      attemptId: b3.attemptId!,
      progressPercent: 83,
    });
    clock.advanceSeconds(1);
    await matches.endAttempt(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      attemptId: b3.attemptId!,
      clientEventId: "r2-b3-end",
      progressPercent: 83,
      cleared: false,
    });
    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.spectator).toMatchObject({ active: true, currentProgress: null });
    expect(state.opponentFinished).toBe(false);
    await expect(
      matches.updateAttemptProgress(matchId, statusB.matchToken, playerB, {
        levelId: roundTwoLevelId,
        attemptId: b3.attemptId!,
        progressPercent: 99,
      }),
    ).rejects.toThrow("Attempt progress requires the active server attempt");

    clock.advanceSeconds(1);
    const b4StartedAt = clock.now().toISOString();
    const b4Intent = await matches.startAttemptIntent(
      matchId,
      statusB.matchToken,
      playerB,
      {
        levelId: roundTwoLevelId,
        clientEventId: "r2-b4-start",
        clientStartedAt: b4StartedAt,
      },
    );
    expect(b4Intent).toMatchObject({ accepted: true, duplicate: false });

    // Simulate a Render process restart after the visual Start lease reached
    // PostgreSQL but before the serialized authoritative Start request arrived.
    // alpha.33 must not depend on InMemoryMatchRuntimeState for this protection.
    matches = new MatchService(
      database,
      clock,
      ids,
      matchRandom,
      new MatchAccessService(tokens, clock),
      sessions,
      new OutboxService(ids),
      new AchievementService(database, clock, ids),
      new InMemoryMatchRuntimeState(),
    );

    // The intent is only a lease for a visual attempt that already started
    // inside the 10-second window. Even if the serialized authoritative Start
    // is delayed for longer than alpha.27's old 30-second hold, the round must
    // remain open while the connected player is still reconciling it.
    clock.advanceSeconds(35);
    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.state).not.toBe("ROUND_RESULT");
    expect(state.opponentFinished).toBe(false);

    const b4 = await matches.startAttempt(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      clientEventId: "r2-b4-start",
      clientStartedAt: b4StartedAt,
    });
    expect(b4).toMatchObject({ accepted: true });
    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.spectator.currentProgress).toBe(0);
    expect(state.opponentFinished).toBe(false);
    clock.advanceSeconds(1);
    await matches.updateAttemptProgress(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      attemptId: b4.attemptId!,
      progressPercent: 91,
    });

    // Active attempts do not have an absolute lifetime. This deliberately
    // exceeds orphanAttemptSeconds (120s in the fixture); it must still be live.
    clock.advanceSeconds(130);
    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.state).toBe("ROUND_SETTLING");
    expect(state.spectator).toMatchObject({ active: true, currentProgress: 91 });
    expect(state.currentRound.displayScores.B).toBe(state.currentRound.scores.B + scoreAttempt(91, false, snapshottedQualifying));
    await matches.endAttempt(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      attemptId: b4.attemptId!,
      clientEventId: "r2-b4-end",
      progressPercent: 100,
      cleared: true,
    });

    state = (await matches.state(matchId, statusA.matchToken, playerA)) as Record<string, any>;
    expect(state.state).toBe("MATCH_RESULT");
    expect(state.result.winnerSide).toBe("A");
    expect(state.result.profileBefore.A).toMatchObject({wins:0,losses:0});
    expect(state.result.profileBefore.B).toMatchObject({wins:0,losses:0});
    expect(state.result.profileAfter.A).toMatchObject({wins:1,losses:0});
    expect(state.result.profileAfter.B).toMatchObject({wins:0,losses:1});
    expect(state.players.A).toMatchObject({wins:1,losses:0});
    expect(state.players.B).toMatchObject({wins:0,losses:1});
    expect(state.result.scorePresentation.version).toBe(1);
    expect(state.result.scorePresentation.tierBands).toEqual([...expandSoloTierBands(document.operational.tierBands)]
      .sort((a,b) => a.minInclusive - b.minInclusive).map(({tier,minInclusive}) => ({tier,minInclusive})));
    const resultScoreSnapshot = structuredClone(state.result.scorePresentation);
    // The persisted result must keep its ladder/target snapshot even after the
    // live ladder changes; normal state polling must never randomize it again.
    await database.query("UPDATE ranked_profiles SET visible_ranked_score = visible_ranked_score + 999 WHERE player_id=$1",[playerA.playerId]);
    const replay = await matches.state(matchId,statusB.matchToken,playerB) as Record<string,any>;
    expect(replay.result.scorePresentation).toEqual(resultScoreSnapshot);
    await database.query("UPDATE ranked_profiles SET visible_ranked_score = visible_ranked_score - 999 WHERE player_id=$1",[playerA.playerId]);
    expect(state.currentRound.outcome).toMatchObject({
      result: "DRAW",
      reason: "LAST_ATTEMPT_CLEAR",
    });
    expect(state.spectator).toEqual({ active: false });
    expect(state.opponentFinished).toBe(true);

    // Simulate a lost HTTP acknowledgement for the final End. Retrying after
    // MATCH_RESULT must acknowledge the already-committed attempt instead of
    // rejecting it because the Round is immutable.
    const duplicateFinalEnd = await matches.endAttempt(matchId, statusB.matchToken, playerB, {
      levelId: roundTwoLevelId,
      attemptId: b4.attemptId!,
      clientEventId: "r2-b4-end",
      progressPercent: 100,
      cleared: true,
    });
    expect(duplicateFinalEnd).toMatchObject({
      accepted: true,
      duplicate: true,
      reason: "ATTEMPT_ALREADY_ENDED",
      awardedScore: 250,
    });

    const afterFirstPoll = await database.query<{
      player_id: string;
      placement_games: number;
      hidden_mmr: number;
      wins: number;
      losses: number;
    }>("SELECT player_id, placement_games, hidden_mmr, wins, losses FROM ranked_profiles ORDER BY player_id");
    await matches.state(matchId, statusB.matchToken, playerB);
    const afterRepeatedPoll = await database.query<{
      player_id: string;
      placement_games: number;
      hidden_mmr: number;
      wins: number;
      losses: number;
    }>("SELECT player_id, placement_games, hidden_mmr, wins, losses FROM ranked_profiles ORDER BY player_id");
    expect(afterRepeatedPoll.rows).toEqual(afterFirstPoll.rows);
    expect(afterRepeatedPoll.rows.every((profile) => Number(profile.placement_games) === 5)).toBe(true);
    expect(afterRepeatedPoll.rows.reduce((sum, profile) => sum + Number(profile.wins), 0)).toBe(1);
    expect(afterRepeatedPoll.rows.reduce((sum, profile) => sum + Number(profile.losses), 0)).toBe(1);

    const attempts = await database.query<{ count: number }>(
      "SELECT COUNT(*)::int AS count FROM ranked_attempts",
    );
    expect(attempts.rows[0]?.count).toBe(13);

    const relayEvents = await database.query<{ event_type: string; count: number }>(
      `SELECT event_type, COUNT(*)::int AS count
       FROM ranked_outbox_events
       GROUP BY event_type
       ORDER BY event_type`,
    );
    expect(relayEvents.rows).toEqual([
      { event_type: "CLEAR_EVENT", count: 11 },
      { event_type: "LAST_ATTEMPT", count: 2 },
      { event_type: "MATCH_RESULT", count: 1 },
      { event_type: "ROUND_RESULT", count: 2 },
      { event_type: "ROUND_START", count: 2 },
    ]);
  });
});
