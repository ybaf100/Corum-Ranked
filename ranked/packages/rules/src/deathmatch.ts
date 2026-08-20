import { RankedDomainError } from "./errors.js";
import { scoreAttempt } from "./scoring.js";
import type { PlayerSide, RankedMapSnapshot } from "./types.js";

export interface DeathmatchAttemptInput {
  readonly progressPercent: number;
  readonly cleared: boolean;
}

export interface DeathmatchResult {
  readonly scoreA: number;
  readonly scoreB: number;
  readonly winner: PlayerSide | null;
  readonly repeatRequired: boolean;
}

const scoreThreeAttempts = (
  attempts: readonly DeathmatchAttemptInput[],
  map: RankedMapSnapshot,
): number => {
  if (attempts.length !== 3) {
    throw new RankedDomainError("INVALID_DEATHMATCH", "Each player must submit exactly three attempts", {
      received: attempts.length,
    });
  }
  return attempts.reduce(
    (total, attempt) =>
      total + scoreAttempt(attempt.progressPercent, attempt.cleared, map.qualifyingPercent),
    0,
  );
};

export const evaluateDeathmatch = (
  map: RankedMapSnapshot,
  attemptsA: readonly DeathmatchAttemptInput[],
  attemptsB: readonly DeathmatchAttemptInput[],
): DeathmatchResult => {
  const scoreA = scoreThreeAttempts(attemptsA, map);
  const scoreB = scoreThreeAttempts(attemptsB, map);
  if (scoreA === scoreB) {
    return { scoreA, scoreB, winner: null, repeatRequired: true };
  }
  return {
    scoreA,
    scoreB,
    winner: scoreA > scoreB ? "A" : "B",
    repeatRequired: false,
  };
};
