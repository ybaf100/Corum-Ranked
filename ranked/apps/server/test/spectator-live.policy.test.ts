import { describe, expect, it } from "vitest";
import {
  decorateSpectatorState,
  spectatorLiveContext,
} from "../src/match/spectator-live.policy.js";

const standard = (spectatorActive: boolean, state = "ROUND_SETTLING") => ({
  matchId: "match-1",
  state,
  side: "A",
  opponentPractice: false,
  players: {
    A: { gdUsername: "alpha" },
    B: { gdUsername: "bravo" },
  },
  currentRound: { roundNumber: 2 },
  deathmatch: null,
  spectator: spectatorActive
    ? { active: true, opponentName: "bravo", currentProgress: 71 }
    : { active: false },
});

const deathmatch = (
  ownCompleted: number,
  ownActive: number | null,
  opponentCompleted: number,
  opponentActive: number | null,
) => ({
  matchId: "match-dm",
  state: "DEATHMATCH_PLAYING",
  side: "A",
  opponentPractice: false,
  players: {
    A: { gdUsername: "alpha" },
    B: { gdUsername: "bravo" },
  },
  currentRound: null,
  spectator: { active: false },
  deathmatch: {
    sequence: 2,
    attemptsCompleted: { A: ownCompleted, B: opponentCompleted },
    activeAttemptNumber: { A: ownActive, B: opponentActive },
  },
});

describe("spectator live viewer eligibility", () => {
  it("keeps retryable Standard death, pending-start, and active-attempt projections non-spectating", () => {
    for (const state of ["ROUND_PLAYING", "FINAL_ATTEMPT_WINDOW", "LAST_ATTEMPT_WINDOW"]) {
      expect(decorateSpectatorState(standard(false, state)).spectator.active).toBe(false);
    }
  });

  it("preserves MatchService's terminal Standard spectator projection", () => {
    const state = decorateSpectatorState(standard(true));
    expect(state.spectator).toEqual({
      active: true,
      opponentName: "bravo",
      currentProgress: 71,
    });
    expect(spectatorLiveContext(state)).toEqual({
      slot: 2,
      roundNumber: 2,
      deathmatchSequence: null,
    });
  });

  it("keeps the LAST ATTEMPT target side playable rather than spectating", () => {
    const target = { ...standard(false, "LAST_ATTEMPT_WINDOW"), side: "B" };
    expect(decorateSpectatorState(target).spectator.active).toBe(false);
  });

  it("rejects a malformed viewer side instead of trusting client-relative identity", () => {
    expect(() => decorateSpectatorState({ ...standard(false), side: "C" })).toThrow(
      "Ranked public state is missing viewer side",
    );
  });

  it("does not enable Death Match spectator after Attempt 1 or 2", () => {
    expect(decorateSpectatorState(deathmatch(1, null, 1, null)).spectator.active).toBe(false);
    expect(decorateSpectatorState(deathmatch(2, null, 2, null)).spectator.active).toBe(false);
  });

  it("does not enable Death Match spectator while the third Attempt is active", () => {
    expect(decorateSpectatorState(deathmatch(2, 3, 2, 3)).spectator.active).toBe(false);
  });

  it("enables Death Match spectator only after three completed Attempts when opponent is active", () => {
    const state = decorateSpectatorState(deathmatch(3, null, 2, 3));
    expect(state.spectator).toEqual({
      active: true,
      opponentName: "bravo",
      currentProgress: null,
    });
    expect(spectatorLiveContext(state)).toEqual({
      slot: 1002,
      roundNumber: 0,
      deathmatchSequence: 2,
    });
  });

  it("allows waiting spectator while the opponent still has an unused Death Match Attempt", () => {
    expect(decorateSpectatorState(deathmatch(3, null, 2, null)).spectator.active).toBe(true);
  });

  it("ends Death Match spectator when both sides exhausted their budgets", () => {
    expect(decorateSpectatorState(deathmatch(3, null, 3, null)).spectator.active).toBe(false);
  });
});
