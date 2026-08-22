import type {
  AllowedModRule,
  ClientEnvironmentPolicy,
  PoolNumber,
  RankedMap,
  RankedOperationalConfig,
  TierBand,
} from "../src/index.js";
import { DOCUMENT_RULES_V0_3 } from "../src/index.js";

export const makeMap = (
  pool: PoolNumber,
  index: number,
  overrides: Partial<RankedMap> = {},
): RankedMap => {
  const canonicalLevelId = `${pool}${String(index).padStart(3, "0")}`;
  const alternateLevelId = `9${canonicalLevelId}`;
  return {
    levelId: alternateLevelId,
    canonicalLevelId,
    alternateLevelId,
    playableLevelId: alternateLevelId,
    title: `Pool ${pool} Map ${index}`,
    creator: `Creator ${index}`,
    difficulty: `Difficulty ${pool}`,
    pool,
    qualifyingPercent: 50 + pool,
    active: true,
    ...overrides,
  };
};

export const allPoolMaps = (): RankedMap[] =>
  ([1, 2, 3, 4, 5, 6] as const).flatMap((pool) =>
    Array.from({ length: 6 }, (_, index) => makeMap(pool, index + 1)),
  );

export const tierBandsFixture: readonly TierBand[] = [
  { tier: "RED", minInclusive: 0, maxExclusive: 1_000, mainPool: 2, deathmatchPool: 2 },
  { tier: "AQUA", minInclusive: 1_000, maxExclusive: 2_000, mainPool: 3, deathmatchPool: 3 },
  {
    tier: "BRONZE",
    minInclusive: 2_000,
    maxExclusive: 3_000,
    mainPool: 4,
    deathmatchPool: 4,
  },
  {
    tier: "SILVER",
    minInclusive: 3_000,
    maxExclusive: 4_000,
    mainPool: 5,
    deathmatchPool: 5,
  },
  { tier: "GOLD", minInclusive: 4_000, maxExclusive: null, mainPool: 6, deathmatchPool: 6 },
];

export const completeOperationalConfig = (): RankedOperationalConfig => ({
  enabled: true,
  generation: "test-generation",
  rules: DOCUMENT_RULES_V0_3,
  tierBands: tierBandsFixture,
  csmpSeeds: {
    NONE: 500,
    RED: 700,
    AQUA: 1_500,
    BRONZE: 2_500,
    SILVER: 3_500,
    GOLD: 4_500,
  },
  mmrPolicy: {
    algorithm: "ELO_V1",
    placementGames: 5,
    placementKFactor: 64,
    regularKFactor: 32,
    expectedScoreDivisor: 400,
    deltaRounding: "NEAREST",
  },
  timeouts: {
    sessionSeconds: 3_600,
    readySeconds: 30,
    reconnectGraceSeconds: 20,
    queueHeartbeatSeconds: 15,
    matchHeartbeatSeconds: 10,
    orphanAttemptSeconds: 120,
    roundResultSeconds: 5,
  },
  matchmaking: {
    initialRatingRange: 100,
    widenPerSecond: 2,
    maximumRatingRange: 500,
  },
  failurePolicy: {
    readyTimeoutAction: "CANCEL_MATCH",
    reconnectTimeoutAction: "FORFEIT_DISCONNECTED",
    restartRecoveryAction: "RESUME",
  },
  cbf: {
    modId: "syzzi.click_between_frames",
    requiredSettings: {
      "soft-toggle": false,
      "click-on-steps": false,
      "physics-bypass": false,
    },
  },
});

export const cbfAllowRule: AllowedModRule = {
  id: "syzzi.click_between_frames",
  displayName: "Click Between Frames",
  required: true,
  enabled: true,
};

export const clientPolicyFixture = (): ClientEnvironmentPolicy => ({
  allowedMods: [
    cbfAllowRule,
    {
      id: "hwanhee1.corum_ranked",
      displayName: "Corum Ranked",
      required: true,
      enabled: true,
    },
  ],
  cbf: completeOperationalConfig().cbf,
});
