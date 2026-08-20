import { describe, expect, it } from "vitest";
import {
  CANDIDATE_POOL_DISTRIBUTIONS,
  RankedDomainError,
  SeededRandom,
  effectiveTierForMatch,
  resolvePlayableLevelId,
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

  it("resolves a valid alternate ID first and falls back to canonical", () => {
    expect(resolvePlayableLevelId(makeMap(2, 1))).toBe("2001");
    expect(resolvePlayableLevelId(makeMap(2, 1, { alternateLevelId: "invalid" }))).toBe(
      "2001",
    );
    expect(resolvePlayableLevelId(makeMap(2, 1, { alternateLevelId: "2001" }))).toBe(
      "2001",
    );
    expect(resolvePlayableLevelId(makeMap(2, 1, { alternateLevelId: "92001" }))).toBe(
      "92001",
    );
    const alternate = snapshotMap(makeMap(2, 1, { alternateLevelId: "92001" }));
    expect(alternate.alternateLevelId).toBe("92001");
    expect(alternate.playableLevelId).toBe("92001");
  });

  it("rejects aliases that collide with another canonical map", () => {
    const maps = allPoolMaps();
    maps[0] = { ...maps[0]!, alternateLevelId: maps[1]!.canonicalLevelId };
    expect(() => selectCandidateMaps("RED", maps, new SeededRandom(1))).toThrowError(
      expect.objectContaining<Partial<RankedDomainError>>({ code: "CONFLICTING_MAP_ALIAS" }),
    );
  });

  it("rejects conflicting active registrations of one canonical level", () => {
    const maps = allPoolMaps();
    maps.push(
      makeMap(4, 99, {
        canonicalLevelId: maps[0]!.canonicalLevelId,
        qualifyingPercent: 99,
      }),
    );
    expect(() => selectCandidateMaps("RED", maps, new SeededRandom(1))).toThrowError(
      expect.objectContaining<Partial<RankedDomainError>>({ code: "CONFLICTING_CANONICAL_MAP" }),
    );
  });

  it("fails loudly instead of distorting a pool with insufficient maps", () => {
    const maps = allPoolMaps().filter(
      (map) => map.pool !== 2 || map.canonicalLevelId.endsWith("001"),
    );
    expect(() => selectCandidateMaps("RED", maps, new SeededRandom(1))).toThrowError(
      expect.objectContaining<Partial<RankedDomainError>>({ code: "INSUFFICIENT_POOL_MAPS" }),
    );
  });

  it("snapshots fields so later source changes do not alter selected maps", () => {
    const maps = allPoolMaps();
    const selected = selectCandidateMaps("GOLD", maps, new SeededRandom(91));
    const chosen = maps.find((map) => map.canonicalLevelId === selected[0]!.canonicalLevelId)!;
    const originalTitle = selected[0]!.title;
    const originalPlayableLevelId = selected[0]!.playableLevelId;
    (chosen as { title: string }).title = "Spreadsheet changed later";
    (chosen as { alternateLevelId: string | null }).alternateLevelId = "999999";
    expect(selected[0]!.title).toBe(originalTitle);
    expect(selected[0]!.playableLevelId).toBe(originalPlayableLevelId);
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
