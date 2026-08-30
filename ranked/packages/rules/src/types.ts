export const RANKED_TIERS = ["RED", "AQUA", "BRONZE", "SILVER", "GOLD"] as const;
export const DISPLAY_TIERS = ["UNRANKED", ...RANKED_TIERS] as const;
export const PLAYER_SIDES = ["A", "B"] as const;

export type RankedTier = (typeof RANKED_TIERS)[number];
export type DisplayTier = (typeof DISPLAY_TIERS)[number];
// CSMP progression tiers are operator-defined in the CSMP Tiers and Ranked
// CSMP Seed sheets. They deliberately do not share the fixed Ranked display
// tier union.
export type CsmpTier = string;
export type PlayerSide = (typeof PLAYER_SIDES)[number];
export type PoolNumber = 1 | 2 | 3 | 4 | 5 | 6;
export type RoundResult = PlayerSide | "DRAW";

export const PLACEMENT_GRADES = ["S", "A", "B", "C", "D"] as const;
export type PlacementGrade = (typeof PLACEMENT_GRADES)[number];

export interface PlacementGradeThresholds {
  readonly sMin: number;
  readonly aMin: number;
  readonly bMin: number;
  readonly cMin: number;
}

/**
 * Server-authoritative solo Placement tuning. Values are snapshot with each
 * run so an operator config refresh cannot change a run already in progress.
 */
export interface PlacementPolicy {
  readonly stageCount: 5;
  readonly mapsPerStage: 2;
  /** Placement-only play clock. PvP keeps RankedRulesConfig.roundSeconds. */
  readonly roundSeconds: 180;
  /** Placement-only last-attempt start window. */
  readonly finalAttemptWindowSeconds: 10;
  /** Authoritative pre-map lobby countdown. */
  readonly prepareSeconds: 10;
  /** Reveal the real GD LevelInfo page at this remaining countdown value. */
  readonly levelRevealRemainingSeconds: 5;
  readonly gradeThresholds: PlacementGradeThresholds;
  readonly transitionGuardSeconds: number;
  readonly topAttemptCount: 2;
  readonly clearTarget: 2;
  readonly poolRatingAnchors: readonly [number, number, number, number, number, number];
  readonly mapScoreCenter: number;
  readonly ratingPerMapScorePoint: number;
  readonly mapRatingWeight: number;
  readonly stageRatingWeight: number;
  readonly overallRatingWeight: number;
}

export interface RankedMap {
  /** @deprecated Use playableLevelId. Kept in the config contract during alpha migration. */
  readonly levelId: string;
  readonly canonicalLevelId: string;
  readonly alternateLevelId: string | null;
  readonly playableLevelId: string;
  readonly title: string;
  readonly creator: string;
  readonly difficulty: string;
  /** Optional source-map length metadata used by server-side simulation/debug tooling. */
  readonly length?: string;
  readonly pool: PoolNumber;
  readonly qualifyingPercent: number;
  readonly active: boolean;
}

export interface RankedMapSnapshot {
  /** @deprecated Use playableLevelId. */
  readonly levelId: string;
  readonly canonicalLevelId: string;
  readonly alternateLevelId: string | null;
  readonly playableLevelId: string;
  readonly title: string;
  readonly creator: string;
  readonly difficulty: string;
  readonly pool: PoolNumber;
  readonly qualifyingPercent: number;
}

export interface TierBand {
  readonly tier: RankedTier;
  readonly minInclusive: number;
  readonly maxExclusive: number | null;
  readonly mainPool: PoolNumber;
  readonly deathmatchPool: PoolNumber;
  /** Promotion series played while moving from this tier to the next tier. */
  readonly promotionGames: number;
  readonly promotionWinsRequired: number;
}

export interface RankedRulesConfig {
  readonly rulesVersion: string;
  readonly roundSeconds: number;
  readonly clearTarget: number;
  readonly finalAttemptWindowSeconds: number;
  readonly lastAttemptWindowSeconds: number;
  readonly banSeconds: number;
  readonly bestOf: 3;
}

export interface MmrPolicy {
  readonly algorithm: "ELO_V1";
  readonly placementGames: number;
  readonly placementKFactor: number;
  readonly regularKFactor: number;
  readonly expectedScoreDivisor: number;
  /** 0 ignores opponent strength, 1 preserves Elo V1, 2 doubles its sensitivity. */
  readonly mmrOpponentStrengthWeight: number;
  readonly deltaRounding: "NEAREST" | "FLOOR" | "CEIL";
  readonly visibleScoreBaseChange: number;
  readonly visibleScoreGapDivisor: number;
  readonly visibleScoreMinFactor: number;
  readonly visibleScoreMaxFactor: number;
  /** 0 ignores opponent strength for LP, 1 mirrors Elo V1, 2 doubles its sensitivity. */
  readonly visibleScoreOpponentStrengthWeight: number;
  readonly scoreMarginMaxBonus: number;
  readonly clearMarginMaxBonus: number;
  readonly outcomeDisagreementFactor: number;
  readonly streakStep: number;
  readonly streakMaxBonus: number;
  readonly promotionScoreBonus: number;
  readonly promotionFailurePenalty: number;
  readonly demotionScorePenalty: number;
}

export interface TimeoutPolicy {
  readonly sessionSeconds: number;
  readonly readySeconds: number;
  readonly reconnectGraceSeconds: number;
  readonly queueHeartbeatSeconds: number;
  readonly matchHeartbeatSeconds: number;
  readonly orphanAttemptSeconds: number;
  readonly roundResultSeconds: number;
}

export interface MatchmakingPolicy {
  readonly initialRatingRange: number;
  readonly widenPerSecond: number;
  readonly maximumRatingRange: number;
}

export interface FailurePolicy {
  readonly readyTimeoutAction: "CANCEL_MATCH" | "FORFEIT_UNREADY";
  readonly reconnectTimeoutAction: "CANCEL_MATCH" | "FORFEIT_DISCONNECTED";
  readonly restartRecoveryAction: "CANCEL_MATCH" | "RESUME";
}

export interface CbfPolicy {
  readonly modId: string;
  readonly requiredSettings: Readonly<Record<string, boolean | number | string>>;
}

export interface RankedOperationalConfig {
  readonly enabled: boolean;
  readonly generation: string;
  readonly rules: RankedRulesConfig;
  readonly tierBands: readonly TierBand[];
  /** Normalized live names from the CSMP Tiers sheet; NONE is reserved for the baseline seed. */
  readonly csmpTierNames: readonly CsmpTier[];
  readonly csmpSeeds: Readonly<Record<CsmpTier, number>>;
  readonly mmrPolicy?: MmrPolicy;
  readonly placementPolicy?: PlacementPolicy;
  readonly timeouts?: TimeoutPolicy;
  readonly matchmaking?: MatchmakingPolicy;
  readonly failurePolicy?: FailurePolicy;
  readonly cbf: CbfPolicy;
}

export const otherSide = (side: PlayerSide): PlayerSide => (side === "A" ? "B" : "A");
