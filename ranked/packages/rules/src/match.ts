import { RankedDomainError } from "./errors.js";
import type { PlayerSide, RoundResult } from "./types.js";

export type MatchSeriesPhase = "ROUND_REQUIRED" | "DEATHMATCH_REQUIRED" | "MATCH_RESULT";
export type RoundBanner = "NONE" | "MATCH_POINT" | "TIEBREAKER";

export interface MatchSeriesState {
  roundResults: RoundResult[];
  roundWins: Record<PlayerSide, number>;
  phase: MatchSeriesPhase;
  winner: PlayerSide | null;
}

export interface MatchSeriesDecision {
  readonly state: MatchSeriesState;
  readonly nextRoundNumber: 1 | 2 | 3 | null;
  readonly nextRoundBanner: RoundBanner | null;
}

export const bannerForRound = (roundNumber: 1 | 2 | 3): RoundBanner => {
  if (roundNumber === 2) return "MATCH_POINT";
  if (roundNumber === 3) return "TIEBREAKER";
  return "NONE";
};

export const createMatchSeries = (): MatchSeriesState => ({
  roundResults: [],
  roundWins: { A: 0, B: 0 },
  phase: "ROUND_REQUIRED",
  winner: null,
});

export const applyRoundResult = (
  state: MatchSeriesState,
  result: RoundResult,
): MatchSeriesDecision => {
  if (state.phase !== "ROUND_REQUIRED" || state.roundResults.length >= 3) {
    throw new RankedDomainError("INVALID_MATCH_TRANSITION", "The match is not accepting a round result", {
      phase: state.phase,
      completedRounds: state.roundResults.length,
    });
  }

  const next: MatchSeriesState = {
    roundResults: [...state.roundResults, result],
    roundWins: { ...state.roundWins },
    phase: "ROUND_REQUIRED",
    winner: null,
  };
  if (result !== "DRAW") next.roundWins[result] += 1;
  const completedRounds = next.roundResults.length;

  if (completedRounds === 1) {
    return {
      state: next,
      nextRoundNumber: 2,
      nextRoundBanner: "MATCH_POINT",
    };
  }

  if (completedRounds === 2) {
    if (next.roundWins.A !== next.roundWins.B) {
      next.phase = "MATCH_RESULT";
      next.winner = next.roundWins.A > next.roundWins.B ? "A" : "B";
      return { state: next, nextRoundNumber: null, nextRoundBanner: null };
    }
    return {
      state: next,
      nextRoundNumber: 3,
      nextRoundBanner: "TIEBREAKER",
    };
  }

  if (result === "DRAW") {
    next.phase = "DEATHMATCH_REQUIRED";
    return { state: next, nextRoundNumber: null, nextRoundBanner: null };
  }
  next.phase = "MATCH_RESULT";
  next.winner = result;
  return { state: next, nextRoundNumber: null, nextRoundBanner: null };
};

export const applyDeathmatchWinner = (
  state: MatchSeriesState,
  winner: PlayerSide,
): MatchSeriesState => {
  if (state.phase !== "DEATHMATCH_REQUIRED") {
    throw new RankedDomainError(
      "INVALID_MATCH_TRANSITION",
      "A deathmatch winner is valid only after a Round 3 draw",
    );
  }
  return {
    roundResults: [...state.roundResults],
    roundWins: { ...state.roundWins },
    phase: "MATCH_RESULT",
    winner,
  };
};
