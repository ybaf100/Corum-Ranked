export const RANKED_TIERS = ["RED", "AQUA", "BRONZE", "SILVER", "GOLD"] as const;
export const DISPLAY_TIERS = ["UNRANKED", ...RANKED_TIERS] as const;
export const CSMP_TIERS = ["NONE", ...RANKED_TIERS] as const;
export const PLAYER_SIDES = ["A", "B"] as const;

export type RankedTier = (typeof RANKED_TIERS)[number];
export type DisplayTier = (typeof DISPLAY_TIERS)[number];
export type CsmpTier = (typeof CSMP_TIERS)[number];
export type PlayerSide = (typeof PLAYER_SIDES)[number];
export type PoolNumber = 1 | 2 | 3 | 4 | 5 | 6;
export type RoundResult = PlayerSide | "DRAW";

export interface RankedMap {
  /** @deprecated Use playableLevelId. Kept in the config contract during alpha migration. */
  readonly levelId: string;
  readonly canonicalLevelId: string;
  readonly alternateLevelId: string | null;
  readonly playableLevelId: string;
  readonly title: string;
  readonly creator: string;
  readonly difficulty: string;
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
}

export interface RankedRulesConfig {
  readonly rulesVersion: string;
  readonly roundSeconds: number;
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
  readonly deltaRounding: "NEAREST" | "FLOOR" | "CEIL";
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
  readonly csmpSeeds: Readonly<Partial<Record<CsmpTier, number>>>;
  readonly mmrPolicy?: MmrPolicy;
  readonly timeouts?: TimeoutPolicy;
  readonly matchmaking?: MatchmakingPolicy;
  readonly failurePolicy?: FailurePolicy;
  readonly cbf: CbfPolicy;
}

export const otherSide = (side: PlayerSide): PlayerSide => (side === "A" ? "B" : "A");
