import { describe, expect, it } from "vitest";
import {
  SeededRandom,
  applyDeathmatchWinner,
  applyRoundResult,
  bannerForRound,
  createMatchSeries,
  evaluateDeathmatch,
  perfectThreeClearWinner,
  selectDeathmatchMap,
} from "../src/index.js";
import { allPoolMaps } from "./fixtures.js";

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

describe("PERFECT WIN classification", () => {
  const threeClear = (result: "A" | "B", clearsA: number, clearsB: number) => ({
    result,
    resultReason: "LAST_ATTEMPT_EXPIRED",
    clearsA,
    clearsB,
  });
  const draw = {
    result: "DRAW" as const,
    resultReason: "LAST_ATTEMPT_CLEAR",
    clearsA: 3,
    clearsB: 3,
  };

  it("accepts two three-Clear wins with no lost round", () => {
    let state = createMatchSeries();
    state = applyRoundResult(state, "A").state;
    state = applyRoundResult(state, "A").state;
    expect(perfectThreeClearWinner(
      state,
      [threeClear("A", 3, 1), threeClear("A", 3, 2)],
      3,
    )).toBe("A");
  });

  it("accepts two draws followed by a three-Clear win", () => {
    let state = createMatchSeries();
    state = applyRoundResult(state, "DRAW").state;
    state = applyRoundResult(state, "DRAW").state;
    state = applyRoundResult(state, "B").state;
    expect(perfectThreeClearWinner(
      state,
      [draw, draw, threeClear("B", 2, 3)],
      3,
    )).toBe("B");
  });

  it("rejects any lost round and any non-three-Clear decisive win", () => {
    let withLoss = createMatchSeries();
    withLoss = applyRoundResult(withLoss, "A").state;
    withLoss = applyRoundResult(withLoss, "B").state;
    withLoss = applyRoundResult(withLoss, "A").state;
    expect(perfectThreeClearWinner(
      withLoss,
      [threeClear("A", 3, 0), threeClear("B", 1, 3), threeClear("A", 3, 2)],
      3,
    )).toBeNull();

    let scoreWin = createMatchSeries();
    scoreWin = applyRoundResult(scoreWin, "A").state;
    scoreWin = applyRoundResult(scoreWin, "A").state;
    expect(perfectThreeClearWinner(scoreWin, [
      { ...threeClear("A", 3, 0), resultReason: "SCORE" },
      threeClear("A", 3, 1),
    ], 3)).toBeNull();
  });

  it("rejects a Death Match winner after three draws", () => {
    let state = createMatchSeries();
    state = applyRoundResult(state, "DRAW").state;
    state = applyRoundResult(state, "DRAW").state;
    state = applyRoundResult(state, "DRAW").state;
    state = applyDeathmatchWinner(state, "A");
    expect(perfectThreeClearWinner(state, [draw, draw, draw], 3)).toBeNull();
  });
});

describe("three-attempt deathmatch", () => {
  it("uses the best raw percentage, never the sum or qualifying score", () => {
    const result = evaluateDeathmatch(
      [
        { progressPercent: 40, cleared: false },
        { progressPercent: 80, cleared: false },
        { progressPercent: 60, cleared: false },
      ],
      [
        { progressPercent: 70, cleared: false },
        { progressPercent: 75, cleared: false },
        { progressPercent: 74, cleared: false },
      ],
    );
    expect(result).toEqual({ scoreA: 80, scoreB: 75, winner: "A", repeatRequired: false });
  });

  it("requires a repeat when the best percentages tie even if totals differ", () => {
    const result = evaluateDeathmatch(
      [
        { progressPercent: 50, cleared: false },
        { progressPercent: 60, cleared: false },
        { progressPercent: 70, cleared: false },
      ],
      [
        { progressPercent: 70, cleared: false },
        { progressPercent: 10, cleared: false },
        { progressPercent: 20, cleared: false },
      ],
    );
    expect(result).toEqual({ scoreA: 70, scoreB: 70, winner: null, repeatRequired: true });
  });

  it("treats a clear as 100 percent and beats repeated 99 percent attempts", () => {
    const result = evaluateDeathmatch(
      [
        { progressPercent: 100, cleared: true },
        { progressPercent: 0, cleared: false },
        { progressPercent: 0, cleared: false },
      ],
      [
        { progressPercent: 99, cleared: false },
        { progressPercent: 99, cleared: false },
        { progressPercent: 99, cleared: false },
      ],
    );
    expect(result).toEqual({ scoreA: 100, scoreB: 99, winner: "A", repeatRequired: false });
  });

  it("requires exactly three attempts per player", () => {
    expect(() => evaluateDeathmatch(
      [{ progressPercent: 100, cleared: true }],
      [
        { progressPercent: 10, cleared: false },
        { progressPercent: 20, cleared: false },
        { progressPercent: 30, cleared: false },
      ],
    )).toThrow(/exactly three attempts/);
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
