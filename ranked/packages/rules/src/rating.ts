import { RankedDomainError } from "./errors.js";
import { expectedScoreAgainst } from "./mmr.js";
import type { VisiblePerformance } from "./performance-rating.js";
import { tierForRating } from "./tier.js";
import { baseRankedTier, soloTierOrder } from "./solo-tiers.js";
import type { DisplayTier, MmrPolicy, PlayerSide, RankedTier, RoundResult, TierBand } from "./types.js";

export type PromotionMark = "WIN" | "LOSS";

export interface PromotionSeriesState {
  readonly sourceTier: RankedTier;
  readonly targetTier: RankedTier;
  readonly cutoff: number;
  readonly gamesRequired: number;
  readonly winsRequired: number;
  readonly results: readonly PromotionMark[];
}

export interface RankedRatingProfile {
  readonly hiddenMmr: number;
  readonly visibleRankedScore: number;
  readonly placementGames: number;
  readonly placementResults: readonly PromotionMark[];
  readonly displayedTier: DisplayTier;
  /** Positive is consecutive wins; negative is consecutive losses. */
  readonly resultStreak: number;
  readonly promotion: PromotionSeriesState | null;
  readonly demotionWarning: boolean;
}

export interface RankedRatingUpdate {
  readonly profile: RankedRatingProfile;
  /** Null while placement is still in progress and LP remains hidden. */
  readonly visibleScoreDelta: number | null;
  readonly placementCompleted: boolean;
  readonly promotionStarted: boolean;
  readonly promotionSucceeded: boolean;
  readonly promotionFailed: boolean;
  readonly demoted: boolean;
}

export interface RankedRatingContext {
  /** Opponent Hidden MMR before this match. Omit only for legacy callers/tests. */
  readonly opponentHiddenMmr?: number;
  /** Forces the winner to the existing LP formula's maximum normal factor. */
  readonly maximumVisibleScoreGain?: boolean;
  /** Rating V2: the match's content and its LP bonus (performance-rating.ts). */
  readonly performance?: VisiblePerformance;
}

const clamp = (value: number, minimum: number, maximum: number): number =>
  Math.max(minimum, Math.min(maximum, value));

const sortedBands = (bands: readonly TierBand[]): readonly TierBand[] =>
  [...bands].sort((left, right) => left.minInclusive - right.minInclusive);

export interface FinalizedPlacementScore {
  readonly visibleRankedScore: number;
  readonly displayedTier: RankedTier;
}

/** Applies the inclusive tier-cutoff exclusion shared by every Placement flow. */
export const finalizePlacementScore = (
  hiddenMmr: number,
  bands: readonly TierBand[],
): FinalizedPlacementScore => {
  if (!Number.isFinite(hiddenMmr)) {
    throw new RankedDomainError("INVALID_CONFIG", "Placement Hidden MMR must be finite");
  }
  const ordered = sortedBands(bands);
  const rawScore = Math.max(ordered[0]!.minInclusive, Math.round(hiddenMmr));
  const visibleRankedScore = ordered.reduce((score, band, index) => {
    if (index === 0 || baseRankedTier(band.tier) === baseRankedTier(ordered[index - 1]!.tier) || Math.abs(score - band.minInclusive) > 15) return score;
    return Math.min(score, band.minInclusive - 16);
  }, rawScore);
  return { visibleRankedScore, displayedTier: tierForRating(visibleRankedScore, bands) };
};

const streakAfter = (existing: number, result: RoundResult, side: PlayerSide): number => {
  if (result === "DRAW") return existing;
  const won = result === side;
  if (won) return existing > 0 ? existing + 1 : 1;
  return existing < 0 ? existing - 1 : -1;
};

const promotionWins = (promotion: PromotionSeriesState): number =>
  promotion.results.filter((result) => result === "WIN").length;

const promotionLosses = (promotion: PromotionSeriesState): number =>
  promotion.results.filter((result) => result === "LOSS").length;

// V1 LP: the MMR catch-up and the opponent's strength only.
const legacyMagnitude = (
  before: RankedRatingProfile,
  won: boolean,
  hiddenMmrAfter: number,
  policy: MmrPolicy,
  context: RankedRatingContext,
): number => {
  const gapCorrection = clamp(
    1 + (hiddenMmrAfter - before.visibleRankedScore) / policy.visibleScoreGapDivisor,
    policy.visibleScoreMinFactor,
    policy.visibleScoreMaxFactor,
  );
  const gapFactor = won ? gapCorrection : 2 - gapCorrection;
  const opponentFactor = context.opponentHiddenMmr === undefined
    ? 1
    : (() => {
        const expected = expectedScoreAgainst(
          before.hiddenMmr,
          context.opponentHiddenMmr,
          policy.expectedScoreDivisor,
          policy.visibleScoreOpponentStrengthWeight,
        );
        return won ? 2 * (1 - expected) : 2 * expected;
      })();
  const movementFactor = context.maximumVisibleScoreGain && won
    ? policy.visibleScoreMaxFactor
    : clamp(
        gapFactor * opponentFactor,
        policy.visibleScoreMinFactor,
        policy.visibleScoreMaxFactor,
      );
  return Math.max(1, Math.round(policy.visibleScoreBaseChange * movementFactor));
};

// V2 LP: the match's content (the winner's dominance, the same for both
// players) times the MMR catch-up and half the opponent-strength weight,
// within 0.5-1.6 of the base; the winner adds its comeback / save bonus.
const performanceMagnitude = (
  before: RankedRatingProfile,
  won: boolean,
  hiddenMmrAfter: number,
  context: RankedRatingContext,
  performance: VisiblePerformance,
): number => {
  const policy = performance.policy;
  const gapCorrection = clamp(1 + (hiddenMmrAfter - before.visibleRankedScore) / policy.visibleScoreGapDivisor, 0.5, 1.5);
  const gapFactor = won ? gapCorrection : 2 - gapCorrection;
  const opponentFactor = context.opponentHiddenMmr === undefined
    ? 1
    : (() => {
        const expected = expectedScoreAgainst(
          before.hiddenMmr,
          context.opponentHiddenMmr,
          policy.expectedScoreDivisor,
          policy.visibleOpponentStrengthWeight,
        );
        return won ? 2 * (1 - expected) : 2 * expected;
      })();
  const dominance = Number.isFinite(performance.dominance) ? clamp(performance.dominance, -1, 1) : 0;
  const contentFactor = 1 + policy.visiblePerformanceWeight * dominance;
  const movementFactor = context.maximumVisibleScoreGain && won
    ? policy.visibleScoreMaxFactor
    : clamp(contentFactor * gapFactor * opponentFactor, policy.visibleScoreMinFactor, policy.visibleScoreMaxFactor);
  const bonus = won ? Math.max(0, Math.round(performance.bonus)) : 0;
  return Math.max(1, Math.round(policy.visibleScoreBaseChange * movementFactor)) + bonus;
};

export const updateRankedRatingProfile = (
  before: RankedRatingProfile,
  side: PlayerSide,
  result: RoundResult,
  hiddenMmrAfter: number,
  policy: MmrPolicy,
  bands: readonly TierBand[],
  context: RankedRatingContext = {},
): RankedRatingUpdate => {
  if (!Number.isFinite(hiddenMmrAfter)) {
    throw new RankedDomainError("INVALID_CONFIG", "hiddenMmrAfter must be finite");
  }
  const ordered = sortedBands(bands);
  const placementInProgress = before.placementGames < policy.placementGames;
  const placementGames = placementInProgress
    ? Math.min(policy.placementGames, before.placementGames + 1)
    : before.placementGames;
  const placementCompleted = before.placementGames < policy.placementGames &&
    placementGames >= policy.placementGames;
  const resultStreak = streakAfter(before.resultStreak, result, side);
  const placementResults = before.placementGames < policy.placementGames && result !== "DRAW"
    ? [...before.placementResults, result === side ? "WIN" as const : "LOSS" as const]
    : before.placementResults;

  if (placementGames < policy.placementGames) {
    return {
      profile: {
        ...before,
        hiddenMmr: hiddenMmrAfter,
        visibleRankedScore: 0,
        placementGames,
        placementResults,
        displayedTier: "UNRANKED",
        resultStreak,
        promotion: null,
        demotionWarning: false,
      },
      visibleScoreDelta: null,
      placementCompleted: false,
      promotionStarted: false,
      promotionSucceeded: false,
      promotionFailed: false,
      demoted: false,
    };
  }

  if (placementCompleted) {
    const finalized = finalizePlacementScore(hiddenMmrAfter, bands);
    const placedScore = finalized.visibleRankedScore;
    return {
      profile: {
        ...before,
        hiddenMmr: hiddenMmrAfter,
        visibleRankedScore: placedScore,
        placementGames,
        placementResults,
        displayedTier: finalized.displayedTier,
        resultStreak,
        promotion: null,
        demotionWarning: false,
      },
      visibleScoreDelta: placedScore,
      placementCompleted: true,
      promotionStarted: false,
      promotionSucceeded: false,
      promotionFailed: false,
      demoted: false,
    };
  }

  const won = result === side;
  const lost = result !== "DRAW" && !won;
  if (before.promotion) {
    if (result === "DRAW") {
      return {
        profile: { ...before, hiddenMmr: hiddenMmrAfter, placementGames, placementResults, resultStreak },
        visibleScoreDelta: 0,
        placementCompleted: false,
        promotionStarted: false,
        promotionSucceeded: false,
        promotionFailed: false,
        demoted: false,
      };
    }
    const promotion: PromotionSeriesState = {
      ...before.promotion,
      results: [...before.promotion.results, won ? "WIN" : "LOSS"],
    };
    const wins = promotionWins(promotion);
    const losses = promotionLosses(promotion);
    if (wins >= promotion.winsRequired) {
      const score = promotion.cutoff + policy.promotionScoreBonus;
      return {
        profile: {
          ...before,
          hiddenMmr: hiddenMmrAfter,
          visibleRankedScore: score,
          placementGames,
          placementResults,
          displayedTier: tierForRating(score, bands),
          resultStreak,
          promotion: null,
          demotionWarning: false,
        },
        visibleScoreDelta: score - before.visibleRankedScore,
        placementCompleted: false,
        promotionStarted: false,
        promotionSucceeded: true,
        promotionFailed: false,
        demoted: false,
      };
    }
    const impossible = losses > promotion.gamesRequired - promotion.winsRequired ||
      promotion.results.length >= promotion.gamesRequired;
    if (impossible) {
      const source = ordered.find((band) => baseRankedTier(band.tier) === baseRankedTier(promotion.sourceTier));
      if (!source) throw new RankedDomainError("INVALID_CONFIG", "Promotion source tier is missing");
      const score = Math.max(source.minInclusive, promotion.cutoff - policy.promotionFailurePenalty);
      return {
        profile: {
          ...before,
          hiddenMmr: hiddenMmrAfter,
          visibleRankedScore: score,
          placementGames,
          placementResults,
          displayedTier: tierForRating(score, bands),
          resultStreak,
          promotion: null,
          demotionWarning: false,
        },
        visibleScoreDelta: score - before.visibleRankedScore,
        placementCompleted: false,
        promotionStarted: false,
        promotionSucceeded: false,
        promotionFailed: true,
        demoted: false,
      };
    }
    return {
      profile: {
        ...before,
        hiddenMmr: hiddenMmrAfter,
        placementGames,
        placementResults,
        resultStreak,
        promotion,
        demotionWarning: false,
      },
      visibleScoreDelta: 0,
      placementCompleted: false,
      promotionStarted: false,
      promotionSucceeded: false,
      promotionFailed: false,
      demoted: false,
    };
  }

  if (result === "DRAW") {
    return {
      profile: { ...before, hiddenMmr: hiddenMmrAfter, placementGames, placementResults, resultStreak },
      visibleScoreDelta: 0,
      placementCompleted: false,
      promotionStarted: false,
      promotionSucceeded: false,
      promotionFailed: false,
      demoted: false,
    };
  }

  const magnitude = context.performance
    ? performanceMagnitude(before, won, hiddenMmrAfter, context, context.performance)
    : legacyMagnitude(before, won, hiddenMmrAfter, policy, context);
  const projected = before.visibleRankedScore + (won ? magnitude : -magnitude);
  const currentIndex = ordered.findIndex((band) => band.tier === before.displayedTier || band.tier === `${before.displayedTier}3`);
  if (currentIndex < 0) {
    throw new RankedDomainError("INVALID_CONFIG", `Unknown displayed tier ${before.displayedTier}`);
  }
  const currentGroup = ordered.filter(band => baseRankedTier(band.tier) === baseRankedTier(before.displayedTier));
  const currentBand = currentGroup[0]!;
  const currentTop = currentGroup.at(-1)!;
  const nextBand = ordered[ordered.indexOf(currentTop) + 1];

  if (won && nextBand && projected >= nextBand.minInclusive) {
    const score = nextBand.minInclusive - 1;
    const promotion: PromotionSeriesState = {
      sourceTier: currentTop.tier,
      targetTier: nextBand.tier,
      cutoff: nextBand.minInclusive,
      gamesRequired: currentTop.promotionGames,
      winsRequired: currentTop.promotionWinsRequired,
      results: [],
    };
    return {
      profile: {
        ...before,
        hiddenMmr: hiddenMmrAfter,
        visibleRankedScore: score,
        displayedTier: currentTop.tier,
        placementGames,
        placementResults,
        resultStreak,
        promotion,
        demotionWarning: false,
      },
      visibleScoreDelta: score - before.visibleRankedScore,
      placementCompleted: false,
      promotionStarted: true,
      promotionSucceeded: false,
      promotionFailed: false,
      demoted: false,
    };
  }

  if (lost && projected <= currentBand.minInclusive) {
    if (ordered.indexOf(currentBand) === 0 || hiddenMmrAfter >= currentBand.minInclusive) {
      return {
        profile: {
          ...before,
          hiddenMmr: hiddenMmrAfter,
          visibleRankedScore: currentBand.minInclusive,
          displayedTier: currentBand.tier,
          placementGames,
          placementResults,
          resultStreak,
          demotionWarning: false,
        },
        visibleScoreDelta: currentBand.minInclusive - before.visibleRankedScore,
        placementCompleted: false,
        promotionStarted: false,
        promotionSucceeded: false,
        promotionFailed: false,
        demoted: false,
      };
    }
    if (before.demotionWarning) {
      const score = Math.max(ordered[0]!.minInclusive, currentBand.minInclusive - policy.demotionScorePenalty);
      return {
        profile: {
          ...before,
          hiddenMmr: hiddenMmrAfter,
          visibleRankedScore: score,
          placementGames,
          placementResults,
          displayedTier: tierForRating(score, bands),
          resultStreak,
          promotion: null,
          demotionWarning: false,
        },
        visibleScoreDelta: score - before.visibleRankedScore,
        placementCompleted: false,
        promotionStarted: false,
        promotionSucceeded: false,
        promotionFailed: false,
        demoted: true,
      };
    }
    return {
      profile: {
        ...before,
        hiddenMmr: hiddenMmrAfter,
        visibleRankedScore: currentBand.minInclusive,
        displayedTier: currentBand.tier,
        placementGames,
        placementResults,
        resultStreak,
        demotionWarning: true,
      },
      visibleScoreDelta: currentBand.minInclusive - before.visibleRankedScore,
      placementCompleted: false,
      promotionStarted: false,
      promotionSucceeded: false,
      promotionFailed: false,
      demoted: false,
    };
  }

  const score = Math.max(ordered[0]!.minInclusive, projected);
  return {
    profile: {
      ...before,
      hiddenMmr: hiddenMmrAfter,
      visibleRankedScore: score,
      placementGames,
      placementResults,
      displayedTier: tierForRating(score, bands),
      resultStreak,
      promotion: null,
      demotionWarning: won ? false : before.demotionWarning,
    },
    visibleScoreDelta: score - before.visibleRankedScore,
    placementCompleted: false,
    promotionStarted: false,
    promotionSucceeded: false,
    promotionFailed: false,
    demoted: soloTierOrder(tierForRating(score, bands)) < soloTierOrder(before.displayedTier),
  };
};
