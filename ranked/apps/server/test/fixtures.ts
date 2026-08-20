import {
  DOCUMENT_RULES_V0_3,
  type AllowedModRule,
  type PoolNumber,
  type RankedMap,
  type RankedOperationalConfig,
  type TierBand,
} from "@corum-ranked/rules";
import type { RankedConfigDocument } from "../src/config/ranked-config.document.js";
import type { ServerEnvironment } from "../src/config/server-environment.js";

const tierBands: readonly TierBand[] = [
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

const operational = (generation: string): RankedOperationalConfig => ({
  enabled: true,
  generation,
  rules: DOCUMENT_RULES_V0_3,
  tierBands,
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

const maps = (): RankedMap[] =>
  ([1, 2, 3, 4, 5, 6] as const).flatMap((pool: PoolNumber) =>
    Array.from({ length: 5 }, (_, index) => ({
      canonicalLevelId: `${pool}${String(index + 1).padStart(3, "0")}`,
      alternateLevelId: `9${pool}${String(index + 1).padStart(3, "0")}`,
      title: `Map ${pool}-${index + 1}`,
      creator: "Test Creator",
      difficulty: "Test Difficulty",
      pool,
      qualifyingPercent: 50,
      active: true,
    })),
  );

const allowedMods: readonly AllowedModRule[] = [
  {
    id: "hwanhee1.corum_ranked",
    displayName: "Corum Ranked",
    required: true,
    enabled: true,
  },
  {
    id: "syzzi.click_between_frames",
    displayName: "Click Between Frames",
    required: true,
    enabled: true,
  },
];

export const configDocumentFixture = (generation = "test-1"): RankedConfigDocument => ({
  generation,
  generatedAt: "2026-08-20T00:00:00.000Z",
  operational: operational(generation),
  maps: maps(),
  allowedMods,
});

export const environmentFixture = (): ServerEnvironment => ({
  nodeEnv: "test",
  port: 3000,
  databaseUrl: "postgresql://test:test@127.0.0.1:5432/test",
  rankedConfigUrl: "http://127.0.0.1/config",
  rankedConfigRefreshMs: 60_000,
  rankedConfigFetchTimeoutMs: 2_000,
  sessionTokenSecret: "test-only-not-a-production-secret",
  corsOrigins: [],
  discordRelay: null,
  debugBotMatch: null,
});
