import { describe, expect, it } from "vitest";
import {
  DOCUMENT_RULES_V0_4,
  IMPLAUSIBLE_ATTEMPT_TIMING,
  attemptTimingPlausible,
  createRound,
  endRoundAttempt,
  gdLengthLowerBoundSeconds,
  maximumPlausibleProgress,
  minimumAttemptElapsedMs,
  startRoundAttempt,
} from "../src/index.js";
import { DEFAULT_SURVIVAL_CONFIG } from "../src/survival.js";
import { createSurvivalMatch, reduceSurvivalMatch, survivalCurrentRound, type SurvivalCommand, type SurvivalMatch } from "../src/survival-match.js";
import { survivalMapRange, type SurvivalVerifiedMap } from "../src/survival-maps.js";
import { makeMap } from "./fixtures.js";

describe("attempt timing plausibility", () => {
  it("requires half the estimated duration minus slack for a clear, never below two seconds", () => {
    expect(minimumAttemptElapsedMs(90, 100, true)).toBe(43_000);
    expect(minimumAttemptElapsedMs(4, 100, true)).toBe(2_000);
    expect(attemptTimingPlausible(42_999, 90, 100, true)).toBe(false);
    expect(attemptTimingPlausible(43_000, 90, 100, true)).toBe(true);
  });

  it("bounds partial progress with GD's fastest/slowest speed split", () => {
    // 50% at the most favourable speed split takes ~30% of the run.
    const half = minimumAttemptElapsedMs(100, 50, false);
    expect(half).toBeGreaterThan(12_000);
    expect(half).toBeLessThan(14_000);
    expect(minimumAttemptElapsedMs(100, 0, false)).toBe(0);
    expect(minimumAttemptElapsedMs(100, 10, false)).toBeLessThan(1_000);
  });

  it("never bounds a map without a duration estimate", () => {
    for (const estimate of [null, undefined, 0, -5, Number.NaN]) {
      expect(minimumAttemptElapsedMs(estimate, 100, true)).toBe(0);
      expect(maximumPlausibleProgress(0, estimate)).toBe(100);
    }
  });

  it("caps streamed progress below a clear until the clear is plausible, consistently with the minimums", () => {
    // Two seconds of slack already allow ~14% on a 60 s estimate.
    expect(maximumPlausibleProgress(0, 60)).toBeLessThan(20);
    expect(maximumPlausibleProgress(27_999, 60)).toBeLessThan(100);
    expect(maximumPlausibleProgress(28_000, 60)).toBe(100);
    for (const elapsed of [1_000, 5_000, 12_000, 20_000]) {
      const cap = maximumPlausibleProgress(elapsed, 60);
      expect(attemptTimingPlausible(elapsed, 60, cap, false)).toBe(true);
    }
  });

  it("maps GD length categories to their lower bounds", () => {
    expect([0, 1, 2, 3, 4].map(gdLengthLowerBoundSeconds)).toEqual([0, 10, 30, 60, 120]);
    expect(gdLengthLowerBoundSeconds(5)).toBeNull();
    expect(gdLengthLowerBoundSeconds(undefined)).toBeNull();
  });
});

describe("Solo round attempt plausibility", () => {
  const START = 1_000_000;
  const base = makeMap(3, 1);
  const snapshot = {
    levelId: base.levelId, canonicalLevelId: base.canonicalLevelId, alternateLevelId: base.alternateLevelId,
    playableLevelId: base.playableLevelId, title: base.title, creator: base.creator, difficulty: base.difficulty,
    pool: base.pool, qualifyingPercent: 60,
    metadata: { kind: "DEMON" as const, stars: 10, gddlTier: null, gddlColor: null, durationSeconds: 90, officialLevelId: null },
  };

  it("voids a clear reported faster than any legitimate run and keeps the round playable", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_4, START);
    const started = startRoundAttempt(state, "A", START + 1_000, "a-start");
    state = started.state;
    const fast = endRoundAttempt(state, "A", started.attemptId!, START + 5_000, 100, true, "a-end");
    expect(fast).toMatchObject({ accepted: true, reason: IMPLAUSIBLE_ATTEMPT_TIMING });
    expect(fast.state.attempts.A[0]).toMatchObject({ valid: false, cleared: false, awardedScore: 0, invalidReason: IMPLAUSIBLE_ATTEMPT_TIMING });
    expect(fast.state.scores.A).toBe(0);
    expect(fast.state.clears.A).toBe(0);
    const again = startRoundAttempt(fast.state, "A", START + 6_000, "a2-start");
    expect(again.accepted).toBe(true);
  });

  it("scores a clear that took a plausible time and ignores non-scoring progress", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_4, START);
    const first = startRoundAttempt(state, "A", START + 1_000, "a-start");
    const early = endRoundAttempt(first.state, "A", first.attemptId!, START + 1_500, 30, false, "a-death");
    expect(early.reason).toBeNull();
    expect(early.state.attempts.A[0]).toMatchObject({ valid: true, progressPercent: 30 });
    state = early.state;
    const second = startRoundAttempt(state, "A", START + 2_000, "a2-start");
    const clear = endRoundAttempt(second.state, "A", second.attemptId!, START + 2_000 + 80_000, 100, true, "a2-end");
    expect(clear.reason).toBeNull();
    expect(clear.state.clears.A).toBe(1);
    expect(clear.state.scores.A).toBe(250);
  });
});

describe("Survival progress plausibility", () => {
  const rig = (length: number | undefined) => {
    let now = Date.parse("2026-11-12T10:00:00Z");
    let match: SurvivalMatch = createSurvivalMatch("plausible", Array.from({ length: 16 }, (_, i) => ({
      participantId: `p${i + 1}`, playerId: i === 0 ? "human" : null, name: `Player ${i + 1}`,
      kind: i === 0 ? "HUMAN" as const : "BOT" as const, lp: 150,
    })), DEFAULT_SURVIVAL_CONFIG, "plausibility", now);
    const send = (command: SurvivalCommand) => { match = reduceSurvivalMatch(match, command, now); };
    const round = () => survivalCurrentRound(match);
    const range = survivalMapRange(match.difficultyGroup, round().phase);
    const map: SurvivalVerifiedMap = {
      levelId: 40_000_001, stars: range.minimumStars ?? 10, demon: range.minimumStars === null,
      demonDifficulty: range.minimumStars === null ? range.demons[0]! : null, automatic: false, platformer: false,
      ...(length === undefined ? {} : { length }), levelVersion: 1, geometrySha256: "a".repeat(64), geometryBytes: 100,
      verifiedAt: new Date(now).toISOString(),
    };
    send({ type: "MAP_SELECTED", roundKey: round().key, mapGeneration: 1, map });
    const context = () => ({ roundKey: round().key, mapGeneration: round().mapGeneration, levelId: map.levelId });
    for (const p of round().players) send({ type: "ASSETS_READY", ...context(), participantId: p.participantId, certificate: map });
    now += 5_000;
    for (const p of round().players) send({ type: "ENTERED", ...context(), participantId: p.participantId });
    now = round().startsAt!;
    send({ type: "TICK" });
    return {
      start: (id: string, attemptId: string) => send({ type: "ATTEMPT_START", ...context(), participantId: id, attemptId }),
      end: (id: string, attemptId: string, progress: number, afterMs: number) => {
        now += afterMs;
        send({ type: "ATTEMPT_END", ...context(), participantId: id, attemptId, progress });
      },
      player: (id: string) => round().players.find((p) => p.participantId === id)!,
    };
  };

  it("caps a human's instant clear on a Long map but not a bot's", () => {
    const r = rig(3);
    r.start("p1", "h1");
    r.start("p2", "b1");
    r.end("p1", "h1", 100, 1_000);
    r.end("p2", "b1", 100, 0);
    expect(r.player("p1").highestProgress).toBeLessThan(25);
    expect(r.player("p2").highestProgress).toBe(100);
  });

  it("accepts a human clear that took a plausible time", () => {
    const r = rig(1);
    r.start("p1", "h1");
    r.end("p1", "h1", 100, 6_000);
    expect(r.player("p1").highestProgress).toBe(100);
  });

  it("leaves maps without a GD length unbounded", () => {
    const r = rig(undefined);
    r.start("p1", "h1");
    r.end("p1", "h1", 100, 0);
    expect(r.player("p1").highestProgress).toBe(100);
  });
});
