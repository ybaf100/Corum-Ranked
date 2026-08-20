import { describe, expect, it } from "vitest";
import { InMemoryMatchRuntimeState } from "../src/match/match-runtime-state.js";

describe("ephemeral spectator progress state", () => {
  it("keeps only the active attempt in memory and rate limits writes to 10Hz", () => {
    const state = new InMemoryMatchRuntimeState();
    state.beginAttempt("match-1", 2, "B", "B-2", 1_000);

    expect(state.progress("match-1", 2, "B")).toMatchObject({
      attemptId: "B-2",
      progressPercent: 0,
    });
    expect(state.updateProgress("match-1", 2, "B", "B-2", 17, 1_050)).toBe(true);
    expect(state.updateProgress("match-1", 2, "B", "B-2", 31, 1_100)).toBe(false);
    expect(state.updateProgress("match-1", 2, "B", "B-2", 31, 1_150)).toBe(true);
    expect(state.progress("match-1", 2, "B")?.progressPercent).toBe(31);

    state.endAttempt("match-1", 2, "B", "B-2");
    expect(state.progress("match-1", 2, "B")).toBeNull();

    state.beginAttempt("match-1", 2, "B", "B-3", 2_000);
    expect(state.progress("match-1", 2, "B")?.progressPercent).toBe(0);
  });

  it("separates matches, rounds, and player sides", () => {
    const state = new InMemoryMatchRuntimeState();
    state.beginAttempt("match-1", 1, "A", "A-1", 1_000);
    state.beginAttempt("match-1", 2, "A", "A-1", 1_000);
    state.beginAttempt("match-1", 1, "B", "B-1", 1_000);
    state.beginAttempt("match-2", 1, "A", "A-1", 1_000);

    expect(state.updateProgress("match-1", 1, "A", "A-1", 40, 1_100)).toBe(true);
    expect(state.progress("match-1", 1, "A")?.progressPercent).toBe(40);
    expect(state.progress("match-1", 2, "A")?.progressPercent).toBe(0);
    expect(state.progress("match-1", 1, "B")?.progressPercent).toBe(0);
    expect(state.progress("match-2", 1, "A")?.progressPercent).toBe(0);

    state.clearRound("match-1", 1);
    expect(state.progress("match-1", 1, "A")).toBeNull();
    expect(state.progress("match-1", 1, "B")).toBeNull();
    expect(state.progress("match-1", 2, "A")?.progressPercent).toBe(0);
  });
});
