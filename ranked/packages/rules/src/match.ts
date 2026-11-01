import { RankedDomainError } from "./errors.js";
import type { PlayerSide, RoundResult } from "./types.js";

export type MatchSeriesPhase = "ROUND_REQUIRED" | "DEATHMATCH_REQUIRED" | "MATCH_RESULT";
/** Per-tier series length from the Ranked Tiers sheet. Missing means BO3. */
export type MatchFormat = "BO3" | "SINGLE";
export type RoundBanner = "NONE" | "MATCH_POINT" | "TIEBREAKER";

export interface MatchSeriesState {
  roundResults: RoundResult[];
  roundWins: Record<PlayerSide, number>;
  phase: MatchSeriesPhase;
  winner: PlayerSide | null;
  /** Absent on series created before per-tier formats: BO3 with a map ban. */
  format?: MatchFormat;
  mapBan?: boolean;
}

export interface MatchSeriesOptions {
  readonly format?: MatchFormat;
  readonly mapBan?: boolean;
}

export const matchFormatOf = (state: Pick<MatchSeriesState, "format">): MatchFormat =>
  state.format === "SINGLE" ? "SINGLE" : "BO3";
export const mapBanOf = (state: Pick<MatchSeriesState, "mapBan">): boolean => state.mapBan !== false;
/** Round maps drawn for a series: one for a single game, three for BO3. */
export const roundMapCountOf = (format: MatchFormat): 1 | 3 => format === "SINGLE" ? 1 : 3;
/** Ban candidates shown: four for a single game, five for BO3. */
export const candidateMapCountOf = (format: MatchFormat): 4 | 5 => format === "SINGLE" ? 4 : 5;
const seriesOptions = (state: MatchSeriesState): Pick<MatchSeriesState, "format" | "mapBan"> => ({
  ...(state.format !== undefined ? { format: state.format } : {}),
  ...(state.mapBan !== undefined ? { mapBan: state.mapBan } : {}),
});

export interface MatchSeriesDecision {
  readonly state: MatchSeriesState;
  readonly nextRoundNumber: 1 | 2 | 3 | null;
  readonly nextRoundBanner: RoundBanner | null;
}

export interface PerfectWinRoundSummary {
  readonly result: RoundResult | null;
  readonly resultReason: string | null;
  readonly clearsA: number;
  readonly clearsB: number;
}

/**
 * A PERFECT WIN is a naturally completed match with no lost rounds and at
 * least one decisive round. Every decisive round must be a real three-Clear
 * victory (expiry, or a decisive three-Clear deadline); draws are allowed. Forced wins, forfeits,
 * score wins, and Death Match wins therefore do not qualify.
 */
export const perfectThreeClearWinner = (
  state: MatchSeriesState,
  rounds: readonly PerfectWinRoundSummary[],
  clearTarget: number,
): PlayerSide | null => {
  if (state.phase !== "MATCH_RESULT" || state.winner === null || clearTarget <= 0) return null;
  if (rounds.length === 0 || rounds.length !== state.roundResults.length) return null;
  const winner = state.winner;
  const loser: PlayerSide = winner === "A" ? "B" : "A";
  let decisiveWins = 0;
  for (const [index, round] of rounds.entries()) {
    if (round.result !== state.roundResults[index]) return null;
    if (round.result === loser || round.result === null) return null;
    if (round.result === "DRAW") continue;
    const winnerClears = winner === "A" ? round.clearsA : round.clearsB;
    const loserClears = loser === "A" ? round.clearsA : round.clearsB;
    if (
      round.result !== winner ||
      !(round.resultReason === "LAST_ATTEMPT_EXPIRED" ||
        (round.resultReason === "CLEAR_TARGET_LAST_ATTEMPT" && loserClears <= 1)) ||
      winnerClears < clearTarget
    ) {
      return null;
    }
    decisiveWins += 1;
  }
  return decisiveWins > 0 ? winner : null;
};

export const bannerForRound = (roundNumber: 1 | 2 | 3): RoundBanner => {
  if (roundNumber === 2) return "MATCH_POINT";
  if (roundNumber === 3) return "TIEBREAKER";
  return "NONE";
};

export const createMatchSeries = (options: MatchSeriesOptions = {}): MatchSeriesState => ({
  roundResults: [],
  roundWins: { A: 0, B: 0 },
  phase: "ROUND_REQUIRED",
  winner: null,
  format: options.format ?? "BO3",
  mapBan: options.mapBan ?? true,
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
    ...seriesOptions(state),
  };
  if (result !== "DRAW") next.roundWins[result] += 1;
  const completedRounds = next.roundResults.length;

  // A single game is decided by its only Round; a draw goes straight to Death Match.
  if (matchFormatOf(state) === "SINGLE") {
    if (result === "DRAW") {
      next.phase = "DEATHMATCH_REQUIRED";
    } else {
      next.phase = "MATCH_RESULT";
      next.winner = result;
    }
    return { state: next, nextRoundNumber: null, nextRoundBanner: null };
  }

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
      "A deathmatch winner is valid only after a deciding-round draw",
    );
  }
  return {
    roundResults: [...state.roundResults],
    roundWins: { ...state.roundWins },
    phase: "MATCH_RESULT",
    winner,
    ...seriesOptions(state),
  };
};
