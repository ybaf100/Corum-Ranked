import { RankedDomainError } from "./errors.js";
import {
  CSMP_TIERS,
  RANKED_TIERS,
  type RankedOperationalConfig,
  type RankedRulesConfig,
  type TierBand,
} from "./types.js";

export const DOCUMENT_RULES_V0_3: RankedRulesConfig = Object.freeze({
  rulesVersion: "corum-ranked-v0.3",
  roundSeconds: 180,
  finalAttemptWindowSeconds: 10,
  lastAttemptWindowSeconds: 10,
  banSeconds: 10,
  bestOf: 3,
});

export interface ConfigValidationResult {
  readonly valid: boolean;
  readonly queueReady: boolean;
  readonly errors: readonly string[];
}

const validateDocumentRules = (rules: RankedRulesConfig, errors: string[]): void => {
  if (!rules.rulesVersion.trim()) errors.push("rules.rulesVersion is required");
  if (rules.roundSeconds !== 180) errors.push("rules.roundSeconds must be 180 for v0.3");
  if (rules.finalAttemptWindowSeconds !== 10) {
    errors.push("rules.finalAttemptWindowSeconds must be 10 for v0.3");
  }
  if (rules.lastAttemptWindowSeconds !== 10) {
    errors.push("rules.lastAttemptWindowSeconds must be 10 for v0.3");
  }
  if (rules.banSeconds !== 10) errors.push("rules.banSeconds must be 10 for v0.3");
  if (rules.bestOf !== 3) errors.push("rules.bestOf must be 3 for v0.3");
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
  return errors;
};

export const validateOperationalConfig = (
  config: RankedOperationalConfig,
): ConfigValidationResult => {
  const errors: string[] = [];
  if (!config.generation.trim()) errors.push("generation is required");
  validateDocumentRules(config.rules, errors);
  errors.push(...validateTierBands(config.tierBands));

  for (const tier of CSMP_TIERS) {
    const seed = config.csmpSeeds[tier];
    if (seed === undefined || !Number.isFinite(seed)) {
      errors.push(`csmpSeeds.${tier} must be configured`);
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
