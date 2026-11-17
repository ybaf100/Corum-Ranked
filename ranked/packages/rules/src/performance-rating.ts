import { RankedDomainError } from "./errors.js";
import { expectedScoreAgainst } from "./mmr.js";
import type { MatchFormat } from "./match.js";
import type { PlayerSide, RoundResult } from "./types.js";

/**
 * Solo Ranked rating V2 (season 1): the match's content moves both ratings.
 *
 * The winner's dominance D (-1..1) sums four margins seen from the winner:
 * rounds (2:0 / 2:1 / death match), total score (with the death match's best
 * percentage), Clears, and how early each round's first Clear came. The same
 * D applies to both players, so a one-sided match moves both a lot and a
 * close one moves both a little.
 *
 * Hidden MMR moves more than LP: MMR = K (64; 96 in placement) x (result -
 * expected) x (1 + 0.6 D) x streak; LP = 20 x (1 + 0.25 D) x MMR catch-up x
 * opponent strength (half of the Elo weight), within 0.5-1.6, plus +2 each
 * for a comeback win and for a round saved with a last-attempt Clear.
 */
export interface PerformanceRatingPolicy {
  readonly placementKFactor: number;
  readonly regularKFactor: number;
  readonly expectedScoreDivisor: number;
  readonly mmrPerformanceWeight: number;
  readonly minimumMmrChange: number;
  readonly streakStep: number;
  readonly streakMaxBonus: number;
  readonly visibleScoreBaseChange: number;
  readonly visiblePerformanceWeight: number;
  readonly visibleOpponentStrengthWeight: number;
  readonly visibleScoreGapDivisor: number;
  readonly visibleScoreMinFactor: number;
  readonly visibleScoreMaxFactor: number;
  readonly roundWeight: number;
  readonly scoreWeight: number;
  readonly clearWeight: number;
  readonly speedWeight: number;
  readonly comebackBonus: number;
  readonly clutchBonus: number;
}

export const DEFAULT_PERFORMANCE_RATING_POLICY: PerformanceRatingPolicy = Object.freeze({
  placementKFactor: 96,
  regularKFactor: 64,
  expectedScoreDivisor: 400,
  mmrPerformanceWeight: 0.6,
  minimumMmrChange: 3,
  streakStep: 0.05,
  streakMaxBonus: 0.25,
  visibleScoreBaseChange: 20,
  visiblePerformanceWeight: 0.25,
  visibleOpponentStrengthWeight: 0.5,
  visibleScoreGapDivisor: 400,
  visibleScoreMinFactor: 0.5,
  visibleScoreMaxFactor: 1.6,
  roundWeight: 0.3,
  scoreWeight: 0.3,
  clearWeight: 0.2,
  speedWeight: 0.2,
  comebackBonus: 2,
  clutchBonus: 2,
});

export interface RoundPace {
  /** Length of the round's normal time in seconds. */
  readonly roundSeconds: number;
  /** Seconds from the round start to each side's first valid Clear; null without one. */
  readonly firstClearSecondsA: number | null;
  readonly firstClearSecondsB: number | null;
}

export interface MatchPerformanceInput {
  readonly winner: RoundResult;
  readonly format: MatchFormat;
  readonly roundWinsA: number;
  readonly roundWinsB: number;
  /** Totals over the rounds plus the death match (best percentage / Clears). */
  readonly scoreA: number;
  readonly scoreB: number;
  readonly clearsA: number;
  readonly clearsB: number;
  readonly rounds: readonly RoundPace[];
  /** A perfect three-Clear sweep counts as total dominance. */
  readonly perfectSide?: PlayerSide | null;
}

export interface MatchDominance {
  /** Each margin from the winner's side, -1..1. */
  readonly round: number;
  readonly score: number;
  readonly clear: number;
  readonly speed: number;
  /** The weighted sum, -1..1 (0 for a draw). */
  readonly total: number;
}

const clamp = (value: number, minimum: number, maximum: number): number =>
  Math.max(minimum, Math.min(maximum, value));
const finiteOr = (value: number, fallback: number): number => (Number.isFinite(value) ? value : fallback);
const margin = (own: number, other: number): number => {
  const a = Math.max(0, finiteOr(own, 0)), b = Math.max(0, finiteOr(other, 0));
  return a + b > 0 ? (a - b) / (a + b) : 0;
};
// 1 for a Clear at the round start, 0 at its end or without one.
const pace = (seconds: number | null, roundSeconds: number): number =>
  seconds === null || !Number.isFinite(seconds) || roundSeconds <= 0 ? 0 : clamp(1 - seconds / roundSeconds, 0, 1);

export const matchDominance = (
  input: MatchPerformanceInput,
  policy: PerformanceRatingPolicy = DEFAULT_PERFORMANCE_RATING_POLICY,
): MatchDominance => {
  if (input.winner === "DRAW") return { round: 0, score: 0, clear: 0, speed: 0, total: 0 };
  const winnerA = input.winner === "A";
  const winsW = winnerA ? input.roundWinsA : input.roundWinsB;
  const winsL = winnerA ? input.roundWinsB : input.roundWinsA;
  // 2:0 = 1, 2:1 = 0.5, a death match after 1:1 = 0; a single map won = 1.
  const toTake = input.format === "SINGLE" ? 1 : 2;
  const round = clamp((winsW - winsL) / toTake, -1, 1);
  const score = winnerA ? margin(input.scoreA, input.scoreB) : margin(input.scoreB, input.scoreA);
  const clear = winnerA ? margin(input.clearsA, input.clearsB) : margin(input.clearsB, input.clearsA);
  const paces = input.rounds.map((r) => {
    const a = pace(r.firstClearSecondsA, r.roundSeconds), b = pace(r.firstClearSecondsB, r.roundSeconds);
    return winnerA ? a - b : b - a;
  });
  const speed = paces.length ? clamp(paces.reduce((sum, p) => sum + p, 0) / paces.length, -1, 1) : 0;
  if (input.perfectSide && input.perfectSide === input.winner) return { round, score, clear, speed, total: 1 };
  const total = clamp(
    policy.roundWeight * round + policy.scoreWeight * score + policy.clearWeight * clear + policy.speedWeight * speed,
    -1,
    1,
  );
  return { round, score, clear, speed, total };
};

export interface PerformanceMmrInput {
  readonly ratingA: number;
  readonly ratingB: number;
  readonly placementGamesA: number;
  readonly placementGamesB: number;
  /** Games a placement takes (the Solo config's placementGames). */
  readonly placementGames: number;
  readonly winner: RoundResult;
  readonly dominance: number;
  /** Positive is a win streak, negative is a loss streak (before this match). */
  readonly resultStreakA?: number;
  readonly resultStreakB?: number;
}

export interface PerformanceMmrResult {
  readonly deltaA: number;
  readonly deltaB: number;
  readonly ratingAfterA: number;
  readonly ratingAfterB: number;
  readonly expectedA: number;
  readonly performanceFactor: number;
  readonly streakFactorA: number;
  readonly streakFactorB: number;
}

const streakFactor = (existing: number, won: boolean | null, policy: PerformanceRatingPolicy): number => {
  if (won === null) return 1;
  const continuing = won ? existing > 0 : existing < 0;
  const next = continuing ? Math.abs(existing) + 1 : 1;
  return 1 + Math.min(policy.streakMaxBonus, Math.max(0, next - 1) * policy.streakStep);
};

export const calculatePerformanceMmrUpdate = (
  input: PerformanceMmrInput,
  policy: PerformanceRatingPolicy = DEFAULT_PERFORMANCE_RATING_POLICY,
): PerformanceMmrResult => {
  if (!Number.isFinite(input.ratingA) || !Number.isFinite(input.ratingB)) {
    throw new RankedDomainError("INVALID_CONFIG", "Match ratings must be finite");
  }
  const dominance = clamp(finiteOr(input.dominance, 0), -1, 1);
  const kA = input.placementGamesA < input.placementGames ? policy.placementKFactor : policy.regularKFactor;
  const kB = input.placementGamesB < input.placementGames ? policy.placementKFactor : policy.regularKFactor;
  const expectedA = expectedScoreAgainst(input.ratingA, input.ratingB, policy.expectedScoreDivisor, 1);
  const actualA = input.winner === "DRAW" ? 0.5 : input.winner === "A" ? 1 : 0;
  const performanceFactor = input.winner === "DRAW" ? 1 : 1 + policy.mmrPerformanceWeight * dominance;
  const streakFactorA = streakFactor(input.resultStreakA ?? 0, input.winner === "DRAW" ? null : input.winner === "A", policy);
  const streakFactorB = streakFactor(input.resultStreakB ?? 0, input.winner === "DRAW" ? null : input.winner === "B", policy);
  // The winner always gains and the loser always loses at least the minimum.
  const settle = (raw: number, side: PlayerSide): number => {
    const rounded = Math.round(raw);
    if (input.winner === "DRAW") return rounded;
    return input.winner === side ? Math.max(policy.minimumMmrChange, rounded) : Math.min(-policy.minimumMmrChange, rounded);
  };
  const deltaA = settle(kA * (actualA - expectedA) * performanceFactor * streakFactorA, "A");
  const deltaB = settle(kB * ((1 - actualA) - (1 - expectedA)) * performanceFactor * streakFactorB, "B");
  return {
    deltaA,
    deltaB,
    ratingAfterA: input.ratingA + deltaA,
    ratingAfterB: input.ratingB + deltaB,
    expectedA,
    performanceFactor,
    streakFactorA,
    streakFactorB,
  };
};

/** What the LP update needs from the match, for one player. */
export interface VisiblePerformance {
  /** The winner's dominance D (the same number for both players). */
  readonly dominance: number;
  /** Flat LP added on a win (comeback / last-attempt save). */
  readonly bonus: number;
  readonly policy: PerformanceRatingPolicy;
}

/** LP bonus for `side`: a comeback win and a round it saved with a last-attempt Clear. */
export const visiblePerformanceBonus = (
  side: PlayerSide,
  winner: RoundResult,
  firstRoundWinner: RoundResult | null,
  savedRoundSides: readonly PlayerSide[],
  policy: PerformanceRatingPolicy = DEFAULT_PERFORMANCE_RATING_POLICY,
): number => {
  if (winner !== side) return 0;
  const other: PlayerSide = side === "A" ? "B" : "A";
  const comeback = firstRoundWinner === other ? policy.comebackBonus : 0;
  const clutch = savedRoundSides.includes(side) ? policy.clutchBonus : 0;
  return comeback + clutch;
};
