import { RankedDomainError } from "./errors.js";
import { sample, shuffle, type RandomSource } from "./random.js";
import type { PlayerSide, RankedMapSnapshot } from "./types.js";

export type BanChoice = string | null;

export interface BanResolution {
  readonly bans: Readonly<Record<PlayerSide, BanChoice>>;
  readonly removedCanonicalIds: readonly string[];
  readonly selectedRoundMaps: readonly RankedMapSnapshot[];
}

const validateCandidateSet = (candidateMaps: readonly RankedMapSnapshot[], roundMaps: 1 | 3): void => {
  const expected = roundMaps === 1 ? 4 : 5;
  if (candidateMaps.length !== expected) {
    throw new RankedDomainError("INVALID_BAN", `Ban phase requires exactly ${expected} candidate maps`);
  }
  if (new Set(candidateMaps.map((map) => map.canonicalLevelId)).size !== expected) {
    throw new RankedDomainError("INVALID_BAN", "Ban candidates must be canonical-distinct");
  }
};

export const resolveBans = (
  candidateMaps: readonly RankedMapSnapshot[],
  banA: BanChoice,
  banB: BanChoice,
  random: RandomSource,
  /** BO3 plays three of five candidates; a single game plays one of four. */
  roundMaps: 1 | 3 = 3,
): BanResolution => {
  validateCandidateSet(candidateMaps, roundMaps);
  const candidateIds = new Set(candidateMaps.map((map) => map.canonicalLevelId));
  for (const [side, choice] of [
    ["A", banA],
    ["B", banB],
  ] as const) {
    if (choice !== null && !candidateIds.has(choice)) {
      throw new RankedDomainError("INVALID_BAN", `${side} selected a map outside the candidate set`, {
        side,
        choice,
      });
    }
  }

  const removedCanonicalIds = [...new Set([banA, banB].filter((id): id is string => id !== null))];
  const remaining = candidateMaps.filter((map) => !removedCanonicalIds.includes(map.canonicalLevelId));
  if (remaining.length < roundMaps) {
    throw new RankedDomainError("INVALID_BAN", "Too few maps remain after bans");
  }
  const selected = remaining.length > roundMaps ? sample(remaining, roundMaps, random) : [...remaining];
  const ordered = shuffle(selected, random);

  return Object.freeze({
    bans: Object.freeze({ A: banA, B: banB }),
    removedCanonicalIds: Object.freeze(removedCanonicalIds),
    selectedRoundMaps: Object.freeze(ordered),
  });
};
