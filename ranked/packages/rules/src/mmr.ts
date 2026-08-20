import { RankedDomainError } from "./errors.js";
import { tierForRating } from "./tier.js";
import type {
  CsmpTier,
  DisplayTier,
  MmrPolicy,
  PlayerSide,
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
  seeds: Readonly<Partial<Record<CsmpTier, number>>>,
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
  const seed = seeds[currentCsmpTier];
  if (seed === undefined || !Number.isFinite(seed)) {
    throw new RankedDomainError("INVALID_CONFIG", `Missing seed MMR for ${currentCsmpTier}`);
  }
  if (!appliedAt.trim()) {
    throw new RankedDomainError("INVALID_CONFIG", "seedAppliedAt is required");
  }
  return {
    applied: true,
    profile: {
      ...profile,
      hiddenMmr: seed,
      initialCsmpTier: currentCsmpTier,
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

const expectedScore = (rating: number, opponentRating: number, divisor: number): number =>
  1 / (1 + 10 ** ((opponentRating - rating) / divisor));

export interface MmrUpdateInput {
  readonly ratingA: number;
  readonly ratingB: number;
  readonly placementGamesA: number;
  readonly placementGamesB: number;
  readonly winner: PlayerSide;
}

export interface MmrUpdateResult {
  readonly deltaA: number;
  readonly deltaB: number;
  readonly ratingAfterA: number;
  readonly ratingAfterB: number;
}

export const calculateMmrUpdate = (
  input: MmrUpdateInput,
  policy: MmrPolicy,
): MmrUpdateResult => {
  if (policy.algorithm !== "ELO_V1") {
    throw new RankedDomainError("INVALID_CONFIG", "Unsupported MMR algorithm");
  }
  const kA =
    input.placementGamesA < policy.placementGames
      ? policy.placementKFactor
      : policy.regularKFactor;
  const kB =
    input.placementGamesB < policy.placementGames
      ? policy.placementKFactor
      : policy.regularKFactor;
  const actualA = input.winner === "A" ? 1 : 0;
  const actualB = 1 - actualA;
  const deltaA = roundDelta(
    kA * (actualA - expectedScore(input.ratingA, input.ratingB, policy.expectedScoreDivisor)),
    policy,
  );
  const deltaB = roundDelta(
    kB * (actualB - expectedScore(input.ratingB, input.ratingA, policy.expectedScoreDivisor)),
    policy,
  );
  return {
    deltaA,
    deltaB,
    ratingAfterA: input.ratingA + deltaA,
    ratingAfterB: input.ratingB + deltaB,
  };
};
