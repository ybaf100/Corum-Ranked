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
    A: {
      gdAccountId: "1001",
      gdUsername: "alpha",
      displayedTier: "GOLD",
      visibleRankedScore: 1834,
      placementGames: 10,
      placementGamesRequired: 10,
      resultStreak: 2,
      promotion: null,
    },
    B: {
      gdAccountId: "1002",
      gdUsername: "bravo",
      displayedTier: "SILVER",
      visibleRankedScore: 1240,
      placementGames: 10,
      placementGamesRequired: 10,
      resultStreak: -1,
      promotion: null,
    },
  },
  currentRound: {
    roundNumber: 2,
    banner: "ROUND_2",
    map: {
      levelId: "12345",
      canonicalLevelId: "12345",
      alternateLevelId: null,
      playableLevelId: "12345",
      title: "Regression Map",
      creator: "Builder",
      difficulty: "12",
      pool: 3,
      qualifyingPercent: 50,
    },
    scores: { A: 75, B: 30 },
    displayScores: { A: 80, B: 45 },
    clears: { A: 1, B: 0 },
    lastAttemptWindow: null,
  },
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
    A: { gdAccountId: "1001", gdUsername: "alpha", displayedTier: "GOLD", visibleRankedScore: 1834 },
    B: { gdAccountId: "1002", gdUsername: "bravo", displayedTier: "SILVER", visibleRankedScore: 1240 },
  },
  currentRound: null,
  spectator: { active: false },
  deathmatch: {
    sequence: 2,
    map: {
      levelId: "67890",
      canonicalLevelId: "67890",
      alternateLevelId: null,
      playableLevelId: "67890",
      title: "Deathmatch Regression Map",
      creator: "Builder",
      difficulty: "14",
      pool: 4,
      qualifyingPercent: 75,
    },
    scores: { A: 67, B: 72 },
    displayScores: { A: 67, B: 81 },
    clears: { A: 0, B: 0 },
    attemptsUsed: { A: ownCompleted + (ownActive === null ? 0 : 1), B: opponentCompleted + (opponentActive === null ? 0 : 1) },
    attemptsCompleted: { A: ownCompleted, B: opponentCompleted },
    attemptsRemaining: { A: Math.max(0, 3 - ownCompleted), B: Math.max(0, 3 - opponentCompleted) },
    activeAttemptNumber: { A: ownActive, B: opponentActive },
    winnerSide: null,
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

  it("is lossless for player rating/profile and current-round map data", () => {
    const raw = standard(true);
    const state = decorateSpectatorState(raw);
    expect(state.players).toEqual(raw.players);
    expect(state.currentRound).toEqual(raw.currentRound);
    expect(state.players.A?.displayedTier).toBe("GOLD");
    expect(state.players.A?.visibleRankedScore).toBe(1834);
    expect(state.currentRound?.map).toEqual(raw.currentRound.map);
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
    const raw = deathmatch(3, null, 2, 3);
    const state = decorateSpectatorState(raw);
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
    expect(state.deathmatch?.map).toEqual(raw.deathmatch.map);
    expect(state.deathmatch?.scores).toEqual(raw.deathmatch.scores);
  });

  it("allows waiting spectator while the opponent still has an unused Death Match Attempt", () => {
    expect(decorateSpectatorState(deathmatch(3, null, 2, null)).spectator.active).toBe(true);
  });

  it("ends Death Match spectator when both sides exhausted their budgets", () => {
    expect(decorateSpectatorState(deathmatch(3, null, 3, null)).spectator.active).toBe(false);
  });
});
