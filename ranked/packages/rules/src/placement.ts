import { RankedDomainError } from "./errors.js";
import { canonicalActiveMaps, snapshotMap } from "./pool.js";
import { sample, type RandomSource } from "./random.js";
import { scoreAttempt } from "./scoring.js";
import type {
  PlacementGrade,
  PlacementPolicy,
  PoolNumber,
  RankedMap,
  RankedMapSnapshot,
  TierBand,
} from "./types.js";

export const PLACEMENT_MAP_SCORE_MAXIMUM = 500;

export const DEFAULT_PLACEMENT_POLICY: PlacementPolicy = Object.freeze({
  stageCount: 5,
  mapsPerStage: 2,
  roundSeconds: 180,
  finalAttemptWindowSeconds: 10,
  prepareSeconds: 10,
  levelRevealRemainingSeconds: 5,
  gradeThresholds: Object.freeze({ sMin: 425, aMin: 350, bMin: 250, cMin: 150 }),
  transitionGuardSeconds: 0.3,
  topAttemptCount: 2,
  clearTarget: 2,
  poolRatingAnchors: Object.freeze([0, 1_000, 2_000, 3_000, 4_000, 5_000] as const),
  mapScoreCenter: 300,
  ratingPerMapScorePoint: 2.5,
  mapRatingWeight: 0.6,
  stageRatingWeight: 0.25,
  overallRatingWeight: 0.15,
});

export interface PlacementAttemptResult {
  readonly attemptId: string;
  readonly progressPercent: number;
  readonly cleared: boolean;
  readonly valid: boolean;
  readonly awardedScore: number;
}

export interface PlacementMapScore {
  readonly mapScore: number;
  readonly grade: PlacementGrade;
  readonly clearCount: number;
  readonly completedByClearTarget: boolean;
  readonly topAttempts: readonly PlacementAttemptResult[];
  readonly validAttemptCount: number;
}

export interface PlacementStageScore {
  readonly stageScore: number;
  readonly grade: PlacementGrade;
  readonly poolDelta: -2 | -1 | 0 | 1 | 2;
  readonly nextPool: PoolNumber;
}

export interface PlacementRatingMapResult {
  readonly pool: PoolNumber;
  readonly mapScore: number;
  readonly mapGrade: PlacementGrade;
  readonly stageIndex: number;
}

export interface PlacementRatingResult {
  readonly placementRating: number;
  readonly hiddenMmr: number;
  readonly mapComponent: number;
  readonly stageComponent: number;
  readonly overallComponent: number;
  readonly overallMapScore: number;
}

const clamp = (value: number, minimum: number, maximum: number): number =>
  Math.max(minimum, Math.min(maximum, value));

const assertMapScore = (value: number): void => {
  if (!Number.isFinite(value) || value < 0 || value > PLACEMENT_MAP_SCORE_MAXIMUM) {
    throw new RankedDomainError(
      "INVALID_ATTEMPT",
      `Placement Map Score must be from 0 through ${PLACEMENT_MAP_SCORE_MAXIMUM}`,
      { value },
    );
  }
};

export const placementGradeForMapScore = (
  mapScore: number,
  policy: PlacementPolicy,
  clearCount = 0,
): PlacementGrade => {
  assertMapScore(mapScore);
  if (clearCount >= policy.clearTarget) return "S";
  const thresholds = policy.gradeThresholds;
  if (mapScore >= thresholds.sMin) return "S";
  if (mapScore >= thresholds.aMin) return "A";
  if (mapScore >= thresholds.bMin) return "B";
  if (mapScore >= thresholds.cMin) return "C";
  return "D";
};

/** Scores a Placement attempt through the exact same curve as Ranked PvP. */
export const scorePlacementAttempt = (
  attemptId: string,
  progressPercent: number,
  cleared: boolean,
  qualifyingPercent: number,
): PlacementAttemptResult => ({
  attemptId,
  progressPercent,
  cleared,
  valid: true,
  awardedScore: scoreAttempt(progressPercent, cleared, qualifyingPercent),
});

const sameAttempt = (left: PlacementAttemptResult, right: PlacementAttemptResult): boolean =>
  left.progressPercent === right.progressPercent &&
  left.cleared === right.cleared &&
  left.valid === right.valid &&
  left.awardedScore === right.awardedScore;

/**
 * Deduplicates by authoritative attempt identity, then sums only the two
 * highest valid awarded scores. A conflicting retry is rejected instead of
 * being counted twice or silently changing prior evidence.
 */
export const calculatePlacementMapScore = (
  attempts: readonly PlacementAttemptResult[],
  policy: PlacementPolicy,
): PlacementMapScore => {
  const unique = new Map<string, PlacementAttemptResult>();
  for (const attempt of attempts) {
    if (!attempt.attemptId.trim()) {
      throw new RankedDomainError("INVALID_ATTEMPT", "Placement attemptId is required");
    }
    const previous = unique.get(attempt.attemptId);
    if (previous) {
      if (!sameAttempt(previous, attempt)) {
        throw new RankedDomainError(
          "INVALID_ATTEMPT",
          "A Placement attempt retry conflicts with its original result",
          { attemptId: attempt.attemptId },
        );
      }
      continue;
    }
    if (!Number.isFinite(attempt.progressPercent) || attempt.progressPercent < 0 ||
      attempt.progressPercent > 100 || (attempt.cleared && attempt.progressPercent !== 100)) {
      throw new RankedDomainError("INVALID_ATTEMPT", "Placement attempt progress/clear is invalid", {
        attemptId: attempt.attemptId,
      });
    }
    if (!Number.isFinite(attempt.awardedScore) || attempt.awardedScore < 0 ||
      attempt.awardedScore > 250) {
      throw new RankedDomainError("INVALID_ATTEMPT", "Placement awardedScore must be from 0 through 250", {
        attemptId: attempt.attemptId,
        awardedScore: attempt.awardedScore,
      });
    }
    unique.set(attempt.attemptId, attempt);
  }
  const validAttempts = [...unique.values()].filter((attempt) => attempt.valid);
  const clearCount = validAttempts.filter((attempt) => attempt.cleared).length;
  const topAttempts = [...validAttempts]
    .sort((left, right) => right.awardedScore - left.awardedScore ||
      left.attemptId.localeCompare(right.attemptId))
    .slice(0, policy.topAttemptCount);
  const completedByClearTarget = clearCount >= policy.clearTarget;
  const mapScore = completedByClearTarget
    ? PLACEMENT_MAP_SCORE_MAXIMUM
    : topAttempts.reduce((total, attempt) => total + attempt.awardedScore, 0);
  return {
    mapScore,
    grade: placementGradeForMapScore(mapScore, policy, clearCount),
    clearCount,
    completedByClearTarget,
    topAttempts,
    validAttemptCount: validAttempts.length,
  };
};

export const clampPlacementPool = (pool: number): PoolNumber =>
  clamp(Math.round(pool), 1, 6) as PoolNumber;

export const calculatePlacementStageScore = (
  mapScores: readonly [PlacementMapScore, PlacementMapScore],
  currentPool: PoolNumber,
  policy: PlacementPolicy,
): PlacementStageScore => {
  const stageScore = (mapScores[0].mapScore + mapScores[1].mapScore) / 2;
  const grade = placementGradeForMapScore(stageScore, policy);
  let poolDelta: -2 | -1 | 0 | 1 | 2;
  switch (grade) {
    case "S": poolDelta = mapScores.every((map) => map.grade === "S") ? 2 : 1; break;
    case "A": poolDelta = 1; break;
    case "B": poolDelta = 0; break;
    case "C": poolDelta = -1; break;
    case "D": poolDelta = mapScores.every((map) => map.grade === "D") ? -2 : -1; break;
  }
  return {
    stageScore,
    grade,
    poolDelta,
    nextPool: clampPlacementPool(currentPool + poolDelta),
  };
};

/**
 * Picks two canonical maps from the current pool. Globally unused maps are
 * preferred, while same-stage duplication is forbidden even during fallback.
 */
export const selectPlacementStageMaps = (
  pool: PoolNumber,
  maps: readonly RankedMap[],
  usedCanonicalIds: readonly string[],
  random: RandomSource,
): readonly [RankedMapSnapshot, RankedMapSnapshot] => {
  const available = canonicalActiveMaps(maps).filter((map) => map.pool === pool);
  if (available.length < 2) {
    throw new RankedDomainError(
      "INSUFFICIENT_POOL_MAPS",
      `Placement Pool ${pool} requires at least two canonical maps`,
      { pool, available: available.length },
    );
  }
  const used = new Set(usedCanonicalIds);
  const unused = available.filter((map) => !used.has(map.canonicalLevelId));
  const firstCandidates = unused.length > 0 ? unused : available;
  const first = sample(firstCandidates, 1, random)[0]!;
  const remainingUnused = unused.filter((map) => map.canonicalLevelId !== first.canonicalLevelId);
  const secondCandidates = remainingUnused.length > 0
    ? remainingUnused
    : available.filter((map) => map.canonicalLevelId !== first.canonicalLevelId);
  const second = sample(secondCandidates, 1, random)[0]!;
  return [snapshotMap(first), snapshotMap(second)];
};

/**
 * Selects one stable representative map for each Pool. The median qualifying
 * requirement is more representative than a random/first row, and canonical
 * ID tie-breaking keeps the selection stable across server restarts.
 */
export const selectPlacementReferenceMaps = (
  maps: readonly RankedMap[],
): readonly RankedMapSnapshot[] => {
  const canonical = canonicalActiveMaps(maps);
  return ([1, 2, 3, 4, 5, 6] as const).map((pool) => {
    const available = canonical
      .filter((map) => map.pool === pool)
      .sort((left, right) =>
        left.qualifyingPercent - right.qualifyingPercent ||
        left.canonicalLevelId.localeCompare(right.canonicalLevelId));
    if (available.length < 2) {
      throw new RankedDomainError(
        "INSUFFICIENT_POOL_MAPS",
        `Placement Pool ${pool} requires at least two canonical maps`,
        { pool, available: available.length },
      );
    }
    return snapshotMap(available[Math.floor((available.length - 1) / 2)]!);
  });
};

/**
 * Three-stage Placement intentionally trades certainty for speed. Its result
 * can never seed the player above the highest score that Placement may assign
 * to AQUA. The -16 offset preserves the global inclusive +/-15 cutoff
 * exclusion around the BRONZE boundary.
 */
export const capPlacementRatingForStageCount = (
  rating: number,
  stageCount: number,
  bands: readonly TierBand[],
): number => {
  if (!Number.isFinite(rating)) {
    throw new RankedDomainError("INVALID_CONFIG", "Placement rating must be finite");
  }
  if (stageCount === 5) return rating;
  if (stageCount !== 3) {
    throw new RankedDomainError("INVALID_CONFIG", "Placement stageCount must be 3 or 5", { stageCount });
  }
  const aqua = bands.find((band) => band.tier === "AQUA");
  const bronze = bands.find((band) => band.tier === "BRONZE");
  if (!aqua || !bronze || bronze.minInclusive <= aqua.minInclusive) {
    throw new RankedDomainError("INVALID_CONFIG", "AQUA/BRONZE tier boundaries are invalid");
  }
  return Math.min(rating, bronze.minInclusive - 16);
};

const ratingEvidence = (
  pool: PoolNumber,
  mapScore: number,
  policy: PlacementPolicy,
): number => policy.poolRatingAnchors[pool - 1]! +
  (mapScore - policy.mapScoreCenter) * policy.ratingPerMapScorePoint;

/** Uses all ten map scores and grades, all five stage aggregates, and the whole-run aggregate. */
export const calculatePlacementRating = (
  maps: readonly PlacementRatingMapResult[],
  policy: PlacementPolicy,
): PlacementRatingResult => {
  const expectedMaps = policy.stageCount * policy.mapsPerStage;
  if (maps.length !== expectedMaps) {
    throw new RankedDomainError("INVALID_CONFIG", `Placement requires exactly ${expectedMaps} map results`, {
      actual: maps.length,
    });
  }
  maps.forEach((map) => {
    assertMapScore(map.mapScore);
    if (placementGradeForMapScore(map.mapScore, policy) !== map.mapGrade) {
      throw new RankedDomainError("INVALID_CONFIG", "Placement Map Grade does not match Map Score", { map });
    }
    if (!Number.isInteger(map.stageIndex) || map.stageIndex < 0 || map.stageIndex >= policy.stageCount) {
      throw new RankedDomainError("INVALID_CONFIG", "Placement stage index is out of range", { map });
    }
  });
  const mapComponent = maps.reduce(
    (total, map) => total + ratingEvidence(map.pool, map.mapScore, policy),
    0,
  ) / maps.length;
  const stages = Array.from({ length: policy.stageCount }, (_, stageIndex) => {
    const stageMaps = maps.filter((map) => map.stageIndex === stageIndex);
    if (stageMaps.length !== policy.mapsPerStage) {
      throw new RankedDomainError("INVALID_CONFIG", "Each Placement stage requires exactly two maps", {
        stageIndex,
        actual: stageMaps.length,
      });
    }
    const stageScore = stageMaps.reduce((total, map) => total + map.mapScore, 0) /
      stageMaps.length;
    placementGradeForMapScore(stageScore, policy);
    const pool = clampPlacementPool(
      stageMaps.reduce((total, map) => total + map.pool, 0) / stageMaps.length,
    );
    return ratingEvidence(pool, stageScore, policy);
  });
  const stageComponent = stages.reduce((total, value) => total + value, 0) / stages.length;
  const overallMapScore = maps.reduce((total, map) => total + map.mapScore, 0) / maps.length;
  const averagePool = clampPlacementPool(
    maps.reduce((total, map) => total + map.pool, 0) / maps.length,
  );
  const overallComponent = ratingEvidence(averagePool, overallMapScore, policy);
  const placementRating = mapComponent * policy.mapRatingWeight +
    stageComponent * policy.stageRatingWeight +
    overallComponent * policy.overallRatingWeight;
  return {
    placementRating,
    hiddenMmr: Math.round(placementRating),
    mapComponent,
    stageComponent,
    overallComponent,
    overallMapScore,
  };
};
