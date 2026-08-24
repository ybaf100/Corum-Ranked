import { RankedDomainError } from "./errors.js";
import { expectedScoreAgainst } from "./mmr.js";
import { tierForRating } from "./tier.js";
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
}

const clamp = (value: number, minimum: number, maximum: number): number =>
  Math.max(minimum, Math.min(maximum, value));

const sortedBands = (bands: readonly TierBand[]): readonly TierBand[] =>
  [...bands].sort((left, right) => left.minInclusive - right.minInclusive);

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
    const rawScore = Math.max(ordered[0]!.minInclusive, Math.round(hiddenMmrAfter));
    const placedScore = ordered.reduce((score, band, index) => {
      if (index === 0 || Math.abs(score - band.minInclusive) > 15) return score;
      // The forbidden interval is inclusive. cutoff - 16 is the nearest legal
      // score and always moves a near-cutoff placement downward, never upward.
      return Math.min(score, band.minInclusive - 16);
    }, rawScore);
    return {
      profile: {
        ...before,
        hiddenMmr: hiddenMmrAfter,
        visibleRankedScore: placedScore,
        placementGames,
        placementResults,
        displayedTier: tierForRating(placedScore, bands),
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
          displayedTier: promotion.targetTier,
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
      const source = ordered.find((band) => band.tier === promotion.sourceTier);
      if (!source) throw new RankedDomainError("INVALID_CONFIG", "Promotion source tier is missing");
      const score = Math.max(source.minInclusive, promotion.cutoff - policy.promotionFailurePenalty);
      return {
        profile: {
          ...before,
          hiddenMmr: hiddenMmrAfter,
          visibleRankedScore: score,
          placementGames,
          placementResults,
          displayedTier: promotion.sourceTier,
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
  const magnitude = Math.max(1, Math.round(
    policy.visibleScoreBaseChange * movementFactor,
  ));
  const projected = before.visibleRankedScore + (won ? magnitude : -magnitude);
  const currentIndex = ordered.findIndex((band) => band.tier === before.displayedTier);
  if (currentIndex < 0) {
    throw new RankedDomainError("INVALID_CONFIG", `Unknown displayed tier ${before.displayedTier}`);
  }
  const currentBand = ordered[currentIndex]!;
  const nextBand = ordered[currentIndex + 1];

  if (won && nextBand && projected >= nextBand.minInclusive) {
    const score = nextBand.minInclusive - 1;
    const promotion: PromotionSeriesState = {
      sourceTier: currentBand.tier,
      targetTier: nextBand.tier,
      cutoff: nextBand.minInclusive,
      gamesRequired: currentBand.promotionGames,
      winsRequired: currentBand.promotionWinsRequired,
      results: [],
    };
    return {
      profile: {
        ...before,
        hiddenMmr: hiddenMmrAfter,
        visibleRankedScore: score,
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
    if (currentIndex === 0 || hiddenMmrAfter >= currentBand.minInclusive) {
      return {
        profile: {
          ...before,
          hiddenMmr: hiddenMmrAfter,
          visibleRankedScore: currentBand.minInclusive,
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
      displayedTier: before.displayedTier,
      resultStreak,
      promotion: null,
      demotionWarning: won ? false : before.demotionWarning,
    },
    visibleScoreDelta: score - before.visibleRankedScore,
    placementCompleted: false,
    promotionStarted: false,
    promotionSucceeded: false,
    promotionFailed: false,
    demoted: false,
  };
};
