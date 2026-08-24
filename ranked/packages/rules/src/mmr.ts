import { RankedDomainError } from "./errors.js";
import { tierForRating } from "./tier.js";
import type {
  CsmpTier,
  DisplayTier,
  MmrPolicy,
  PlayerSide,
  RoundResult,
  TierBand,
} from "./types.js";

export interface RankedProfileSeedState {
  readonly hiddenMmr: number | null;
  readonly placementGamesPlayed: number;
  readonly initialCsmpTier: CsmpTier | null;
  readonly initialSeedMmr: number | null;
  readonly seedAppliedAt: string | null;
}

export interface SeedProfileResult {
  readonly profile: RankedProfileSeedState;
  readonly applied: boolean;
}

export const seedProfileOnce = (
  profile: RankedProfileSeedState,
  currentCsmpTier: CsmpTier,
  seeds: Readonly<Record<CsmpTier, number>>,
  appliedAt: string,
): SeedProfileResult => {
  if (profile.seedAppliedAt !== null) return { profile: { ...profile }, applied: false };
  if (
    profile.hiddenMmr !== null ||
    profile.initialCsmpTier !== null ||
    profile.initialSeedMmr !== null
  ) {
    throw new RankedDomainError(
      "INVALID_CONFIG",
      "An unseeded profile cannot contain partial seed data",
      { profile },
    );
  }
  const normalizedTier = currentCsmpTier.trim().toUpperCase();
  if (!normalizedTier || normalizedTier.length > 80) {
    throw new RankedDomainError("INVALID_CONFIG", "CSMP tier must be a 1..80 character name");
  }
  const seed = seeds[normalizedTier];
  if (seed === undefined || !Number.isFinite(seed)) {
    throw new RankedDomainError("INVALID_CONFIG", `Missing seed MMR for ${normalizedTier}`);
  }
  if (!appliedAt.trim()) {
    throw new RankedDomainError("INVALID_CONFIG", "seedAppliedAt is required");
  }
  return {
    applied: true,
    profile: {
      ...profile,
      hiddenMmr: seed,
      initialCsmpTier: normalizedTier,
      initialSeedMmr: seed,
      seedAppliedAt: appliedAt,
    },
  };
};

export const displayedTierForProfile = (
  hiddenMmr: number,
  placementGamesPlayed: number,
  policy: MmrPolicy,
  tierBands: readonly TierBand[],
): DisplayTier =>
  placementGamesPlayed < policy.placementGames
    ? "UNRANKED"
    : tierForRating(hiddenMmr, tierBands);

const roundDelta = (value: number, policy: MmrPolicy): number => {
  if (policy.deltaRounding === "FLOOR") return Math.floor(value);
  if (policy.deltaRounding === "CEIL") return Math.ceil(value);
  return Math.round(value);
};

const clamp = (value: number, minimum: number, maximum: number): number =>
  Math.max(minimum, Math.min(maximum, value));

const rawExpectedScore = (rating: number, opponentRating: number, divisor: number): number =>
  1 / (1 + 10 ** ((opponentRating - rating) / divisor));

/**
 * Scales the distance from an even 50% expectation. Weight 0 treats every
 * opponent as equal, 1 preserves the existing Elo result, and 2 doubles the
 * configured opponent-strength effect while keeping the probability bounded.
 */
export const expectedScoreAgainst = (
  rating: number,
  opponentRating: number,
  divisor: number,
  weight: number,
): number => {
  const expected = rawExpectedScore(rating, opponentRating, divisor);
  return clamp(0.5 + (expected - 0.5) * weight, 0, 1);
};

export interface MmrUpdateInput {
  readonly ratingA: number;
  readonly ratingB: number;
  readonly placementGamesA: number;
  readonly placementGamesB: number;
  readonly winner: RoundResult;
  readonly scoreA?: number;
  readonly scoreB?: number;
  readonly clearsA?: number;
  readonly clearsB?: number;
  /** Positive is a win streak, negative is a loss streak, zero is neutral. */
  readonly resultStreakA?: number;
  readonly resultStreakB?: number;
  /** Winner that receives the theoretical maximum positive delta. */
  readonly maximumGainSide?: PlayerSide | null;
}

export interface MmrUpdateResult {
  readonly deltaA: number;
  readonly deltaB: number;
  readonly ratingAfterA: number;
  readonly ratingAfterB: number;
  readonly performanceFactor: number;
  readonly streakFactorA: number;
  readonly streakFactorB: number;
  readonly outcomeDisagreement: boolean;
  readonly maximumGainSide: PlayerSide | null;
}

const normalizedDifference = (left: number, right: number): number =>
  Math.min(1, Math.abs(left - right) / Math.max(1, Math.abs(left), Math.abs(right)));

const nextStreakFactor = (
  existing: number,
  won: boolean | null,
  policy: MmrPolicy,
): number => {
  if (won === null) return 1;
  const continuing = won ? existing > 0 : existing < 0;
  const nextCount = continuing ? Math.abs(existing) + 1 : 1;
  return 1 + Math.min(policy.streakMaxBonus, Math.max(0, nextCount - 1) * policy.streakStep);
};

export const calculateMmrUpdate = (
  input: MmrUpdateInput,
  policy: MmrPolicy,
): MmrUpdateResult => {
  if (policy.algorithm !== "ELO_V1") {
    throw new RankedDomainError("INVALID_CONFIG", "Unsupported MMR algorithm");
  }
  const maximumGainSide = input.maximumGainSide ?? null;
  if (maximumGainSide !== null && input.winner !== maximumGainSide) {
    throw new RankedDomainError(
      "INVALID_MATCH_TRANSITION",
      "Maximum MMR gain can only be assigned to the match winner",
    );
  }
  const kA =
    input.placementGamesA < policy.placementGames
      ? policy.placementKFactor
      : policy.regularKFactor;
  const kB =
    input.placementGamesB < policy.placementGames
      ? policy.placementKFactor
      : policy.regularKFactor;
  const actualA = input.winner === "DRAW" ? 0.5 : input.winner === "A" ? 1 : 0;
  const actualB = input.winner === "DRAW" ? 0.5 : 1 - actualA;
  const scoreA = Number(input.scoreA ?? 0);
  const scoreB = Number(input.scoreB ?? 0);
  const clearsA = Number(input.clearsA ?? 0);
  const clearsB = Number(input.clearsB ?? 0);
  const winnerScore = input.winner === "A" ? scoreA : scoreB;
  const loserScore = input.winner === "A" ? scoreB : scoreA;
  const winnerClears = input.winner === "A" ? clearsA : clearsB;
  const loserClears = input.winner === "A" ? clearsB : clearsA;
  const outcomeDisagreement = input.winner !== "DRAW" &&
    (winnerScore < loserScore || winnerClears < loserClears);
  const marginBonus = input.winner === "DRAW" ? 0 :
    normalizedDifference(scoreA, scoreB) * policy.scoreMarginMaxBonus +
    normalizedDifference(clearsA, clearsB) * policy.clearMarginMaxBonus;
  const marginFactor = 1 + marginBonus;
  // A match-series win that disagrees with either total score or total Clears
  // must never receive a decisive-win bonus. Reduce it below the ordinary
  // movement even when the other metric has a very large margin.
  const performanceFactor = outcomeDisagreement
    ? Math.min(1, marginFactor * policy.outcomeDisagreementFactor)
    : marginFactor;
  const streakFactorA = nextStreakFactor(
    input.resultStreakA ?? 0,
    input.winner === "DRAW" ? null : input.winner === "A",
    policy,
  );
  const streakFactorB = nextStreakFactor(
    input.resultStreakB ?? 0,
    input.winner === "DRAW" ? null : input.winner === "B",
    policy,
  );
  const expectedA = expectedScoreAgainst(
    input.ratingA,
    input.ratingB,
    policy.expectedScoreDivisor,
    policy.mmrOpponentStrengthWeight,
  );
  const expectedB = expectedScoreAgainst(
    input.ratingB,
    input.ratingA,
    policy.expectedScoreDivisor,
    policy.mmrOpponentStrengthWeight,
  );
  const maximumPerformanceFactor =
    1 + policy.scoreMarginMaxBonus + policy.clearMarginMaxBonus;
  const maximumStreakFactor = 1 + policy.streakMaxBonus;
  const normalDeltaA = roundDelta(
    kA * (actualA - expectedA) * performanceFactor * streakFactorA,
    policy,
  );
  const normalDeltaB = roundDelta(
    kB * (actualB - expectedB) * performanceFactor * streakFactorB,
    policy,
  );
  const deltaA = maximumGainSide === "A"
    ? roundDelta(kA * maximumPerformanceFactor * maximumStreakFactor, policy)
    : normalDeltaA;
  const deltaB = maximumGainSide === "B"
    ? roundDelta(kB * maximumPerformanceFactor * maximumStreakFactor, policy)
    : normalDeltaB;
  return {
    deltaA,
    deltaB,
    ratingAfterA: input.ratingA + deltaA,
    ratingAfterB: input.ratingB + deltaB,
    performanceFactor,
    streakFactorA,
    streakFactorB,
    outcomeDisagreement,
    maximumGainSide,
  };
};
