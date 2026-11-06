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
  readonly round4Qualifiers?: number;
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
  tiebreakAttempts: 6, tiebreakDecimals: 3, minimumLevelId: 40_000_000,
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
  if (![16,64].includes(value.matchPlayerCount) || value.minimumPlayers < 8 ||
      value.minimumPlayers > value.matchPlayerCount) errors.push("player counts require matchPlayerCount 16 or 64, with 8 <= minimumPlayers <= matchPlayerCount");
  if (value.matchmakingMaxWaitSeconds < 1 || value.roundSeconds < 1 || value.tiebreakAttempts < 1 ||
      value.tiebreakDecimals > 6 || value.minimumLevelId < 1) errors.push("wait, round, tiebreak and level settings are invalid");
  if (!Number.isFinite(value.scoreMultiplier) || value.scoreMultiplier <= 0) errors.push("scoreMultiplier must be positive and finite");
  if (!(value.matchPlayerCount > value.round1Qualifiers && value.round1Qualifiers > value.round2Qualifiers &&
      value.round2Qualifiers > value.round3Qualifiers && (value.matchPlayerCount===64 ? Number.isSafeInteger(value.round4Qualifiers) && value.round3Qualifiers > value.round4Qualifiers! && value.round4Qualifiers! > value.finalWinners : value.round3Qualifiers > value.finalWinners) && value.finalWinners > 0)) {
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
  if (!Array.isArray(value.placementLp) || value.placementLp.length !== 16 ||
      value.placementLp.some(lp => !Number.isSafeInteger(lp))) errors.push("placementLp must provide the 16 canonical integer rank awards");
  return errors;
}

export type SurvivalPlayerCount = 16 | 64;
/** One shared LP/achievement system; only the field and elimination schedule change. */
export function survivalConfigForPlayerCount(config: SurvivalConfig, count: SurvivalPlayerCount): SurvivalConfig {
  if(count!==16 && count!==64)throw new RankedDomainError("INVALID_CONFIG","Survival format must be 16 or 64 players");
  return {...config,matchPlayerCount:count,minimumPlayers:Math.min(config.minimumPlayers,count),
    round1Qualifiers:count===64?40:10,round2Qualifiers:count===64?26:6,
    round3Qualifiers:count===64?12:3,round4Qualifiers:count===64?5:0,finalWinners:1};
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

/** Survival promotion series (per source tier group). One qualifying finish
 * within the chances promotes; using every chance without one costs LP. */
export interface SurvivalPromotionRule { readonly top16: number; readonly top64: number; readonly chances: number; }
export const SURVIVAL_PROMOTION_RULES: Readonly<Partial<Record<RankedTier, SurvivalPromotionRule>>> = Object.freeze({
  RED: { top16: 10, top64: 40, chances: 3 },
  AQUA: { top16: 5, top64: 15, chances: 3 },
  BRONZE: { top16: 2, top64: 5, chances: 5 },
  SILVER: { top16: 1, top64: 1, chances: 5 },
});
export const SURVIVAL_PROMOTION_FAILURE_PENALTY = 50;
/** Fixed bonus above the promotion boundary on success (Solo parity). */
export const SURVIVAL_PROMOTION_SCORE_BONUS = 50;
export type SurvivalPromotionMark = "WIN" | "LOSS";
export interface SurvivalPromotionSeries {
  readonly sourceTier: string;
  readonly targetTier: string;
  /** Target tier minimum LP; the player is held at cutoff - 1 during the series. */
  readonly cutoff: number;
  readonly chances: number;
  readonly top16: number;
  readonly top64: number;
  readonly results: readonly SurvivalPromotionMark[];
  /** Official rank achieved in each series match, aligned with `results`. */
  readonly ranks?: readonly number[];
}
export function survivalPromotionRequiredRank(series: Pick<SurvivalPromotionSeries,"top16"|"top64">, playerCount: number): number {
  return playerCount === 64 ? series.top64 : series.top16;
}
/** Official placement of the settled match (best occupied rank) and its format. */
export interface SurvivalMatchOutcome { readonly rank: number; readonly playerCount: number; }
export function survivalPromotionPassed(series: Pick<SurvivalPromotionSeries,"top16"|"top64">, outcome: SurvivalMatchOutcome): boolean {
  return Number.isSafeInteger(outcome.rank) && outcome.rank >= 1 && outcome.rank <= survivalPromotionRequiredRank(series, outcome.playerCount);
}

export interface SurvivalRatingProfile {
  readonly lp: number;
  readonly tier: string;
  readonly demotionWarning: boolean;
  readonly promotion?: SurvivalPromotionSeries | null;
}
export interface SurvivalRatingUpdate {
  readonly profile: SurvivalRatingProfile;
  readonly actualDelta: number;
  readonly promoted: boolean;
  readonly demoted: boolean;
  readonly protected: boolean;
  readonly warningRecoveryLp: number | null;
  readonly promotionStarted?: boolean;
  readonly promotionSucceeded?: boolean;
  readonly promotionFailed?: boolean;
  /** The series as it stands after this match (this match's mark included),
   * also when it just ended; for the result screen. Absent outside a series. */
  readonly series?: SurvivalPromotionSeries;
  /** Marks the series already had before this match. */
  readonly seriesMarksBefore?: number;
}

/**
 * `outcome` (the settled official placement) enables the promotion series:
 * reaching the next tier's boundary holds LP at boundary - 1 and starts a
 * series; while it runs, LP is frozen and each settled match is one chance.
 * Without an outcome (legacy/admin callers) the tier changes immediately.
 */
export function updateSurvivalRating(before: SurvivalRatingProfile, delta: number, config: SurvivalConfig,
  outcome?: SurvivalMatchOutcome): SurvivalRatingUpdate {
  assertConfig(config);
  const index = config.tierBands.findIndex(band => band.tier === before.tier);
  if (index < 0 || !Number.isSafeInteger(before.lp) || before.lp < 0 || !Number.isSafeInteger(delta) ||
      !Number.isSafeInteger(before.lp + delta)) throw new RankedDomainError("INVALID_CONFIG", "Invalid Survival rating update");
  const floor = config.tierBands[index]!.minimumLp;
  const series = before.promotion ?? null;
  if (outcome && series) {
    const results = [...series.results, survivalPromotionPassed(series, outcome) ? "WIN" as const : "LOSS" as const];
    const ranks = [...(series.ranks ?? []), outcome.rank];
    if (results.at(-1) === "WIN") {
      const target = config.tierBands.find(band => band.tier === series.targetTier) ?? config.tierBands[Math.min(index + 1, config.tierBands.length - 1)]!;
      const lp = Math.max(series.cutoff, target.minimumLp) + SURVIVAL_PROMOTION_SCORE_BONUS;
      return { profile: { lp, tier: target.tier, demotionWarning: false, promotion: null }, actualDelta: lp - before.lp,
        promoted: true, demoted: false, protected: false, warningRecoveryLp: null, promotionSucceeded: true,
        series: { ...series, results, ranks }, seriesMarksBefore: series.results.length };
    }
    if (results.length >= series.chances) {
      const lp = Math.max(floor, series.cutoff - SURVIVAL_PROMOTION_FAILURE_PENALTY);
      return { profile: { lp, tier: before.tier, demotionWarning: false, promotion: null }, actualDelta: lp - before.lp,
        promoted: false, demoted: false, protected: false, warningRecoveryLp: null, promotionFailed: true,
        series: { ...series, results, ranks }, seriesMarksBefore: series.results.length };
    }
    return { profile: { lp: before.lp, tier: before.tier, demotionWarning: false, promotion: { ...series, results, ranks } }, actualDelta: 0,
      promoted: false, demoted: false, protected: false, warningRecoveryLp: null,
      series: { ...series, results, ranks }, seriesMarksBefore: series.results.length };
  }
  let lp = Math.max(0, before.lp + delta), tier = before.tier, warning = before.demotionWarning;
  let promoted = false, demoted = false, protectedLoss = false;
  const earned = survivalTierForLp(lp, config.tierBands);
  const earnedIndex = config.tierBands.indexOf(earned);
  const next = config.tierBands[index + 1];
  const rule = SURVIVAL_PROMOTION_RULES[config.tierBands[index]!.group];
  if (earnedIndex > index && outcome && next && rule) {
    // One tier at a time: hold just below the boundary and open the series.
    const cutoff = next.minimumLp;
    const held = cutoff - 1;
    const promotion: SurvivalPromotionSeries = { sourceTier: before.tier, targetTier: next.tier, cutoff,
      chances: rule.chances, top16: rule.top16, top64: rule.top64, results: [], ranks: [] };
    return { profile: { lp: held, tier: before.tier, demotionWarning: false, promotion }, actualDelta: held - before.lp,
      promoted: false, demoted: false, protected: false, warningRecoveryLp: null, promotionStarted: true,
      series: promotion, seriesMarksBefore: 0 };
  }
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
  const profile = outcome ? { lp, tier, demotionWarning: warning, promotion: null } : { lp, tier, demotionWarning: warning };
  return { profile, actualDelta: lp - before.lp, promoted, demoted, protected: protectedLoss,
    warningRecoveryLp: warning ? floor + config.demotionWarningRecoveryLp : null };
}
