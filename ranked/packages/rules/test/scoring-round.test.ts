import { describe, expect, it } from "vitest";
import {
  DOCUMENT_RULES_V0_4,
  advanceRoundClock,
  compareRoundScores,
  createRound,
  endRoundAttempt,
  scoreAttempt,
  startRoundAttempt,
  startRoundAttemptFromIntent,
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
  it("awards zero below qualifying, integer progress below 70, and x1.5 from 70 through 99", () => {
    expect(scoreAttempt(59.99, false, 60)).toBe(0);
    expect(scoreAttempt(60, false, 60)).toBe(60);
    expect(scoreAttempt(69.99, false, 60)).toBe(69);
    expect(scoreAttempt(70, false, 60)).toBe(105);
    expect(scoreAttempt(79.99, false, 60)).toBe(118.5);
    expect(scoreAttempt(99.99, false, 60)).toBe(148.5);
  });

  it("awards a fixed 200 points for a clear regardless of qualifying", () => {
    expect(scoreAttempt(100, true, 60)).toBe(200);
    expect(scoreAttempt(100, true, 20.1)).toBe(200);
    expect(scoreAttempt(100, true, 20.125)).toBe(200);
  });

  it("does not invent a tiebreaker when scores are equal", () => {
    expect(compareRoundScores(320, 320)).toEqual({ result: "DRAW", winner: null });
  });
});

describe("five minutes plus final attempt start window", () => {
  it("accepts attempts starting at 5:02, 5:05, and 5:09", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_4, START);
    for (const [index, offset] of [302, 305, 309].entries()) {
      const current = start(state, "A", offset, `start-${offset}`);
      state = end(current.state, "A", current.attemptId, offset + 1, 70 + index, false, `end-${offset}`);
    }
    expect(state.attempts.A).toHaveLength(3);
    expect(state.scores.A).toBe(105 + 106.5 + 108);
  });

  it("rejects a new attempt at 5:11", () => {
    const state = createRound(1, snapshot, DOCUMENT_RULES_V0_4, START);
    const decision = startRoundAttempt(state, "A", START + 311_000, "late-start");
    expect(decision.accepted).toBe(false);
    expect(decision.state.phase).toBe("ROUND_RESULT");
  });

  it("restores a pre-deadline visual start after the deadline without ending the active attempt", () => {
    const state = createRound(1, snapshot, DOCUMENT_RULES_V0_4, START);
    const late = startRoundAttemptFromIntent(
      advanceRoundClock(state, START + 309_500),
      "A",
      START + 309_000,
      START + 313_000,
      "intent-start",
    );
    expect(late.accepted).toBe(true);
    expect(late.state.phase).toBe("ROUND_SETTLING");
    expect(late.state.attempts.A[0]?.endedAtMs).toBeNull();

    const ended = endRoundAttempt(
      late.state,
      "A",
      late.attemptId!,
      START + 340_000,
      88,
      false,
      "intent-end",
    );
    expect(ended.accepted).toBe(true);
    expect(ended.state.phase).toBe("ROUND_RESULT");
    expect(ended.state.scores.A).toBe(132);
  });

  it("counts a 5:09 attempt even when it ends at 5:40", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_4, START);
    const current = start(state, "A", 309, "long-start");
    state = advanceRoundClock(current.state, START + 310_000);
    expect(state.phase).toBe("ROUND_SETTLING");
    state = end(state, "A", current.attemptId, 340, 88, false, "long-end");
    expect(state.phase).toBe("ROUND_RESULT");
    expect(state.scores.A).toBe(132);
    expect(state.outcome?.winner).toBe("A");
  });

  it("opens a fresh LAST ATTEMPT window when the third clear finishes after five minutes", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_4, START);
    const first = start(state, "A", 1, "late-clear-a1-start");
    state = end(first.state, "A", first.attemptId, 2, 100, true, "late-clear-a1-end");

    const second = start(state, "A", 3, "late-clear-a2-start");
    state = end(second.state, "A", second.attemptId, 4, 100, true, "late-clear-a2-end");

    const third = start(state, "A", 309, "late-clear-a3-start");
    state = advanceRoundClock(third.state, START + 310_000);
    expect(state.phase).toBe("ROUND_SETTLING");

    state = end(state, "A", third.attemptId, 340, 100, true, "late-clear-a3-end");
    expect(state.phase).toBe("LAST_ATTEMPT_WINDOW");
    expect(state.lastAttemptWindow).toMatchObject({
      triggerSide: "A",
      targetSide: "B",
      startedAtMs: START + 340_000,
      endsAtMs: START + 350_000,
    });

    const target = startRoundAttempt(state, "B", START + 349_000, "late-clear-b-start");
    expect(target.accepted).toBe(true);
  });

  it("settles equal totals as a Draw", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_4, START);
    const a = start(state, "A", 1, "a-start");
    state = a.state;
    const b = start(state, "B", 2, "b-start");
    state = b.state;
    state = end(state, "A", a.attemptId, 3, 75, false, "a-end");
    state = end(state, "B", b.attemptId, 4, 75, false, "b-end");
    state = advanceRoundClock(state, START + 310_000);
    expect(state.outcome?.result).toBe("DRAW");
    expect(state.outcome?.reason).toBe("SCORE");
  });

  it("deduplicates a repeated attempt end event", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_4, START);
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
    expect(repeated.state.scores.A).toBe(120);
  });
});

describe("three-clear and LAST ATTEMPT rules", () => {
  it("opens LAST ATTEMPT when A reaches three clears while B has zero", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_4, START);
    const b = start(state, "B", 1, "b-current");
    state = b.state;
    const a1 = start(state, "A", 2, "a1-start");
    state = end(a1.state, "A", a1.attemptId, 3, 100, true, "a1-end");
    const a2 = start(state, "A", 4, "a2-start");
    state = end(a2.state, "A", a2.attemptId, 5, 100, true, "a2-end");
    const a3 = start(state, "A", 6, "a3-start");
    state = end(a3.state, "A", a3.attemptId, 7, 100, true, "a3-end");
    expect(state.phase).toBe("LAST_ATTEMPT_WINDOW");
    expect(state.lastAttemptWindow).toMatchObject({ targetSide: "B", endsAtMs: START + 17_000 });
    expect(state.attempts.B[0]!.endedAtMs).toBeNull();
  });

  it("lets a zero-clear target reach three clears using starts inside the same 10-second window", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_4, START);
    const a1 = start(state, "A", 1, "a1s");
    state = end(a1.state, "A", a1.attemptId, 2, 100, true, "a1e");
    const a2 = start(state, "A", 3, "a2s");
    state = end(a2.state, "A", a2.attemptId, 4, 100, true, "a2e");
    const a3 = start(state, "A", 5, "a3s");
    state = end(a3.state, "A", a3.attemptId, 6, 100, true, "a3e");
    expect(state.phase).toBe("LAST_ATTEMPT_WINDOW");

    const b1 = start(state, "B", 7, "b1s");
    state = end(b1.state, "B", b1.attemptId, 8, 100, true, "b1e");
    expect(state.phase).toBe("LAST_ATTEMPT_WINDOW");
    expect(state.clears.B).toBe(1);

    const b2 = start(state, "B", 9, "b2s");
    state = end(b2.state, "B", b2.attemptId, 10, 100, true, "b2e");
    expect(state.clears.B).toBe(2);

    const b3 = start(state, "B", 15, "b3s");
    state = end(b3.state, "B", b3.attemptId, 20, 100, true, "b3e");
    expect(state.phase).toBe("ROUND_RESULT");
    expect(state.outcome).toMatchObject({ result: "DRAW", reason: "LAST_ATTEMPT_CLEAR" });
  });

  it("allows the target's current attempt and multiple starts during the 10-second window", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_4, START);
    const b1 = start(state, "B", 1, "b1-start");
    state = end(b1.state, "B", b1.attemptId, 2, 100, true, "b1-end");
    const b2 = start(state, "B", 3, "b2-start");
    state = end(b2.state, "B", b2.attemptId, 4, 100, true, "b2-end");
    const a1 = start(state, "A", 5, "a1-start");
    state = end(a1.state, "A", a1.attemptId, 6, 100, true, "a1-end");
    const a2 = start(state, "A", 7, "a2-start");
    state = end(a2.state, "A", a2.attemptId, 8, 100, true, "a2-end");
    const b3 = start(state, "B", 9, "b3-start");
    state = b3.state;
    const a3 = start(state, "A", 10, "a3-start");
    state = end(a3.state, "A", a3.attemptId, 11, 100, true, "a3-end");
    expect(state.phase).toBe("LAST_ATTEMPT_WINDOW");
    expect(state.lastAttemptWindow).toMatchObject({ targetSide: "B", endsAtMs: START + 21_000 });

    state = end(state, "B", b3.attemptId, 12, 70, false, "b3-end");
    const b4 = start(state, "B", 13, "b4-start");
    state = end(b4.state, "B", b4.attemptId, 14, 70, false, "b4-end");
    const b5 = start(state, "B", 20, "b5-start");
    state = end(b5.state, "B", b5.attemptId, 30, 100, true, "b5-end");

    expect(state.phase).toBe("ROUND_RESULT");
    expect(state.outcome).toMatchObject({ result: "DRAW", reason: "LAST_ATTEMPT_CLEAR" });
  });

  it("awards the round to the trigger when a zero-clear target only reaches two clears", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_4, START);
    const a1 = start(state, "A", 1, "a1s");
    state = end(a1.state, "A", a1.attemptId, 2, 100, true, "a1e");
    const a2 = start(state, "A", 3, "a2s");
    state = end(a2.state, "A", a2.attemptId, 4, 100, true, "a2e");
    const a3 = start(state, "A", 5, "a3s");
    state = end(a3.state, "A", a3.attemptId, 6, 100, true, "a3e");
    const b1 = start(state, "B", 7, "b1s");
    state = end(b1.state, "B", b1.attemptId, 8, 100, true, "b1e");
    const b2 = start(state, "B", 9, "b2s");
    state = end(b2.state, "B", b2.attemptId, 10, 100, true, "b2e");
    state = advanceRoundClock(state, START + 16_000);
    expect(state.outcome).toMatchObject({ winner: "A", reason: "LAST_ATTEMPT_EXPIRED" });
  });

  it("awards the round to the trigger when all LAST ATTEMPT runs fail", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_4, START);
    const b1 = start(state, "B", 1, "b1s");
    state = end(b1.state, "B", b1.attemptId, 2, 100, true, "b1e");
    const a1 = start(state, "A", 3, "a1s");
    state = end(a1.state, "A", a1.attemptId, 4, 100, true, "a1e");
    const a2 = start(state, "A", 5, "a2s");
    state = end(a2.state, "A", a2.attemptId, 6, 100, true, "a2e");
    const a3 = start(state, "A", 7, "a3s");
    state = end(a3.state, "A", a3.attemptId, 8, 100, true, "a3e");
    const b2 = start(state, "B", 17, "b2s");
    state = end(b2.state, "B", b2.attemptId, 20, 80, false, "b2e");
    expect(state.outcome).toMatchObject({ winner: "A", reason: "LAST_ATTEMPT_EXPIRED" });
  });

  it("keeps LAST ATTEMPT authoritative when its window crosses the original five-minute deadline", () => {
    let state = createRound(1, snapshot, DOCUMENT_RULES_V0_4, START);
    const a1 = start(state, "A", 270, "cross-a1-start");
    state = end(a1.state, "A", a1.attemptId, 271, 100, true, "cross-a1-end");
    const a2 = start(state, "A", 280, "cross-a2-start");
    state = end(a2.state, "A", a2.attemptId, 281, 100, true, "cross-a2-end");
    const a3 = start(state, "A", 294, "cross-a3-start");
    state = end(a3.state, "A", a3.attemptId, 295, 100, true, "cross-a3-end");
    expect(state.lastAttemptWindow?.endsAtMs).toBe(START + 305_000);

    const b = start(state, "B", 299, "cross-b-start");
    state = advanceRoundClock(b.state, START + 300_000);
    expect(state.phase).toBe("LAST_ATTEMPT_WINDOW");
    state = advanceRoundClock(state, START + 305_000);
    expect(state.phase).toBe("ROUND_SETTLING");

    state = end(state, "B", b.attemptId, 320, 80, false, "cross-b-end");
    expect(state.phase).toBe("ROUND_RESULT");
    expect(state.outcome).toMatchObject({ winner: "A", reason: "LAST_ATTEMPT_EXPIRED" });
  });
});
