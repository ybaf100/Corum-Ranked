import { describe, expect, it } from "vitest";
import {
  CANDIDATE_POOL_DISTRIBUTIONS,
  RankedDomainError,
  SeededRandom,
  effectiveTierForMatch,
  resolveBans,
  selectCandidateMaps,
  snapshotMap,
} from "../src/index.js";
import { allPoolMaps, makeMap, tierBandsFixture } from "./fixtures.js";

describe("effective tier and candidate pools", () => {
  it("uses the average hidden rating for the effective tier", () => {
    expect(effectiveTierForMatch(1_700, 2_500, tierBandsFixture)).toEqual({
      averageRating: 2_100,
      tier: "BRONZE",
    });
  });

  it.each(["RED", "AQUA", "BRONZE", "SILVER", "GOLD"] as const)(
    "%s selects exactly the documented five-map distribution",
    (tier) => {
      const selected = selectCandidateMaps(tier, allPoolMaps(), new SeededRandom(1234));
      const counts = Object.fromEntries(
        [1, 2, 3, 4, 5, 6].map((pool) => [
          pool,
          selected.filter((map) => map.pool === pool).length,
        ]),
      );
      for (const pool of [1, 2, 3, 4, 5, 6] as const) {
        expect(counts[pool]).toBe(CANDIDATE_POOL_DISTRIBUTIONS[tier][pool] ?? 0);
      }
      expect(new Set(selected.map((map) => map.canonicalLevelId)).size).toBe(5);
    },
  );

  it("deduplicates equivalent registrations by canonical ID", () => {
    const maps = allPoolMaps();
    const original = maps.find((map) => map.pool === 2);
    expect(original).toBeDefined();
    maps.push({ ...original! });
    const selected = selectCandidateMaps("RED", maps, new SeededRandom(8));
    expect(new Set(selected.map((map) => map.canonicalLevelId)).size).toBe(5);
  });

  it("uses a valid alternate Level ID as playable ID and snapshots both IDs", () => {
    const resolved = snapshotMap(makeMap(3, 7, {
      alternateLevelId: "90000007",
      playableLevelId: "90000007",
      levelId: "90000007",
    }));
    expect(resolved).toMatchObject({
      canonicalLevelId: "3007",
      alternateLevelId: "90000007",
      playableLevelId: "90000007",
      levelId: "90000007",
    });
  });

  it("rejects a canonical/alternate alias collision between two maps", () => {
    const maps = allPoolMaps();
    const first = maps[0]!;
    const second = maps[1]!;
    maps[0] = {
      ...first,
      alternateLevelId: second.canonicalLevelId,
      playableLevelId: second.canonicalLevelId,
      levelId: second.canonicalLevelId,
    };
    expect(() => selectCandidateMaps("RED", maps, new SeededRandom(1))).toThrowError(
      expect.objectContaining<Partial<RankedDomainError>>({ code: "CONFLICTING_MAP_ALIAS" }),
    );
  });

  it("rejects conflicting active registrations of one canonical level", () => {
    const maps = allPoolMaps();
    const canonicalLevelId = maps[0]!.canonicalLevelId;
    maps.push(
      makeMap(4, 99, {
        levelId: canonicalLevelId,
        canonicalLevelId,
        playableLevelId: canonicalLevelId,
        qualifyingPercent: 99,
      }),
    );
    expect(() => selectCandidateMaps("RED", maps, new SeededRandom(1))).toThrowError(
      expect.objectContaining<Partial<RankedDomainError>>({ code: "CONFLICTING_CANONICAL_MAP" }),
    );
  });

  it("rejects one canonical map registered with two different alternate IDs", () => {
    const maps = allPoolMaps();
    const original = maps[0]!;
    maps.push({
      ...original,
      alternateLevelId: "99900001",
      playableLevelId: "99900001",
      levelId: "99900001",
    });
    expect(() => selectCandidateMaps("RED", maps, new SeededRandom(1))).toThrowError(
      expect.objectContaining<Partial<RankedDomainError>>({ code: "CONFLICTING_CANONICAL_MAP" }),
    );
  });

  it("fails loudly instead of distorting a pool with insufficient maps", () => {
    const maps = allPoolMaps().filter((map) => map.pool !== 2 || map.levelId.endsWith("001"));
    expect(() => selectCandidateMaps("RED", maps, new SeededRandom(1))).toThrowError(
      expect.objectContaining<Partial<RankedDomainError>>({ code: "INSUFFICIENT_POOL_MAPS" }),
    );
  });

  it("snapshots fields so later source changes do not alter selected maps", () => {
    const maps = allPoolMaps();
    const selected = selectCandidateMaps("GOLD", maps, new SeededRandom(91));
    const chosen = maps.find((map) => map.canonicalLevelId === selected[0]!.canonicalLevelId)!;
    const originalTitle = selected[0]!.title;
    (chosen as { title: string }).title = "Spreadsheet changed later";
    expect(selected[0]!.title).toBe(originalTitle);
  });
});

describe("simultaneous private bans", () => {
  const candidates = selectCandidateMaps("AQUA", allPoolMaps(), new SeededRandom(77));

  it("removes a shared ban once, then selects and orders three maps", () => {
    const shared = candidates[0]!.canonicalLevelId;
    const resolution = resolveBans(candidates, shared, shared, new SeededRandom(10));
    expect(resolution.removedCanonicalIds).toEqual([shared]);
    expect(resolution.selectedRoundMaps).toHaveLength(3);
    expect(resolution.selectedRoundMaps.every((map) => map.canonicalLevelId !== shared)).toBe(true);
  });

  it("supports one No Ban", () => {
    const resolution = resolveBans(
      candidates,
      candidates[0]!.canonicalLevelId,
      null,
      new SeededRandom(11),
    );
    expect(resolution.bans.B).toBeNull();
    expect(resolution.selectedRoundMaps).toHaveLength(3);
  });

  it("supports two No Bans", () => {
    const resolution = resolveBans(candidates, null, null, new SeededRandom(12));
    expect(resolution.removedCanonicalIds).toEqual([]);
    expect(resolution.selectedRoundMaps).toHaveLength(3);
  });

  it("rejects a ban outside the candidate set", () => {
    expect(() => resolveBans(candidates, "not-a-candidate", null, new SeededRandom(12))).toThrowError(
      expect.objectContaining<Partial<RankedDomainError>>({ code: "INVALID_BAN" }),
    );
  });
});
