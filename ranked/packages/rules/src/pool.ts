import { RankedDomainError } from "./errors.js";
import { sample, shuffle, type RandomSource } from "./random.js";
import type {
  PoolNumber,
  RankedMap,
  RankedMapSnapshot,
  RankedTier,
} from "./types.js";

export type PoolDistribution = Readonly<Partial<Record<PoolNumber, number>>>;

export const CANDIDATE_POOL_DISTRIBUTIONS: Readonly<Record<RankedTier, PoolDistribution>> =
  Object.freeze({
    RED: Object.freeze({ 1: 1, 2: 3, 3: 1 }),
    AQUA: Object.freeze({ 2: 1, 3: 3, 4: 1 }),
    BRONZE: Object.freeze({ 3: 1, 4: 3, 5: 1 }),
    SILVER: Object.freeze({ 4: 1, 5: 3, 6: 1 }),
    GOLD: Object.freeze({ 4: 1, 5: 2, 6: 2 }),
  });

const isValidLevelId = (value: string): boolean => /^[1-9]\d*$/.test(value);

export const normalizeAlternateLevelId = (
  map: Pick<RankedMap, "canonicalLevelId" | "alternateLevelId">,
): string | null =>
  map.alternateLevelId !== null &&
  isValidLevelId(map.alternateLevelId) &&
  map.alternateLevelId !== map.canonicalLevelId
    ? map.alternateLevelId
    : null;

export const resolvePlayableLevelId = (
  map: Pick<RankedMap, "canonicalLevelId" | "alternateLevelId">,
): string => {
  if (!isValidLevelId(map.canonicalLevelId)) {
    throw new RankedDomainError("INVALID_MAP", "Canonical level ID must be a positive integer", {
      map,
    });
  }
  return normalizeAlternateLevelId(map) ?? map.canonicalLevelId;
};

const validateMap = (map: RankedMap): void => {
  resolvePlayableLevelId(map);
  if (!Number.isInteger(map.pool) || map.pool < 1 || map.pool > 6) {
    throw new RankedDomainError("INVALID_MAP", "Pool must be an integer from 1 through 6", { map });
  }
  if (!Number.isFinite(map.qualifyingPercent) || map.qualifyingPercent < 0 || map.qualifyingPercent > 100) {
    throw new RankedDomainError("INVALID_MAP", "Qualifying percent must be from 0 through 100", {
      map,
    });
  }
};

const equivalentCanonicalRegistration = (left: RankedMap, right: RankedMap): boolean =>
  normalizeAlternateLevelId(left) === normalizeAlternateLevelId(right) &&
  left.pool === right.pool &&
  left.qualifyingPercent === right.qualifyingPercent &&
  left.title === right.title &&
  left.creator === right.creator &&
  left.difficulty === right.difficulty;

export const canonicalActiveMaps = (maps: readonly RankedMap[]): RankedMap[] => {
  const canonical = new Map<string, RankedMap>();
  for (const map of maps) {
    validateMap(map);
    if (!map.active) continue;
    const existing = canonical.get(map.canonicalLevelId);
    if (existing && !equivalentCanonicalRegistration(existing, map)) {
      throw new RankedDomainError(
        "CONFLICTING_CANONICAL_MAP",
        `Canonical level ${map.canonicalLevelId} has conflicting active registrations`,
        { existing, conflicting: map },
      );
    }
    if (!existing) canonical.set(map.canonicalLevelId, map);
  }
  const active = [...canonical.values()];
  const identityByLevelId = new Map<string, string>();
  for (const map of active) {
    const playableLevelId = resolvePlayableLevelId(map);
    for (const levelId of [map.canonicalLevelId, playableLevelId]) {
      const owner = identityByLevelId.get(levelId);
      if (owner && owner !== map.canonicalLevelId) {
        throw new RankedDomainError(
          "CONFLICTING_MAP_ALIAS",
          `Level ID ${levelId} belongs to multiple canonical maps`,
          { levelId, firstCanonicalLevelId: owner, secondCanonicalLevelId: map.canonicalLevelId },
        );
      }
      identityByLevelId.set(levelId, map.canonicalLevelId);
    }
  }
  return active;
};

export const snapshotMap = (map: RankedMap): RankedMapSnapshot =>
  Object.freeze({
    canonicalLevelId: map.canonicalLevelId,
    alternateLevelId: normalizeAlternateLevelId(map),
    playableLevelId: resolvePlayableLevelId(map),
    title: map.title,
    creator: map.creator,
    difficulty: map.difficulty,
    pool: map.pool,
    qualifyingPercent: map.qualifyingPercent,
  });

export const selectCandidateMaps = (
  tier: RankedTier,
  maps: readonly RankedMap[],
  random: RandomSource,
): readonly RankedMapSnapshot[] => {
  const canonical = canonicalActiveMaps(maps);
  const distribution = CANDIDATE_POOL_DISTRIBUTIONS[tier];
  const selected: RankedMap[] = [];

  for (const [poolText, required] of Object.entries(distribution)) {
    const pool = Number(poolText) as PoolNumber;
    const available = canonical.filter((map) => map.pool === pool);
    if (available.length < required) {
      throw new RankedDomainError(
        "INSUFFICIENT_POOL_MAPS",
        `Pool ${pool} has ${available.length} canonical maps but ${required} are required for ${tier}`,
        { tier, pool, required, available: available.length },
      );
    }
    selected.push(...sample(available, required, random));
  }

  if (selected.length !== 5 || new Set(selected.map((map) => map.canonicalLevelId)).size !== 5) {
    throw new RankedDomainError("INSUFFICIENT_POOL_MAPS", "Candidate maps must be five canonical levels", {
      tier,
    });
  }

  return Object.freeze(shuffle(selected, random).map(snapshotMap));
};

export const selectDeathmatchMap = (
  pool: PoolNumber,
  maps: readonly RankedMap[],
  priorCanonicalIds: readonly string[],
  random: RandomSource,
): RankedMapSnapshot => {
  const available = canonicalActiveMaps(maps).filter((map) => map.pool === pool);
  if (available.length === 0) {
    throw new RankedDomainError("INSUFFICIENT_POOL_MAPS", `Deathmatch Pool ${pool} is empty`, { pool });
  }
  const unused = available.filter((map) => !priorCanonicalIds.includes(map.canonicalLevelId));
  const candidates = unused.length > 0 ? unused : available;
  const chosen = sample(candidates, 1, random)[0];
  if (!chosen) {
    throw new RankedDomainError("INSUFFICIENT_POOL_MAPS", `Deathmatch Pool ${pool} is empty`, { pool });
  }
  return snapshotMap(chosen);
};
