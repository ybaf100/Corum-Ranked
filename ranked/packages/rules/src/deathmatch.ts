import { RankedDomainError } from "./errors.js";
import type { PlayerSide } from "./types.js";

export interface DeathmatchAttemptInput {
  readonly progressPercent: number;
  readonly cleared: boolean;
}

export interface DeathmatchResult {
  /** Death Match comparison value: best authoritative percentage across exactly three attempts. */
  readonly scoreA: number;
  readonly scoreB: number;
  readonly winner: PlayerSide | null;
  readonly repeatRequired: boolean;
}

// Shared comparison primitive. Solo keeps its existing raw-percent semantics;
// Survival normalizes its own inputs before calling this function.
export const bestPercentOfThree = (
  attempts: readonly DeathmatchAttemptInput[],
): number => {
  if (attempts.length !== 3) {
    throw new RankedDomainError("INVALID_DEATHMATCH", "Each player must submit exactly three attempts", {
      received: attempts.length,
    });
  }
  return Math.max(
    ...attempts.map((attempt) => attempt.cleared ? 100 : attempt.progressPercent),
  );
};

/**
 * Death Match is intentionally independent from the normal Ranked scoring curve.
 * There is no qualifying percentage and no three-attempt sum: each side is judged
 * only by the best raw authoritative progress percentage from its three attempts.
 */
export const evaluateDeathmatch = (
  attemptsA: readonly DeathmatchAttemptInput[],
  attemptsB: readonly DeathmatchAttemptInput[],
): DeathmatchResult => {
  const scoreA = bestPercentOfThree(attemptsA);
  const scoreB = bestPercentOfThree(attemptsB);
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
