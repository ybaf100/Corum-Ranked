import { describe, expect, it } from "vitest";
import {
  type MatchSeriesState,
  SeededRandom,
  applyDeathmatchWinner,
  applyRoundResult,
  candidateMapCountOf,
  candidatePoolDistribution,
  createMatchSeries,
  mapBanOf,
  matchFormatOf,
  resolveBans,
  roundMapCountOf,
  selectCandidateMaps,
} from "../src/index.js";
import { allPoolMaps } from "./fixtures.js";

describe("per-tier match format", () => {
  it("treats series created before formats as BO3 with a map ban", () => {
    const legacy: MatchSeriesState = { roundResults: [], roundWins: { A: 0, B: 0 }, phase: "ROUND_REQUIRED", winner: null };
    expect(matchFormatOf(legacy)).toBe("BO3");
    expect(mapBanOf(legacy)).toBe(true);
    expect(applyRoundResult(legacy, "A").nextRoundNumber).toBe(2);
  });

  it("decides a single game with its only round", () => {
    const series = createMatchSeries({ format: "SINGLE", mapBan: true });
    const decision = applyRoundResult(series, "B");
    expect(decision.state.phase).toBe("MATCH_RESULT");
    expect(decision.state.winner).toBe("B");
    expect(decision.state.format).toBe("SINGLE");
    expect(decision.nextRoundNumber).toBeNull();
  });

  it("sends a single-game draw straight to Death Match and keeps the format", () => {
    const series = createMatchSeries({ format: "SINGLE", mapBan: false });
    const decision = applyRoundResult(series, "DRAW");
    expect(decision.state.phase).toBe("DEATHMATCH_REQUIRED");
    const final = applyDeathmatchWinner(decision.state, "A");
    expect(final.winner).toBe("A");
    expect(final.format).toBe("SINGLE");
    expect(final.mapBan).toBe(false);
  });

  it("keeps the BO3 flow unchanged", () => {
    let state = createMatchSeries();
    state = applyRoundResult(state, "A").state;
    const second = applyRoundResult(state, "B");
    expect(second.nextRoundNumber).toBe(3);
    expect(applyRoundResult(second.state, "DRAW").state.phase).toBe("DEATHMATCH_REQUIRED");
  });

  it.each([1, 2, 3, 4, 5, 6] as const)("single games draw four candidates within the BO3 requirement (pool %s)", (mainPool) => {
    const four = candidatePoolDistribution(mainPool, 4);
    const five = candidatePoolDistribution(mainPool);
    expect(Object.values(four).reduce((sum, value) => sum + (value ?? 0), 0)).toBe(4);
    for (const [pool, count] of Object.entries(four)) {
      expect(count).toBeLessThanOrEqual(five[Number(pool) as 1]! ?? 0);
    }
    const candidates = selectCandidateMaps("AQUA", mainPool, allPoolMaps(), new SeededRandom(mainPool), candidateMapCountOf("SINGLE"));
    expect(candidates).toHaveLength(4);
  });

  it("resolves a single-game ban to one round map, with or without bans", () => {
    const candidates = selectCandidateMaps("AQUA", 3, allPoolMaps(), new SeededRandom(5), 4);
    const banned = resolveBans(candidates, candidates[0]!.canonicalLevelId, candidates[1]!.canonicalLevelId, new SeededRandom(6), roundMapCountOf("SINGLE"));
    expect(banned.selectedRoundMaps).toHaveLength(1);
    expect([candidates[0]!.canonicalLevelId, candidates[1]!.canonicalLevelId]).not.toContain(banned.selectedRoundMaps[0]!.canonicalLevelId);
    expect(resolveBans(candidates, null, null, new SeededRandom(7), 1).selectedRoundMaps).toHaveLength(1);
    expect(() => resolveBans(candidates, null, null, new SeededRandom(7))).toThrow();
  });
});
