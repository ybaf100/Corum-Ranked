import { describe, expect, it } from "vitest";
import { DOCUMENT_RULES_V0_3, createRound, startRoundAttempt, type RankedMapSnapshot } from "@corum-ranked/rules";
import {
  ATTEMPT_TRANSPORT_GRACE_MS,
  resolveAttemptStartTime,
  shouldHoldRoundForAttemptTransport,
} from "../src/match/attempt-timing.js";

const map: RankedMapSnapshot = {
  levelId: "100",
  canonicalLevelId: "100",
  alternateLevelId: null,
  playableLevelId: "100",
  title: "Timing Test",
  creator: "Corum",
  difficulty: "7",
  pool: 1,
  qualifyingPercent: 20,
};

const rules = DOCUMENT_RULES_V0_3;

describe("attempt transport timing", () => {
  it("accepts only a bounded client start timestamp", () => {
    const now = new Date("2026-08-22T04:00:10.000Z");
    expect(resolveAttemptStartTime("2026-08-22T04:00:08.500Z", now).toISOString())
      .toBe("2026-08-22T04:00:08.500Z");
    expect(resolveAttemptStartTime("2026-08-22T04:00:07.000Z", now).toISOString())
      .toBe("2026-08-22T04:00:07.000Z");
    expect(resolveAttemptStartTime("2026-08-22T04:00:04.000Z", now).toISOString())
      .toBe(now.toISOString());
    expect(resolveAttemptStartTime("2026-08-22T04:00:11.000Z", now).toISOString())
      .toBe(now.toISOString());
  });

  it("holds an otherwise-finished round briefly so a pre-deadline start packet can reconcile", () => {
    const startedAt = Date.parse("2026-08-22T04:00:00.000Z");
    const state = createRound(1, map, rules, startedAt);
    expect(shouldHoldRoundForAttemptTransport(state, state.finalWindowEndAtMs + 500)).toBe(true);
    expect(
      shouldHoldRoundForAttemptTransport(
        state,
        state.finalWindowEndAtMs + ATTEMPT_TRANSPORT_GRACE_MS + 1,
      ),
    ).toBe(false);
  });
  it("accepts a packet received just after the deadline when its bounded timestamp was before it", () => {
    const startedAt = Date.parse("2026-08-22T04:00:00.000Z");
    const state = createRound(1, map, rules, startedAt);
    const receivedAt = new Date(state.finalWindowEndAtMs + 1_000);
    const observedStart = new Date(state.finalWindowEndAtMs - 150).toISOString();
    expect(shouldHoldRoundForAttemptTransport(state, receivedAt.getTime())).toBe(true);
    const trusted = resolveAttemptStartTime(observedStart, receivedAt);
    const decision = startRoundAttempt(
      state,
      "A",
      Math.max(state.lastEvaluatedAtMs, trusted.getTime()),
      "late-but-valid-start",
    );
    expect(decision.accepted).toBe(true);
  });

});
