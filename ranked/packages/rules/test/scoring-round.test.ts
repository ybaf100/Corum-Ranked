import { describe, expect, it } from "vitest";
import {
  DOCUMENT_RULES_V0_3,
  advanceRoundClock,
  compareRoundScores,
  createRound,
  endRoundAttempt,
  scoreAttempt,
  startRoundAttempt,
  type PlayerSide,
  type RoundState,
} from "../src/index.js";
import { makeMap } from "./fixtures.js";

const START = 1_000_000;
const map = {
  ...makeMap(3, 1),
  qualifyingPercent: 60,
};
const snapshot = {
  levelId: map.levelId,
  canonicalLevelId: map.canonicalLevelId,
  alternateLevelId: map.alternateLevelId,
  playableLevelId: map.playableLevelId,
  title: map.title,
  creator: map.creator,
  difficulty: map.difficulty,
  pool: map.pool,
  qualifyingPercent: map.qualifyingPercent,
};

const start = (
  state: RoundState,
  side: PlayerSide,
  offsetSeconds: number,
  event: string,
): { state: RoundState; attemptId: string } => {
  const decision = startRoundAttempt(state, side, START + offsetSeconds * 1_000, event);
  expect(decision.accepted).toBe(true);
  expect(decision.attemptId).not.toBeNull();
  return { state: decision.state, attemptId: decision.attemptId! };
};

const end = (
  state: RoundState,
  side: PlayerSide,
  attemptId: string,
  offsetSeconds: number,
  progress: number,
  cleared: boolean,
  event: string,
): RoundState => {
  const decision = endRoundAttempt(
    state,
    side,
    attemptId,
    START + offsetSeconds * 1_000,
    progress,
    cleared,
    event,
  );
  expect(decision.accepted).toBe(true);
  return decision.state;
};

describe("score race formula", () => {
  it("awards zero below qualifying and integer progress at or above it", () => {
    expect(scoreAttempt(59.99, false, 60)).toBe(0);
    expect(scoreAttempt(60, false, 60)).toBe(60);
    expect(scoreAttempt(79.99, false, 60)).toBe(79);
  });

  it("awards 100 plus qualifying for a clear", () => {
    expect(scoreAttempt(100, true, 60)).toBe(160);
  });

  it("does not invent a tiebreaker when scores are equal", () => {
    expect(compareRoundScores(320, 320)).toEqual({ result: "DRAW", winner: null });
  });
});

describe("three minutes plus final attempt start window", () => {
  it("accepts attempts starting at 3:02, 3:05, and 3:09", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_3, START);
    for (const [index, offset] of [182, 185, 189].entries()) {
      const current = start(state, "A", offset, `start-${offset}`);
      state = end(current.state, "A", current.attemptId, offset + 1, 70 + index, false, `end-${offset}`);
    }
    expect(state.attempts.A).toHaveLength(3);
    expect(state.scores.A).toBe(70 + 71 + 72);
  });

  it("rejects a new attempt at 3:11", () => {
    const state = createRound(1, snapshot, DOCUMENT_RULES_V0_3, START);
    const decision = startRoundAttempt(state, "A", START + 191_000, "late-start");
    expect(decision.accepted).toBe(false);
    expect(decision.state.phase).toBe("ROUND_RESULT");
  });

  it("counts a 3:09 attempt even when it ends at 3:40", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_3, START);
    const current = start(state, "A", 189, "long-start");
    state = advanceRoundClock(current.state, START + 190_000);
    expect(state.phase).toBe("ROUND_SETTLING");
    state = end(state, "A", current.attemptId, 220, 88, false, "long-end");
    expect(state.phase).toBe("ROUND_RESULT");
    expect(state.scores.A).toBe(88);
    expect(state.outcome?.winner).toBe("A");
  });

  it("settles equal totals as a Draw", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_3, START);
    const a = start(state, "A", 1, "a-start");
    state = a.state;
    const b = start(state, "B", 2, "b-start");
    state = b.state;
    state = end(state, "A", a.attemptId, 3, 75, false, "a-end");
    state = end(state, "B", b.attemptId, 4, 75, false, "b-end");
    state = advanceRoundClock(state, START + 190_000);
    expect(state.outcome?.result).toBe("DRAW");
    expect(state.outcome?.reason).toBe("SCORE");
  });

  it("deduplicates a repeated attempt end event", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_3, START);
    const current = start(state, "A", 1, "dedupe-start");
    const first = endRoundAttempt(
      current.state,
      "A",
      current.attemptId,
      START + 2_000,
      80,
      false,
      "dedupe-end",
    );
    const repeated = endRoundAttempt(
      first.state,
      "A",
      current.attemptId,
      START + 3_000,
      99,
      false,
      "dedupe-end",
    );
    expect(repeated.accepted).toBe(true);
    expect(repeated.duplicate).toBe(true);
    expect(repeated.state.scores.A).toBe(80);
  });
});

describe("two-clear and LAST ATTEMPT rules", () => {
  it("ends immediately when A reaches two clears while B has zero", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_3, START);
    const b = start(state, "B", 1, "b-current");
    state = b.state;
    const a1 = start(state, "A", 2, "a1-start");
    state = end(a1.state, "A", a1.attemptId, 3, 100, true, "a1-end");
    const a2 = start(state, "A", 4, "a2-start");
    state = end(a2.state, "A", a2.attemptId, 5, 100, true, "a2-end");
    expect(state.phase).toBe("ROUND_RESULT");
    expect(state.outcome).toMatchObject({ winner: "A", reason: "TWO_CLEAR_ZERO" });
    expect(state.attempts.B[0]!.endedAtMs).toBeNull();
  });

  it("allows the target's current attempt and multiple starts during the 10-second window", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_3, START);
    const b1 = start(state, "B", 1, "b1-start");
    state = end(b1.state, "B", b1.attemptId, 2, 100, true, "b1-end");
    const a1 = start(state, "A", 3, "a1-start");
    state = end(a1.state, "A", a1.attemptId, 4, 100, true, "a1-end");
    const b2 = start(state, "B", 5, "b2-start");
    state = b2.state;
    const a2 = start(state, "A", 6, "a2-start");
    state = end(a2.state, "A", a2.attemptId, 7, 100, true, "a2-end");
    expect(state.phase).toBe("LAST_ATTEMPT_WINDOW");
    expect(state.lastAttemptWindow).toMatchObject({ targetSide: "B", endsAtMs: START + 17_000 });

    state = end(state, "B", b2.attemptId, 8, 70, false, "b2-end");
    const b3 = start(state, "B", 9, "b3-start");
    state = end(b3.state, "B", b3.attemptId, 10, 70, false, "b3-end");
    const b4 = start(state, "B", 16, "b4-start");
    state = end(b4.state, "B", b4.attemptId, 30, 100, true, "b4-end");

    expect(state.phase).toBe("ROUND_RESULT");
    expect(state.outcome).toMatchObject({ result: "DRAW", reason: "LAST_ATTEMPT_CLEAR" });
  });

  it("awards the round to the trigger when all LAST ATTEMPT runs fail", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_3, START);
    const b1 = start(state, "B", 1, "b1s");
    state = end(b1.state, "B", b1.attemptId, 2, 100, true, "b1e");
    const a1 = start(state, "A", 3, "a1s");
    state = end(a1.state, "A", a1.attemptId, 4, 100, true, "a1e");
    const a2 = start(state, "A", 5, "a2s");
    state = end(a2.state, "A", a2.attemptId, 6, 100, true, "a2e");
    const b2 = start(state, "B", 15, "b2s");
    state = end(b2.state, "B", b2.attemptId, 20, 80, false, "b2e");
    expect(state.outcome).toMatchObject({ winner: "A", reason: "LAST_ATTEMPT_EXPIRED" });
  });
});
