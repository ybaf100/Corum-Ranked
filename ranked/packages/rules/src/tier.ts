import { RankedDomainError } from "./errors.js";
import { validateTierBands } from "./config.js";
import type { RankedTier, TierBand } from "./types.js";

export const tierForRating = (rating: number, bands: readonly TierBand[]): RankedTier => {
  if (!Number.isFinite(rating)) {
    throw new RankedDomainError("INVALID_CONFIG", "Rating must be finite", { rating });
  }
  const errors = validateTierBands(bands);
  if (errors.length > 0) {
    throw new RankedDomainError("INVALID_CONFIG", "Tier boundaries are invalid", { errors });
  }

  const band = bands.find(
    (candidate) =>
      rating >= candidate.minInclusive &&
      (candidate.maxExclusive === null || rating < candidate.maxExclusive),
  );
  if (!band) {
    throw new RankedDomainError("INVALID_CONFIG", "Rating is outside configured tier boundaries", {
      rating,
    });
  }
  return band.tier;
};

/**
 * Resolves an internal Hidden MMR to a Ranked tier. Hidden MMR is allowed to
 * fall below the visible LP floor, so values below the lowest configured tier
 * are projected onto that lowest tier instead of being treated as invalid
 * configuration. Values within the configured bands remain strict.
 */
export const tierForHiddenMmr = (
  rating: number,
  bands: readonly TierBand[],
): RankedTier => {
  if (!Number.isFinite(rating)) {
    throw new RankedDomainError("INVALID_CONFIG", "Hidden MMR must be finite", { rating });
  }
  const errors = validateTierBands(bands);
  if (errors.length > 0) {
    throw new RankedDomainError("INVALID_CONFIG", "Tier boundaries are invalid", { errors });
  }

  const lowestBand = [...bands].sort(
    (left, right) => left.minInclusive - right.minInclusive,
  )[0];
  if (!lowestBand) {
    throw new RankedDomainError("INVALID_CONFIG", "Tier boundaries are empty");
  }
  if (rating < lowestBand.minInclusive) return lowestBand.tier;
  return tierForRating(rating, bands);
};

export interface EffectiveTierResult {
  readonly averageRating: number;
  readonly tier: RankedTier;
}

export const effectiveTierForMatch = (
  ratingA: number,
  ratingB: number,
  bands: readonly TierBand[],
): EffectiveTierResult => {
  if (!Number.isFinite(ratingA) || !Number.isFinite(ratingB)) {
    throw new RankedDomainError("INVALID_CONFIG", "Both ratings must be finite", {
      ratingA,
      ratingB,
    });
  }
  const averageRating = (ratingA + ratingB) / 2;
  return { averageRating, tier: tierForHiddenMmr(averageRating, bands) };
};

export const tierBandFor = (tier: RankedTier, bands: readonly TierBand[]): TierBand => {
  const errors = validateTierBands(bands);
  if (errors.length > 0) {
    throw new RankedDomainError("INVALID_CONFIG", "Tier boundaries are invalid", { errors });
  }
  const band = bands.find((candidate) => candidate.tier === tier);
  if (!band) {
    throw new RankedDomainError("INVALID_CONFIG", `Missing tier band for ${tier}`);
  }
  return band;
};
