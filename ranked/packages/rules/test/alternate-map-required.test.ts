import { describe, expect, it } from "vitest";
import {
  RankedDomainError,
  SeededRandom,
  resolvePlayableLevelId,
  selectCandidateMaps,
  snapshotMap,
} from "../src/index.js";
import { allPoolMaps, makeMap } from "./fixtures.js";

describe("alternate Level ID is the only playable Ranked map ID", () => {
  it("rejects canonical fallback when alternateLevelId is missing", () => {
    expect(() => resolvePlayableLevelId("1001", null)).toThrowError(
      expect.objectContaining<Partial<RankedDomainError>>({ code: "INVALID_MAP" }),
    );
  });

  it("rejects an active pool containing a map without an alternate Level ID", () => {
    const maps = allPoolMaps();
    const first = maps[0]!;
    maps[0] = {
      ...first,
      alternateLevelId: null,
      playableLevelId: first.canonicalLevelId,
      levelId: first.canonicalLevelId,
    };
    expect(() => selectCandidateMaps("RED", maps, new SeededRandom(1))).toThrowError(
      expect.objectContaining<Partial<RankedDomainError>>({ code: "INVALID_MAP" }),
    );
  });

  it("snapshots alternateLevelId as both playableLevelId and legacy levelId", () => {
    const map = makeMap(3, 7, {
      alternateLevelId: "90000007",
      playableLevelId: "90000007",
      levelId: "90000007",
    });
    expect(snapshotMap(map)).toMatchObject({
      canonicalLevelId: "3007",
      alternateLevelId: "90000007",
      playableLevelId: "90000007",
      levelId: "90000007",
    });
  });
});
