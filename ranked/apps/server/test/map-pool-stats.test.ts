import { describe, expect, it } from "vitest";
import {
  deathmatchWinnerDeficit, difficultyRank, mapDetailStats, myMapRecord, normalRoundWinnerDeficit,
  type AttemptRow, type DeathmatchAttemptRow, type DeathmatchRow, type RoundRow,
} from "../src/map-pool/map-pool-stats.js";

const round = (id: string, result: RoundRow["result"], tier = "SILVER2"): RoundRow =>
  ({ id, result, effectiveTier: tier, playerA: "a", playerB: "b" });
let clock = 0;
const attempt = (roundId: string, playerId: string, sequence: number, score: number, progress: number, cleared = false): AttemptRow =>
  ({ roundId, playerId, sequence, endedAt: ++clock * 1000, progress, cleared, score });
const dmAttempt = (deathmatchId: string, playerId: string, progress: number, cleared = false): DeathmatchAttemptRow =>
  ({ deathmatchId, playerId, endedAt: ++clock * 1000, progress, cleared });

describe("map pool difficulty order", () => {
  it("orders GD difficulties easiest first and keeps unknown names last", () => {
    expect(["Hard Demon", "Easy", "Insane", "Demon", "Extreme Demon", "N/A", "Harder"]
      .sort((a, b) => difficultyRank(a) - difficultyRank(b)))
      .toEqual(["Easy", "Harder", "Insane", "Hard Demon", "Demon", "Extreme Demon", "N/A"]);
    expect(difficultyRank("medium_demon")).toBe(difficultyRank("Medium Demon"));
  });
});

describe("normal round comeback", () => {
  it("measures the winner's largest point deficit over time", () => {
    const r = round("r", "A");
    // B leads 120-0, then A answers with a clear (250) and wins 250-150.
    const rows = [attempt("r", "b", 1, 120, 80), attempt("r", "a", 1, 0, 10), attempt("r", "b", 2, 30, 30), attempt("r", "a", 2, 250, 100, true)];
    expect(normalRoundWinnerDeficit(r, rows)).toBe(150);
  });
  it("is not a comeback below 50 points or for a draw", () => {
    const rows = [attempt("r", "b", 1, 49, 49), attempt("r", "a", 1, 60, 60)];
    expect(normalRoundWinnerDeficit(round("r", "A"), rows)).toBe(49);
    expect(normalRoundWinnerDeficit(round("r", "DRAW"), rows)).toBe(0);
  });
  it("uses the order in which attempts ended, not the input order", () => {
    const early = attempt("r", "b", 1, 60, 60);
    const late = attempt("r", "a", 1, 200, 100, true);
    expect(normalRoundWinnerDeficit(round("r", "A"), [late, early])).toBe(60);
  });
});

describe("Death Match comeback", () => {
  const match: DeathmatchRow = { id: "d", playerA: "a", playerB: "b", winner: "b", startedAt: 0, finishedAt: 125_000 };
  it("compares best progress so far, a clear counting as 100%", () => {
    const rows = [dmAttempt("d", "a", 42), dmAttempt("d", "b", 30), dmAttempt("d", "b", 0, true)];
    expect(deathmatchWinnerDeficit(match, rows)).toBe(42);
  });
  it("needs a 10 point deficit for the rate", () => {
    const stats = mapDetailStats({ rounds: [], attempts: [], names: new Map([["b", "Bee"]]),
      deathmatches: [match, { ...match, id: "e", finishedAt: 30_000 }],
      deathmatchAttempts: [dmAttempt("d", "a", 20), dmAttempt("d", "b", 50), dmAttempt("e", "b", 15), dmAttempt("e", "a", 19), dmAttempt("e", "b", 25)] });
    expect(stats.comeback.deathmatch).toEqual({ eligible: 2, count: 1, rate: 50 });
    expect(stats.longestDeathmatch).toEqual({ seconds: 125, winner: "Bee" });
  });
});

describe("map detail statistics", () => {
  it("summarises clears, attempts, deaths, tiers and the fastest clear", () => {
    const rounds = [round("r1", "A", "SILVER1"), round("r2", "DRAW", "GOLD"), round("r3", "B", "SILVER3")];
    const attempts = [
      attempt("r1", "a", 1, 0, 12), attempt("r1", "a", 2, 250, 100, true), attempt("r1", "b", 1, 81, 47),
      attempt("r2", "a", 1, 90, 45), attempt("r2", "b", 1, 90, 45),
      attempt("r3", "a", 1, 150, 99), attempt("r3", "b", 1, 250, 100, true),
    ];
    const stats = mapDetailStats({ rounds, attempts, deathmatches: [], deathmatchAttempts: [], names: new Map([["a", "Ace"], ["b", "Bee"]]) });
    expect(stats.rounds).toBe(3);
    expect(stats.playerRounds).toBe(6);
    expect(stats.clearRate).toBe(33.3);
    expect(stats.firstAttemptClears).toBe(1);
    expect(stats.fastestClear).toEqual({ attempt: 1, player: "Bee" });
    expect(stats.averageAttempts).toBe(1.2);
    expect(stats.averageBestPercent).toBe(72.7);
    expect(stats.deaths).toBe(5);
    expect(stats.deathCounts).toEqual([0, 1, 0, 0, 3, 0, 0, 0, 0, 1]);
    expect(stats.deathBins[4]).toBe(60);
    expect(stats.tiers).toEqual([{ tier: "SILVER", rounds: 2, share: 66.7 }, { tier: "GOLD", rounds: 1, share: 33.3 }]);
    expect(stats.comeback.normal).toEqual({ eligible: 2, count: 1, rate: 50 });
  });
  it("reports empty maps without dividing by zero", () => {
    const stats = mapDetailStats({ rounds: [], attempts: [], deathmatches: [], deathmatchAttempts: [], names: new Map() });
    expect(stats).toMatchObject({ clearRate: null, averageAttempts: null, fastestClear: null, longestDeathmatch: null,
      comeback: { normal: { rate: null }, deathmatch: { rate: null } } });
    expect(stats.deathBins).toEqual(Array(10).fill(0));
  });
});

describe("my map record", () => {
  it("counts only the viewer's rounds", () => {
    const rounds = [round("r1", "A"), round("r2", "B"), round("r3", "DRAW"), { ...round("r4", "A"), playerA: "x", playerB: "y" }];
    const attempts = [attempt("r1", "a", 1, 150, 87), attempt("r2", "a", 1, 0, 30), attempt("r4", "x", 1, 250, 100, true)];
    expect(myMapRecord("a", rounds, attempts)).toEqual({ rounds: 3, wins: 1, losses: 1, draws: 1, bestPercent: 87 });
    expect(myMapRecord("nobody", rounds, attempts)).toEqual({ rounds: 0, wins: 0, losses: 0, draws: 0, bestPercent: null });
  });
});
