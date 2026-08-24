import { RankedDomainError } from "./errors.js";
import {
  RANKED_TIERS,
  type RankedOperationalConfig,
  type RankedRulesConfig,
  type TierBand,
} from "./types.js";

export const DOCUMENT_RULES_V0_5: RankedRulesConfig = Object.freeze({
  rulesVersion: "corum-ranked-v0.5",
  roundSeconds: 300,
  clearTarget: 3,
  finalAttemptWindowSeconds: 10,
  lastAttemptWindowSeconds: 10,
  banSeconds: 10,
  bestOf: 3,
});

/** @deprecated Kept as a source-compatible alias for older tests/tools. */
export const DOCUMENT_RULES_V0_4 = DOCUMENT_RULES_V0_5;

export interface ConfigValidationResult {
  readonly valid: boolean;
  readonly queueReady: boolean;
  readonly errors: readonly string[];
}

const validateDocumentRules = (rules: RankedRulesConfig, errors: string[]): void => {
  if (!rules.rulesVersion.trim()) errors.push("rules.rulesVersion is required");
  if (rules.rulesVersion !== "corum-ranked-v0.5") {
    errors.push("rules.rulesVersion must be corum-ranked-v0.5");
  }
  if (rules.roundSeconds !== 300) errors.push("rules.roundSeconds must be 300 for v0.5");
  if (rules.clearTarget !== 3) errors.push("rules.clearTarget must be 3 for v0.5");
  if (rules.finalAttemptWindowSeconds !== 10) {
    errors.push("rules.finalAttemptWindowSeconds must be 10 for v0.5");
  }
  if (rules.lastAttemptWindowSeconds !== 10) {
    errors.push("rules.lastAttemptWindowSeconds must be 10 for v0.5");
  }
  if (rules.banSeconds !== 10) errors.push("rules.banSeconds must be 10 for v0.5");
  if (rules.bestOf !== 3) errors.push("rules.bestOf must be 3 for v0.5");
};

export const validateTierBands = (bands: readonly TierBand[]): readonly string[] => {
  const errors: string[] = [];
  const byTier = new Map(bands.map((band) => [band.tier, band]));

  for (const tier of RANKED_TIERS) {
    if (!byTier.has(tier)) errors.push(`tierBands is missing ${tier}`);
  }
  if (byTier.size !== RANKED_TIERS.length || bands.length !== RANKED_TIERS.length) {
    errors.push("tierBands must contain each ranked tier exactly once");
  }

  const sorted = [...bands].sort((left, right) => left.minInclusive - right.minInclusive);
  for (const [index, band] of sorted.entries()) {
    if (!Number.isFinite(band.minInclusive)) {
      errors.push(`${band.tier}.minInclusive must be finite`);
    }
    if (band.maxExclusive !== null && band.maxExclusive <= band.minInclusive) {
      errors.push(`${band.tier}.maxExclusive must be greater than minInclusive`);
    }
    if (index > 0) {
      const previous = sorted[index - 1];
      if (previous?.maxExclusive === null) {
        errors.push(`${previous.tier}.maxExclusive may be null only for the final tier`);
      } else if (previous && previous.maxExclusive !== band.minInclusive) {
        errors.push(`${previous.tier} and ${band.tier} boundaries must be contiguous`);
      }
    }
  }
  const finalBand = sorted.at(-1);
  if (finalBand && finalBand.maxExclusive !== null) {
    errors.push("the highest tier must have maxExclusive = null");
  }
  for (const [index, band] of sorted.entries()) {
    const highest = index === sorted.length - 1;
    if (!Number.isInteger(band.promotionGames) || band.promotionGames < 0) {
      errors.push(`${band.tier}.promotionGames must be a non-negative integer`);
    }
    if (!Number.isInteger(band.promotionWinsRequired) || band.promotionWinsRequired < 0) {
      errors.push(`${band.tier}.promotionWinsRequired must be a non-negative integer`);
    }
    if (highest) {
      if (band.promotionGames !== 0 || band.promotionWinsRequired !== 0) {
        errors.push(`${band.tier} is the highest tier and cannot configure a promotion series`);
      }
    } else if (
      band.promotionGames < 1 ||
      band.promotionWinsRequired < 1 ||
      band.promotionWinsRequired > band.promotionGames
    ) {
      errors.push(`${band.tier} promotion series must require 1..promotionGames wins`);
    }
  }
  return errors;
};

export const validateOperationalConfig = (
  config: RankedOperationalConfig,
): ConfigValidationResult => {
  const errors: string[] = [];
  if (!config.generation.trim()) errors.push("generation is required");
  validateDocumentRules(config.rules, errors);
  errors.push(...validateTierBands(config.tierBands));

  const normalizedSeedNames = new Set<string>();
  for (const [rawTier, seed] of Object.entries(config.csmpSeeds)) {
    const tier = rawTier.trim().toUpperCase();
    if (!tier || tier.length > 80 || tier !== rawTier) {
      errors.push(`csmpSeeds key '${rawTier}' must be a normalized 1..80 character tier name`);
      continue;
    }
    if (normalizedSeedNames.has(tier)) {
      errors.push(`csmpSeeds contains duplicate normalized tier: ${tier}`);
    }
    normalizedSeedNames.add(tier);
    if (!Number.isFinite(seed)) errors.push(`csmpSeeds.${tier} must be finite`);
  }
  if (!normalizedSeedNames.has("NONE")) {
    errors.push("csmpSeeds.NONE must be configured");
  }

  if (!Array.isArray(config.csmpTierNames) || config.csmpTierNames.length === 0) {
    errors.push("csmpTierNames must contain the live CSMP Tiers sheet names");
  } else {
    const configuredTierNames = new Set<string>();
    for (const rawTier of config.csmpTierNames) {
      if (typeof rawTier !== "string") {
        errors.push("csmpTierNames entries must be strings");
        continue;
      }
      const tier = rawTier.trim().toUpperCase();
      if (!tier || tier.length > 80 || tier !== rawTier || tier === "NONE") {
        errors.push(`csmpTierNames entry '${rawTier}' must be a normalized non-NONE 1..80 character name`);
        continue;
      }
      if (configuredTierNames.has(tier)) {
        errors.push(`csmpTierNames contains duplicate tier: ${tier}`);
        continue;
      }
      configuredTierNames.add(tier);
      if (!Number.isFinite(config.csmpSeeds[tier])) {
        errors.push(`csmpSeeds.${tier} must be configured for the live CSMP tier`);
      }
    }
  }

  if (!config.mmrPolicy) {
    errors.push("mmrPolicy must be configured");
  } else {
    const policy = config.mmrPolicy;
    if (!Number.isInteger(policy.placementGames) || policy.placementGames < 1) {
      errors.push("mmrPolicy.placementGames must be a positive integer");
    }
    if (!(policy.placementKFactor > 0)) errors.push("mmrPolicy.placementKFactor must be positive");
    if (!(policy.regularKFactor > 0)) errors.push("mmrPolicy.regularKFactor must be positive");
    if (!(policy.expectedScoreDivisor > 0)) {
      errors.push("mmrPolicy.expectedScoreDivisor must be positive");
    }
    if (!(policy.visibleScoreBaseChange > 0)) errors.push("mmrPolicy.visibleScoreBaseChange must be positive");
    if (!(policy.visibleScoreGapDivisor > 0)) errors.push("mmrPolicy.visibleScoreGapDivisor must be positive");
    if (!(policy.visibleScoreMinFactor > 0)) errors.push("mmrPolicy.visibleScoreMinFactor must be positive");
    if (policy.visibleScoreMaxFactor < policy.visibleScoreMinFactor) {
      errors.push("mmrPolicy.visibleScoreMaxFactor must cover visibleScoreMinFactor");
    }
    for (const key of ["scoreMarginMaxBonus", "clearMarginMaxBonus", "streakStep", "streakMaxBonus"] as const) {
      if (policy[key] < 0) errors.push(`mmrPolicy.${key} must not be negative`);
    }
    if (!(policy.outcomeDisagreementFactor > 0 && policy.outcomeDisagreementFactor <= 1)) {
      errors.push("mmrPolicy.outcomeDisagreementFactor must be in (0, 1]");
    }
    for (const key of ["promotionScoreBonus", "promotionFailurePenalty", "demotionScorePenalty"] as const) {
      if (!Number.isInteger(policy[key]) || policy[key] < 0) {
        errors.push(`mmrPolicy.${key} must be a non-negative integer`);
      }
    }
  }

  if (!config.timeouts) {
    errors.push("timeouts must be configured");
  } else {
    for (const [key, value] of Object.entries(config.timeouts)) {
      if (!Number.isFinite(value) || value <= 0) errors.push(`timeouts.${key} must be positive`);
    }
  }

  if (!config.matchmaking) {
    errors.push("matchmaking must be configured");
  } else {
    if (config.matchmaking.initialRatingRange < 0) {
      errors.push("matchmaking.initialRatingRange must not be negative");
    }
    if (config.matchmaking.widenPerSecond < 0) {
      errors.push("matchmaking.widenPerSecond must not be negative");
    }
    if (config.matchmaking.maximumRatingRange < config.matchmaking.initialRatingRange) {
      errors.push("matchmaking.maximumRatingRange must cover initialRatingRange");
    }
  }

  if (!config.failurePolicy) {
    errors.push("failurePolicy must be configured");
  }

  if (!config.cbf.modId.trim()) errors.push("cbf.modId is required");

  return {
    valid: errors.length === 0,
    queueReady: config.enabled && errors.length === 0,
    errors,
  };
};

export const assertOperationalConfig = (config: RankedOperationalConfig): void => {
  const result = validateOperationalConfig(config);
  if (!result.valid) {
    throw new RankedDomainError("INVALID_CONFIG", "Ranked operational config is invalid", {
      errors: result.errors,
    });
  }
};
