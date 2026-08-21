import { RankedDomainError } from "./errors.js";
import type { PlayerSide, RoundResult } from "./types.js";

export interface ScoredAttempt {
  readonly progressPercent: number;
  readonly cleared: boolean;
  readonly valid: boolean;
  readonly awardedScore: number;
}

const assertPercentage = (name: string, value: number): void => {
  if (!Number.isFinite(value) || value < 0 || value > 100) {
    throw new RankedDomainError("INVALID_ATTEMPT", `${name} must be from 0 through 100`, { value });
  }
};

export const scoreAttempt = (
  progressPercent: number,
  cleared: boolean,
  qualifyingPercent: number,
): number => {
  assertPercentage("progressPercent", progressPercent);
  assertPercentage("qualifyingPercent", qualifyingPercent);
  if (cleared && progressPercent !== 100) {
    throw new RankedDomainError("INVALID_ATTEMPT", "A clear must report exactly 100% progress", {
      progressPercent,
    });
  }
  if (cleared) return 200;
  if (progressPercent < qualifyingPercent) return 0;

  const wholeProgress = Math.floor(progressPercent);
  return wholeProgress >= 70 ? wholeProgress * 1.5 : wholeProgress;
};

export const totalAttemptScore = (attempts: readonly ScoredAttempt[]): number =>
  attempts.reduce((total, attempt) => total + (attempt.valid ? attempt.awardedScore : 0), 0);

export const compareRoundScores = (
  scoreA: number,
  scoreB: number,
): { readonly result: RoundResult; readonly winner: PlayerSide | null } => {
  if (!Number.isFinite(scoreA) || !Number.isFinite(scoreB)) {
    throw new RankedDomainError("INVALID_ATTEMPT", "Round scores must be finite", {
      scoreA,
      scoreB,
    });
  }
  if (scoreA === scoreB) return { result: "DRAW", winner: null };
  const winner: PlayerSide = scoreA > scoreB ? "A" : "B";
  return { result: winner, winner };
};
