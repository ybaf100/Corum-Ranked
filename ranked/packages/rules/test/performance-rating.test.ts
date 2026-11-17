import { describe, expect, it } from "vitest";
import {
  DEFAULT_PERFORMANCE_RATING_POLICY as V2,
  calculatePerformanceMmrUpdate,
  matchDominance,
  updateRankedRatingProfile,
  visiblePerformanceBonus,
  type MatchPerformanceInput,
  type PlayerSide,
  type RankedRatingProfile,
} from "../src/index.js";
import { completeOperationalConfig, tierBandsFixture } from "./fixtures.js";

const policy = completeOperationalConfig().mmrPolicy!;
const profile = (overrides: Partial<RankedRatingProfile> = {}): RankedRatingProfile => ({
  hiddenMmr: 1_500,
  visibleRankedScore: 1_500,
  placementGames: policy.placementGames,
  placementResults: [],
  displayedTier: "AQUA",
  resultStreak: 0,
  promotion: null,
  demotionWarning: false,
  ...overrides,
});

// One match from A's side, as the design calculator takes it: A's MMR and
// LP 1500, the opponent's MMR relative to it, one pace entry per match.
interface Case {
  readonly won: boolean;
  readonly opponent?: number;
  readonly series: "20" | "21" | "dm" | "single";
  readonly score: readonly [number, number];
  readonly clears: readonly [number, number];
  readonly firstClear: readonly [number | null, number | null];
  readonly comeback?: boolean;
  readonly saved?: boolean;
}
const rate = (c: Case) => {
  const winner: PlayerSide = c.won ? "A" : "B";
  const series: Record<Case["series"], readonly [number, number]> = { "20": [2, 0], "21": [2, 1], dm: [1, 1], single: [1, 0] };
  const [w, l] = series[c.series];
  const input: MatchPerformanceInput = {
    winner,
    format: c.series === "single" ? "SINGLE" : "BO3",
    roundWinsA: c.won ? w : l,
    roundWinsB: c.won ? l : w,
    scoreA: c.score[0], scoreB: c.score[1], clearsA: c.clears[0], clearsB: c.clears[1],
    rounds: [{ roundSeconds: 300, firstClearSecondsA: c.firstClear[0], firstClearSecondsB: c.firstClear[1] }],
  };
  const dominance = matchDominance(input);
  const before = profile();
  const opponent = profile({ hiddenMmr: 1_500 + (c.opponent ?? 0) });
  const mmr = calculatePerformanceMmrUpdate({
    ratingA: before.hiddenMmr, ratingB: opponent.hiddenMmr, placementGamesA: policy.placementGames,
    placementGamesB: policy.placementGames, placementGames: policy.placementGames, winner, dominance: dominance.total,
  });
  const firstRound = c.comeback ? "B" : winner;
  const bonus = visiblePerformanceBonus("A", winner, firstRound, c.saved ? ["A"] : []);
  const lp = updateRankedRatingProfile(before, "A", winner, mmr.ratingAfterA, policy, tierBandsFixture,
    { opponentHiddenMmr: opponent.hiddenMmr, performance: { dominance: dominance.total, bonus, policy: V2 } });
  return { dominance: dominance.total, mmr: mmr.deltaA, lp: lp.visibleScoreDelta, mmrB: mmr.deltaB };
};

describe("Solo rating V2: the match's content moves MMR and, about half as far, LP", () => {
  it("matches the design sheet's example matches", () => {
    const close = { series: "21", score: [1260, 1140], clears: [4, 3], firstClear: [140, 150] } as const;
    const sweep = { series: "20", score: [1500, 520], clears: [6, 1], firstClear: [70, null] } as const;
    expect(rate({ won: true, ...close })).toMatchObject({ mmr: 36, lp: 23, mmrB: -36 });
    expect(rate({ won: true, ...sweep })).toMatchObject({ mmr: 46, lp: 26 });
    expect(rate({ won: false, series: "21", score: [1140, 1260], clears: [3, 4], firstClear: [150, 140] })).toMatchObject({ mmr: -36, lp: -23 });
    expect(rate({ won: false, series: "20", score: [520, 1500], clears: [1, 6], firstClear: [null, 70] })).toMatchObject({ mmr: -46, lp: -26 });
    expect(rate({ won: true, opponent: 200, series: "21", score: [1200, 1150], clears: [4, 4], firstClear: [150, 145] })).toMatchObject({ mmr: 53, lp: 30 });
    expect(rate({ won: true, opponent: -200, series: "21", score: [1100, 1080], clears: [3, 3], firstClear: [160, 165] })).toMatchObject({ mmr: 17, lp: 16 });
    expect(rate({ won: false, opponent: -200, series: "21", score: [1080, 1100], clears: [3, 3], firstClear: [165, 160] })).toMatchObject({ mmr: -53, lp: -30 });
    expect(rate({ won: true, series: "dm", score: [1180, 1170], clears: [3, 3], firstClear: [150, 150] })).toMatchObject({ mmr: 32, lp: 22 });
    expect(rate({ won: true, ...close, comeback: true, saved: true })).toMatchObject({ mmr: 36, lp: 27 });
  });

  it("MMR moves further than LP, and a one-sided match moves both players further than a close one", () => {
    const close = rate({ won: true, series: "21", score: [1260, 1140], clears: [4, 3], firstClear: [140, 150] });
    const sweep = rate({ won: true, series: "20", score: [1500, 520], clears: [6, 1], firstClear: [70, null] });
    expect(close.mmr).toBeGreaterThan(close.lp!);
    expect(sweep.mmr).toBeGreaterThan(close.mmr);
    expect(sweep.lp!).toBeGreaterThan(close.lp!);
    expect(Math.abs(sweep.mmrB)).toBeGreaterThan(Math.abs(close.mmrB));
  });

  it("a win always gains and a loss always loses, even against expectation and on worse content", () => {
    const heavyFavourite = calculatePerformanceMmrUpdate({ ratingA: 2_000, ratingB: 1_000, placementGamesA: 9, placementGamesB: 9,
      placementGames: 5, winner: "A", dominance: -1 });
    expect(heavyFavourite.deltaA).toBe(V2.minimumMmrChange);
    expect(heavyFavourite.deltaB).toBe(-V2.minimumMmrChange);
    const upset = rate({ won: true, series: "21", score: [900, 1300], clears: [2, 5], firstClear: [200, 100] });
    expect(upset.dominance).toBeLessThan(0);
    expect(upset.mmr).toBeGreaterThan(0);
    expect(upset.lp!).toBeGreaterThan(0);
  });

  it("reads each margin from the winner's side", () => {
    const d = matchDominance({ winner: "B", format: "BO3", roundWinsA: 0, roundWinsB: 2, scoreA: 250, scoreB: 750, clearsA: 1, clearsB: 3,
      rounds: [{ roundSeconds: 300, firstClearSecondsA: null, firstClearSecondsB: 60 }, { roundSeconds: 300, firstClearSecondsA: 150, firstClearSecondsB: 90 }] });
    expect(d.round).toBe(1);
    expect(d.score).toBeCloseTo(0.5);
    expect(d.clear).toBeCloseTo(0.5);
    expect(d.speed).toBeCloseTo(((0.8 - 0) + (0.7 - 0.5)) / 2);
    expect(d.total).toBeCloseTo(0.3 * 1 + 0.3 * 0.5 + 0.2 * 0.5 + 0.2 * 0.5);
    // 2:1 = 0.5, a death match after 1:1 = 0, a single map won = 1, a perfect sweep = total dominance.
    const base = { winner: "A" as const, scoreA: 0, scoreB: 0, clearsA: 0, clearsB: 0, rounds: [] };
    expect(matchDominance({ ...base, format: "BO3", roundWinsA: 2, roundWinsB: 1 }).round).toBe(0.5);
    expect(matchDominance({ ...base, format: "BO3", roundWinsA: 1, roundWinsB: 1 }).round).toBe(0);
    expect(matchDominance({ ...base, format: "SINGLE", roundWinsA: 1, roundWinsB: 0 }).round).toBe(1);
    expect(matchDominance({ ...base, format: "BO3", roundWinsA: 2, roundWinsB: 0, perfectSide: "A" }).total).toBe(1);
    expect(matchDominance({ ...base, winner: "DRAW", format: "BO3", roundWinsA: 1, roundWinsB: 1 }).total).toBe(0);
  });

  it("placement moves MMR faster and keeps LP hidden; streaks speed MMR up to +25%", () => {
    const placing = calculatePerformanceMmrUpdate({ ratingA: 1_000, ratingB: 1_000, placementGamesA: 2, placementGamesB: 9, placementGames: 5,
      winner: "A", dominance: 0 });
    expect(placing.deltaA).toBe(48);
    expect(placing.deltaB).toBe(-32);
    const streak = (n: number) => calculatePerformanceMmrUpdate({ ratingA: 1_000, ratingB: 1_000, placementGamesA: 9, placementGamesB: 9,
      placementGames: 5, winner: "A", dominance: 0, resultStreakA: n }).deltaA;
    expect(streak(0)).toBe(32);
    expect(streak(2)).toBe(Math.round(32 * 1.1));
    expect(streak(20)).toBe(40);
    expect(streak(-3)).toBe(32);
    const hidden = updateRankedRatingProfile(profile({ placementGames: 1, visibleRankedScore: 0, displayedTier: "UNRANKED" }), "A", "A", 1_532,
      policy, tierBandsFixture, { performance: { dominance: 1, bonus: 4, policy: V2 } });
    expect(hidden.visibleScoreDelta).toBeNull();
  });

  it("bonuses go to the winner only: +2 for a comeback, +2 for a round saved with a last-attempt Clear", () => {
    expect(visiblePerformanceBonus("A", "A", "B", ["A"])).toBe(4);
    expect(visiblePerformanceBonus("A", "A", "A", [])).toBe(0);
    expect(visiblePerformanceBonus("A", "A", "DRAW", ["B"])).toBe(0);
    expect(visiblePerformanceBonus("B", "A", "A", ["B"])).toBe(0);
  });

  it("LP stays within 0.5-1.6 of the base before bonuses; the old formula is unchanged without V2 input", () => {
    const at = (gap: number, won: boolean, dominance: number) => updateRankedRatingProfile(profile(), "A", won ? "A" : "B", 1_500 + gap,
      policy, tierBandsFixture, { opponentHiddenMmr: 3_000, performance: { dominance, bonus: 0, policy: V2 } }).visibleScoreDelta!;
    expect(at(600, true, 1)).toBe(Math.round(V2.visibleScoreBaseChange * V2.visibleScoreMaxFactor));
    expect(at(600, false, -1)).toBe(-Math.round(V2.visibleScoreBaseChange * V2.visibleScoreMinFactor));
    const legacy = updateRankedRatingProfile(profile(), "A", "A", 1_532, policy, tierBandsFixture, { opponentHiddenMmr: 1_500 });
    expect(legacy.visibleScoreDelta).toBe(Math.round(policy.visibleScoreBaseChange * (1 + 32 / policy.visibleScoreGapDivisor)));
  });
});
