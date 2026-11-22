import { describe, expect, it } from "vitest";
import { canonicalActiveMaps, RankedDomainError, resolveFixedMatchMaps, snapshotMap } from "../src/index.js";
import { allPoolMaps } from "./fixtures.js";

describe("designated-map matches (debug)", () => {
  const maps = canonicalActiveMaps(allPoolMaps());
  const fillers = maps.slice(10, 15).map(snapshotMap);
  const id = (index: number) => maps[index]!.canonicalLevelId;

  it("keeps the chosen rounds in order and fills empty slots from the draw", () => {
    const fixed = resolveFixedMatchMaps({ rounds: [id(3), null, id(1)], deathmatch: id(2) }, allPoolMaps(), fillers, 3);
    expect(fixed.rounds.map((m) => m.canonicalLevelId)).toEqual([id(3), fillers[0]!.canonicalLevelId, id(1)]);
    expect(fixed.deathmatch?.canonicalLevelId).toBe(id(2));
    // Fewer chosen than the rounds: the rest come from the draw, never a repeat.
    const short = resolveFixedMatchMaps({ rounds: [fillers[0]!.canonicalLevelId] }, allPoolMaps(), fillers, 3);
    expect(short.rounds.map((m) => m.canonicalLevelId)).toEqual(fillers.slice(0, 3).map((m) => m.canonicalLevelId));
    expect(short.deathmatch).toBeNull();
  });

  it("refuses unknown, repeated or too many maps", () => {
    expect(() => resolveFixedMatchMaps({ rounds: ["nope"] }, allPoolMaps(), fillers, 3)).toThrow(RankedDomainError);
    expect(() => resolveFixedMatchMaps({ rounds: [id(1), id(1)] }, allPoolMaps(), fillers, 3)).toThrow(/one round only/);
    expect(() => resolveFixedMatchMaps({ rounds: [id(1), id(2)] }, allPoolMaps(), fillers, 1)).toThrow(/At most 1/);
    expect(() => resolveFixedMatchMaps({ rounds: [], deathmatch: "nope" }, allPoolMaps(), fillers, 3)).toThrow(RankedDomainError);
  });
});
