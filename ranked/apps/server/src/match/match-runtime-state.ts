import { Injectable } from "@nestjs/common";
import type { PlayerSide } from "@corum-ranked/rules";

export const MATCH_RUNTIME_STATE = Symbol("MATCH_RUNTIME_STATE");

export interface AttemptProgressSnapshot {
  readonly attemptId: string;
  readonly progressPercent: number;
  readonly updatedAtMs: number;
}

export interface MatchRuntimeStatePort {
  beginAttempt(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
    attemptId: string,
    atMs: number,
  ): void;
  updateProgress(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
    attemptId: string,
    progressPercent: number,
    atMs: number,
  ): boolean;
  endAttempt(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
    attemptId: string,
  ): void;
  progress(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
  ): AttemptProgressSnapshot | null;
  clearRound(matchId: string, roundNumber: number): void;
}

interface MutableAttemptProgress {
  attemptId: string;
  progressPercent: number;
  updatedAtMs: number;
}

const keyFor = (matchId: string, roundNumber: number, side: PlayerSide): string =>
  `${matchId}:${roundNumber}:${side}`;

@Injectable()
export class InMemoryMatchRuntimeState implements MatchRuntimeStatePort {
  private readonly attempts = new Map<string, MutableAttemptProgress>();

  public beginAttempt(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
    attemptId: string,
    atMs: number,
  ): void {
    this.attempts.set(keyFor(matchId, roundNumber, side), {
      attemptId,
      progressPercent: 0,
      updatedAtMs: atMs,
    });
  }

  public updateProgress(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
    attemptId: string,
    progressPercent: number,
    atMs: number,
  ): boolean {
    const key = keyFor(matchId, roundNumber, side);
    const current = this.attempts.get(key);
    if (!current || current.attemptId !== attemptId) {
      this.attempts.set(key, { attemptId, progressPercent, updatedAtMs: atMs });
      return true;
    }
    if (current.progressPercent === progressPercent) return false;
    if (current.progressPercent !== 0 && atMs - current.updatedAtMs < 100) return false;
    current.progressPercent = progressPercent;
    current.updatedAtMs = atMs;
    return true;
  }

  public endAttempt(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
    attemptId: string,
  ): void {
    const key = keyFor(matchId, roundNumber, side);
    if (this.attempts.get(key)?.attemptId === attemptId) this.attempts.delete(key);
  }

  public progress(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
  ): AttemptProgressSnapshot | null {
    const current = this.attempts.get(keyFor(matchId, roundNumber, side));
    return current ? { ...current } : null;
  }

  public clearRound(matchId: string, roundNumber: number): void {
    this.attempts.delete(keyFor(matchId, roundNumber, "A"));
    this.attempts.delete(keyFor(matchId, roundNumber, "B"));
  }
}
