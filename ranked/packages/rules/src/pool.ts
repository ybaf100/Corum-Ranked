import { RankedDomainError } from "./errors.js";
import { sample, shuffle, type RandomSource } from "./random.js";
import type {
  PoolNumber,
  RankedMap,
  RankedMapSnapshot,
  RankedTier,
} from "./types.js";

export type PoolDistribution = Readonly<Partial<Record<PoolNumber, number>>>;

export const candidatePoolDistribution = (mainPool: PoolNumber, count: 4 | 5 = 5): PoolDistribution => {
  if (!Number.isInteger(mainPool) || mainPool < 1 || mainPool > 6) {
    throw new RankedDomainError("INVALID_CONFIG", "Main Pool must be an integer from 1 through 6", {
      mainPool,
    });
  }
  // Single games show four candidates: one fewer from the neighbour/main share,
  // so every four-candidate requirement is covered by the five-candidate one.
  if (count === 4) {
    if (mainPool === 1) return Object.freeze({ 1: 2, 2: 1, 3: 1 });
    if (mainPool === 6) return Object.freeze({ 4: 1, 5: 1, 6: 2 });
    return Object.freeze({
      [(mainPool - 1) as PoolNumber]: 1,
      [mainPool]: 2,
      [(mainPool + 1) as PoolNumber]: 1,
    });
  }
  if (mainPool === 1) return Object.freeze({ 1: 2, 2: 2, 3: 1 });
  if (mainPool === 6) return Object.freeze({ 4: 1, 5: 2, 6: 2 });
  return Object.freeze({
    [(mainPool - 1) as PoolNumber]: 1,
    [mainPool]: 3,
    [(mainPool + 1) as PoolNumber]: 1,
  });
};

const validateMap = (map: RankedMap): void => {
  if (!isValidLevelId(map.canonicalLevelId)) {
    throw new RankedDomainError("INVALID_MAP", "A positive canonical Level ID is required", { map });
  }
  const expectedPlayable = resolvePlayableLevelId(map.canonicalLevelId, map.alternateLevelId);
  if (map.playableLevelId !== expectedPlayable || map.levelId !== expectedPlayable) {
    throw new RankedDomainError(
      "INVALID_MAP",
      "playableLevelId and levelId must exactly match alternateLevelId",
      { map, expectedPlayable },
    );
  }
  if (!Number.isInteger(map.pool) || map.pool < 1 || map.pool > 6) {
    throw new RankedDomainError("INVALID_MAP", "Pool must be an integer from 1 through 6", { map });
  }
  if (!Number.isFinite(map.qualifyingPercent) || map.qualifyingPercent < 0 || map.qualifyingPercent > 100) {
    throw new RankedDomainError("INVALID_MAP", "Qualifying percent must be from 0 through 100", {
      map,
    });
  }
};

const isValidLevelId = (value: string | null | undefined): value is string =>
  typeof value === "string" && /^[1-9]\d*$/.test(value.trim());

export const resolvePlayableLevelId = (
  canonicalLevelId: string,
  alternateLevelId: string | null | undefined,
): string => {
  const canonical = canonicalLevelId.trim();
  if (!isValidLevelId(canonical)) {
    throw new RankedDomainError("INVALID_MAP", "A positive canonical Level ID is required");
  }
  const alternate = alternateLevelId?.trim() ?? "";
  if (!isValidLevelId(alternate)) {
    throw new RankedDomainError(
      "INVALID_MAP",
      "A positive alternate Level ID is required for every Ranked map; canonical fallback is forbidden",
      { canonicalLevelId: canonical, alternateLevelId },
    );
  }
  return alternate;
};

const equivalentCanonicalRegistration = (left: RankedMap, right: RankedMap): boolean =>
  left.alternateLevelId === right.alternateLevelId &&
  left.playableLevelId === right.playableLevelId &&
  left.pool === right.pool &&
  left.qualifyingPercent === right.qualifyingPercent &&
  left.title === right.title &&
  left.creator === right.creator &&
  left.difficulty === right.difficulty;

export const canonicalActiveMaps = (maps: readonly RankedMap[]): RankedMap[] => {
  const canonical = new Map<string, RankedMap>();
  const aliases = new Map<string, string>();
  for (const map of maps) {
    // Preserve the stronger canonical-registration error when a duplicate row is
    // already known to describe the same canonical map differently. The first
    // registration was validated when inserted; validate the current row after
    // conflict classification so legacy regression tests and operator messages
    // do not get downgraded to a secondary field-shape error.
    const existing = map.active ? canonical.get(map.canonicalLevelId) : undefined;
    if (existing && !equivalentCanonicalRegistration(existing, map)) {
      throw new RankedDomainError(
        "CONFLICTING_CANONICAL_MAP",
        `Canonical level ${map.canonicalLevelId} has conflicting active registrations`,
        { existing, conflicting: map },
      );
    }
    validateMap(map);
    if (!map.active) continue;
    if (!existing) canonical.set(map.canonicalLevelId, map);
    const identifiers = [map.canonicalLevelId, map.alternateLevelId].filter(isValidLevelId);
    for (const identifier of identifiers) {
      const owner = aliases.get(identifier);
      if (owner && owner !== map.canonicalLevelId) {
        throw new RankedDomainError(
          "CONFLICTING_MAP_ALIAS",
          `Level ID ${identifier} belongs to more than one canonical Ranked map`,
          { identifier, owner, conflicting: map.canonicalLevelId },
        );
      }
      aliases.set(identifier, map.canonicalLevelId);
    }
  }
  return [...canonical.values()];
};

export const snapshotMap = (map: RankedMap): RankedMapSnapshot =>
  Object.freeze({
    levelId: resolvePlayableLevelId(map.canonicalLevelId, map.alternateLevelId),
    canonicalLevelId: map.canonicalLevelId,
    alternateLevelId: map.alternateLevelId,
    playableLevelId: resolvePlayableLevelId(map.canonicalLevelId, map.alternateLevelId),
    title: map.title,
    creator: map.creator,
    difficulty: map.difficulty,
    ...(map.length !== undefined ? { length: map.length } : {}),
    ...(map.metadata ? { metadata: Object.freeze({ ...map.metadata }) } : {}),
    pool: map.pool,
    qualifyingPercent: map.qualifyingPercent,
  });

/** Reattach optional display fields from the match/run's pinned catalog. */
export const mapPresentationFields = (
  maps: readonly RankedMap[], playableLevelId: string,
): Pick<RankedMapSnapshot, "metadata" | "length"> => {
  const map = maps.find(candidate => candidate.playableLevelId === playableLevelId);
  return {
    ...(map?.length !== undefined ? { length: map.length } : {}),
    ...(map?.metadata ? { metadata: { ...map.metadata } } : {}),
  };
};

export const selectCandidateMaps = (
  tier: RankedTier,
  mainPool: PoolNumber,
  maps: readonly RankedMap[],
  random: RandomSource,
  count: 4 | 5 = 5,
): readonly RankedMapSnapshot[] => {
  const canonical = canonicalActiveMaps(maps);
  const distribution = candidatePoolDistribution(mainPool, count);
  const selected: RankedMap[] = [];

  for (const [poolText, required] of Object.entries(distribution)) {
    const pool = Number(poolText) as PoolNumber;
    const available = canonical.filter((map) => map.pool === pool);
    if (available.length < required) {
      throw new RankedDomainError(
        "INSUFFICIENT_POOL_MAPS",
        `Pool ${pool} has ${available.length} canonical maps but ${required} are required for ${tier} Main Pool ${mainPool}`,
        { tier, mainPool, pool, required, available: available.length },
      );
    }
    selected.push(...sample(available, required, random));
  }

  if (selected.length !== count || new Set(selected.map((map) => map.canonicalLevelId)).size !== count) {
    throw new RankedDomainError("INSUFFICIENT_POOL_MAPS", `Candidate maps must be ${count} canonical levels`, {
      tier,
      mainPool,
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

/** A designated-map match (debug): the round maps in order and an optional
 * Death Match map, by canonical id. */
export interface FixedMapRequest {
  readonly rounds: readonly (string | null)[];
  readonly deathmatch?: string | null;
}
export interface FixedMatchMaps {
  readonly rounds: readonly RankedMapSnapshot[];
  readonly deathmatch: RankedMapSnapshot | null;
}

/**
 * Resolves a designated-map request against the active map pool: exactly
 * `roundCount` round maps in the requested order, each empty slot filled from
 * `fillers` (unused, in order). An unknown or repeated map is refused.
 */
export const resolveFixedMatchMaps = (
  request: FixedMapRequest,
  maps: readonly RankedMap[],
  fillers: readonly RankedMapSnapshot[],
  roundCount: number,
): FixedMatchMaps => {
  const canonical = canonicalActiveMaps(maps);
  const find = (id: string): RankedMapSnapshot => {
    const map = canonical.find((entry) => entry.canonicalLevelId === id);
    if (!map) throw new RankedDomainError("INVALID_CONFIG", `Map ${id} is not in the active map pool`, { canonicalLevelId: id });
    return snapshotMap(map);
  };
  if (request.rounds.length > roundCount) {
    throw new RankedDomainError("INVALID_CONFIG", `At most ${roundCount} round maps can be chosen`, { roundCount });
  }
  const chosen = request.rounds.map((id) => (id ? find(id) : null));
  const ids = chosen.filter((map): map is RankedMapSnapshot => map !== null).map((map) => map.canonicalLevelId);
  if (new Set(ids).size !== ids.length) throw new RankedDomainError("INVALID_CONFIG", "A map can be chosen for one round only", {});
  const spare = fillers.filter((map) => !ids.includes(map.canonicalLevelId));
  const rounds: RankedMapSnapshot[] = [];
  for (let index = 0; index < roundCount; index++) {
    const map = chosen[index] ?? spare.shift();
    if (!map) throw new RankedDomainError("INSUFFICIENT_POOL_MAPS", "Not enough maps to fill the rounds", { roundCount });
    rounds.push(map);
  }
  return Object.freeze({ rounds: Object.freeze(rounds), deathmatch: request.deathmatch ? find(request.deathmatch) : null });
};
