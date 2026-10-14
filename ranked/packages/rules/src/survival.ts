import { RankedDomainError } from "./errors.js";
import type { BaseRankedTier as RankedTier } from "./types.js";

export interface SurvivalTierBand {
  readonly tier: string;
  readonly group: RankedTier;
  readonly minimumLp: number;
}

/** Independent from Solo MMR, placement, promotion series and tier boundaries. */
export interface SurvivalConfig {
  readonly enabled: boolean;
  readonly initialLp: number;
  readonly placementEnabled: boolean;
  readonly promotionSeriesEnabled: boolean;
  readonly demotionProtectionEnabled: boolean;
  readonly matchPlayerCount: number;
  readonly minimumPlayers: number;
  readonly matchmakingMaxWaitSeconds: number;
  readonly tierMatchingEnabled: boolean;
  readonly roundSeconds: number;
  readonly round1Qualifiers: number;
  readonly round2Qualifiers: number;
  readonly round3Qualifiers: number;
  readonly finalWinners: number;
  readonly allowCutoffTies: boolean;
  readonly lastAttemptEnabled: boolean;
  readonly scoreMultiplier: number;
  readonly tiebreakAttempts: number;
  readonly tiebreakDecimals: number;
  readonly minimumLevelId: number;
  readonly demotionWarningRecoveryLp: number;
  readonly demotionTargetOffsetLp: number;
  readonly tierBands: readonly SurvivalTierBand[];
  readonly placementLp: readonly number[];
}

export const DEFAULT_SURVIVAL_CONFIG: SurvivalConfig = Object.freeze({
  enabled: false, initialLp: 150, placementEnabled: false,
  promotionSeriesEnabled: false, demotionProtectionEnabled: true,
  matchPlayerCount: 16, minimumPlayers: 8, matchmakingMaxWaitSeconds: 180,
  tierMatchingEnabled: false, roundSeconds: 180,
  round1Qualifiers: 10, round2Qualifiers: 6, round3Qualifiers: 3, finalWinners: 1,
  allowCutoffTies: true, lastAttemptEnabled: false, scoreMultiplier: 1,
  tiebreakAttempts: 3, tiebreakDecimals: 3, minimumLevelId: 40_000_000,
  demotionWarningRecoveryLp: 50, demotionTargetOffsetLp: 50,
  tierBands: Object.freeze([
    { tier: "RED", group: "RED", minimumLp: 0 },
    { tier: "AQUA", group: "AQUA", minimumLp: 300 },
    { tier: "BRONZE", group: "BRONZE", minimumLp: 600 },
    { tier: "SILVER", group: "SILVER", minimumLp: 900 },
    { tier: "GOLD", group: "GOLD", minimumLp: 1200 },
  ] as const),
  placementLp: Object.freeze([50, 40, 30, 25, 20, 15, 10, 8, 5, 2, -10, -20, -30, -40, -50, -55]),
});

export function validateSurvivalConfig(value: SurvivalConfig): readonly string[] {
  const errors: string[] = [];
  if (!value || typeof value !== "object") return ["Survival config must be an object"];
  for (const key of ["enabled", "placementEnabled", "promotionSeriesEnabled", "demotionProtectionEnabled",
    "tierMatchingEnabled", "allowCutoffTies", "lastAttemptEnabled"] as const) {
    if (typeof value[key] !== "boolean") errors.push(`${key} must be a boolean`);
  }
  for (const key of ["initialLp", "matchPlayerCount", "minimumPlayers", "matchmakingMaxWaitSeconds",
    "roundSeconds", "round1Qualifiers", "round2Qualifiers", "round3Qualifiers", "finalWinners",
    "tiebreakAttempts", "tiebreakDecimals", "minimumLevelId", "demotionWarningRecoveryLp", "demotionTargetOffsetLp"] as const) {
    if (!Number.isSafeInteger(value[key]) || value[key] < 0) errors.push(`${key} must be a non-negative safe integer`);
  }
  if (value.placementEnabled || value.promotionSeriesEnabled || value.lastAttemptEnabled) {
    errors.push("Survival has no placement, promotion series or LAST ATTEMPT in M1");
  }
  if (value.matchPlayerCount < 8 || value.matchPlayerCount > 16 || value.minimumPlayers < 8 ||
      value.minimumPlayers > value.matchPlayerCount) errors.push("player counts must satisfy 8 <= minimumPlayers <= matchPlayerCount <= 16");
  if (value.matchmakingMaxWaitSeconds < 1 || value.roundSeconds < 1 || value.tiebreakAttempts < 1 ||
      value.tiebreakDecimals > 6 || value.minimumLevelId < 1) errors.push("wait, round, tiebreak and level settings are invalid");
  if (!Number.isFinite(value.scoreMultiplier) || value.scoreMultiplier <= 0) errors.push("scoreMultiplier must be positive and finite");
  if (!(value.matchPlayerCount > value.round1Qualifiers && value.round1Qualifiers > value.round2Qualifiers &&
      value.round2Qualifiers > value.round3Qualifiers && value.round3Qualifiers > value.finalWinners && value.finalWinners > 0)) {
    errors.push("nominal round qualifiers must decrease strictly; short-field recalculation is deferred to M3/M5");
  }
  if (value.demotionTargetOffsetLp < 1) errors.push("demotionTargetOffsetLp must be positive");
  const bands = value.tierBands;
  if (!Array.isArray(bands) || bands.length < 1) errors.push("Survival tierBands are required");
  else {
    const ids = new Set<string>();
    for (const [index, band] of bands.entries()) {
      if (!band || !/^[A-Z][A-Z0-9_]{0,39}$/.test(band.tier) || ids.has(band.tier)) errors.push("tier IDs must be unique stable identifiers");
      if (!band) continue;
      ids.add(band.tier);
      if (!["RED", "AQUA", "BRONZE", "SILVER", "GOLD"].includes(band.group)) errors.push(`${band.tier} has an unknown group`);
      if (!Number.isSafeInteger(band.minimumLp) || band.minimumLp < 0 ||
          (index === 0 ? band.minimumLp !== 0 : !bands[index - 1] || band.minimumLp <= bands[index - 1]!.minimumLp)) {
        errors.push("Survival boundaries must start at zero and increase strictly");
      }
      if (index > 0 && bands[index - 1] && band.minimumLp - bands[index - 1]!.minimumLp < value.demotionTargetOffsetLp) {
        errors.push("demotion offset must leave the player inside the immediately lower tier");
      }
    }
  }
  if (!Array.isArray(value.placementLp) || value.placementLp.length !== value.matchPlayerCount ||
      value.placementLp.some(lp => !Number.isSafeInteger(lp))) errors.push("placementLp must provide an integer award for every nominal rank");
  return errors;
}

function assertConfig(config: SurvivalConfig): void {
  const errors = validateSurvivalConfig(config);
  if (errors.length) throw new RankedDomainError("INVALID_CONFIG", errors.join("; "));
}

export function survivalTierForLp(lp: number, bands: readonly SurvivalTierBand[]): SurvivalTierBand {
  const tier = [...bands].reverse().find(band => lp >= band.minimumLp);
  if (!Number.isSafeInteger(lp) || !tier) throw new RankedDomainError("INVALID_CONFIG", "Invalid Survival LP or boundaries");
  return tier;
}

/** Tied players occupy adjacent places; every player receives their lowest award. */
export function survivalPlacementLp(firstRank: number, tiedPlayers: number, awards: readonly number[]): number {
  if (!Number.isSafeInteger(firstRank) || !Number.isSafeInteger(tiedPlayers) || firstRank < 1 || tiedPlayers < 1 ||
      firstRank + tiedPlayers - 1 > awards.length || awards.some(lp => !Number.isSafeInteger(lp))) {
    throw new RankedDomainError("INVALID_CONFIG", "Invalid occupied Survival ranks");
  }
  return Math.min(...awards.slice(firstRank - 1, firstRank + tiedPlayers - 1));
}

export interface SurvivalRatingProfile {
  readonly lp: number;
  readonly tier: string;
  readonly demotionWarning: boolean;
}
export interface SurvivalRatingUpdate {
  readonly profile: SurvivalRatingProfile;
  readonly actualDelta: number;
  readonly promoted: boolean;
  readonly demoted: boolean;
  readonly protected: boolean;
  readonly warningRecoveryLp: number | null;
}

export function updateSurvivalRating(before: SurvivalRatingProfile, delta: number, config: SurvivalConfig): SurvivalRatingUpdate {
  assertConfig(config);
  const index = config.tierBands.findIndex(band => band.tier === before.tier);
  if (index < 0 || !Number.isSafeInteger(before.lp) || before.lp < 0 || !Number.isSafeInteger(delta) ||
      !Number.isSafeInteger(before.lp + delta)) throw new RankedDomainError("INVALID_CONFIG", "Invalid Survival rating update");
  const floor = config.tierBands[index]!.minimumLp;
  let lp = Math.max(0, before.lp + delta), tier = before.tier, warning = before.demotionWarning;
  let promoted = false, demoted = false, protectedLoss = false;
  const earned = survivalTierForLp(lp, config.tierBands);
  const earnedIndex = config.tierBands.indexOf(earned);
  if (earnedIndex > index) {
    tier = earned.tier; warning = false; promoted = true;
  } else if (index > 0 && lp < floor) {
    if (config.demotionProtectionEnabled && !warning) {
      lp = floor; warning = true; protectedLoss = true;
    } else {
      tier = config.tierBands[index - 1]!.tier;
      // Confirmed: SILVER 900 -> BRONZE 850, not lower-boundary 600 - 50.
      lp = Math.max(0, floor - config.demotionTargetOffsetLp); warning = false; demoted = true;
    }
  } else if (!config.demotionProtectionEnabled || index === 0 || lp >= floor + config.demotionWarningRecoveryLp) warning = false;
  const profile = { lp, tier, demotionWarning: warning };
  return { profile, actualDelta: lp - before.lp, promoted, demoted, protected: protectedLoss,
    warningRecoveryLp: warning ? floor + config.demotionWarningRecoveryLp : null };
}
