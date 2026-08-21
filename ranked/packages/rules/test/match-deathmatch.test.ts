import { describe, expect, it } from "vitest";
import {
  SeededRandom,
  applyDeathmatchWinner,
  applyRoundResult,
  bannerForRound,
  createMatchSeries,
  evaluateDeathmatch,
  selectDeathmatchMap,
} from "../src/index.js";
import { allPoolMaps, makeMap } from "./fixtures.js";

describe("Bo3 match progression", () => {
  it("marks Round 2 as MATCH POINT and Round 3 as TIEBREAKER", () => {
    expect(bannerForRound(1)).toBe("NONE");
    expect(bannerForRound(2)).toBe("MATCH_POINT");
    expect(bannerForRound(3)).toBe("TIEBREAKER");
  });

  it.each([
    [["DRAW", "A"], "A"],
    [["A", "DRAW"], "A"],
    [["B", "DRAW"], "B"],
  ] as const)("ends after Round 2 for %j", (results, winner) => {
    let state = createMatchSeries();
    state = applyRoundResult(state, results[0]).state;
    const decision = applyRoundResult(state, results[1]);
    expect(decision.state.phase).toBe("MATCH_RESULT");
    expect(decision.state.winner).toBe(winner);
  });

  it.each([
    ["A", "B"],
    ["B", "A"],
    ["DRAW", "DRAW"],
  ] as const)("goes to Round 3 when the first two rounds are %s/%s", (round1, round2) => {
    let state = createMatchSeries();
    state = applyRoundResult(state, round1).state;
    const decision = applyRoundResult(state, round2);
    expect(decision.state.phase).toBe("ROUND_REQUIRED");
    expect(decision.nextRoundNumber).toBe(3);
    expect(decision.nextRoundBanner).toBe("TIEBREAKER");
  });

  it("moves a Round 3 draw to deathmatch, never a match draw", () => {
    let state = createMatchSeries();
    state = applyRoundResult(state, "DRAW").state;
    state = applyRoundResult(state, "DRAW").state;
    state = applyRoundResult(state, "DRAW").state;
    expect(state.phase).toBe("DEATHMATCH_REQUIRED");
    expect(state.winner).toBeNull();
    expect(applyDeathmatchWinner(state, "B")).toMatchObject({
      phase: "MATCH_RESULT",
      winner: "B",
    });
  });
});

describe("three-attempt deathmatch", () => {
  const base = makeMap(3, 1, { qualifyingPercent: 50 });
  const map = {
    levelId: base.levelId,
    canonicalLevelId: base.canonicalLevelId,
    alternateLevelId: base.alternateLevelId,
    playableLevelId: base.playableLevelId,
    title: base.title,
    creator: base.creator,
    difficulty: base.difficulty,
    pool: base.pool,
    qualifyingPercent: base.qualifyingPercent,
  };

  it("scores exactly three attempts per player", () => {
    const result = evaluateDeathmatch(
      map,
      [
        { progressPercent: 100, cleared: true },
        { progressPercent: 60, cleared: false },
        { progressPercent: 20, cleared: false },
      ],
      [
        { progressPercent: 80, cleared: false },
        { progressPercent: 70, cleared: false },
        { progressPercent: 60, cleared: false },
      ],
    );
    expect(result).toEqual({ scoreA: 260, scoreB: 285, winner: "B", repeatRequired: false });
  });

  it("requires a repeat on a tie", () => {
    const same = [
      { progressPercent: 60, cleared: false },
      { progressPercent: 0, cleared: false },
      { progressPercent: 0, cleared: false },
    ];
    expect(evaluateDeathmatch(map, same, same)).toMatchObject({
      winner: null,
      repeatRequired: true,
    });
  });

  it("chooses a different map for a repeat when the pool permits", () => {
    const first = selectDeathmatchMap(3, allPoolMaps(), [], new SeededRandom(2));
    const second = selectDeathmatchMap(
      3,
      allPoolMaps(),
      [first.canonicalLevelId],
      new SeededRandom(2),
    );
    expect(second.canonicalLevelId).not.toBe(first.canonicalLevelId);
  });
});
