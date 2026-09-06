import { Injectable } from "@nestjs/common";
import type { PlayerSide } from "@corum-ranked/rules";

export const MATCH_RUNTIME_STATE = Symbol("MATCH_RUNTIME_STATE");

export interface AttemptProgressSnapshot {
  readonly attemptId: string;
  readonly progressPercent: number;
  readonly updatedAtMs: number;
}

export interface AttemptStartIntentSnapshot {
  readonly eventId: string;
  readonly startedAtMs: number;
  readonly observedAtMs: number;
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
    cleared: boolean,
  ): void;
  progress(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
  ): AttemptProgressSnapshot | null;
  displayProgress(matchId: string, roundNumber: number, side: PlayerSide): number | null;
  noteStartIntent(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
    eventId: string,
    startedAtMs: number,
    observedAtMs: number,
  ): void;
  startIntent(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
  ): AttemptStartIntentSnapshot | null;
  clearStartIntent(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
    eventId?: string,
  ): void;
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
  private readonly displayedProgress = new Map<string, number>();
  private readonly startIntents = new Map<string, AttemptStartIntentSnapshot>();

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
    this.displayedProgress.set(keyFor(matchId, roundNumber, side), 0);
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
      this.displayedProgress.set(key, progressPercent);
      return true;
    }
    if (current.progressPercent === progressPercent) {
      // A player can legitimately remain at the same percentage for a while.
      // Treat repeated telemetry as an attempt lease heartbeat even when the
      // visible progress value did not change.
      current.updatedAtMs = atMs;
      return false;
    }
    if (current.progressPercent !== 0 && atMs - current.updatedAtMs < 100) return false;
    current.progressPercent = progressPercent;
    current.updatedAtMs = atMs;
    this.displayedProgress.set(key, progressPercent);
    return true;
  }

  public endAttempt(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
    attemptId: string,
    cleared: boolean,
  ): void {
    const key = keyFor(matchId, roundNumber, side);
    if (this.attempts.get(key)?.attemptId !== attemptId) return;
    this.attempts.delete(key);
    this.displayedProgress.set(key, cleared ? 100 : 0);
  }

  public progress(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
  ): AttemptProgressSnapshot | null {
    const current = this.attempts.get(keyFor(matchId, roundNumber, side));
    return current ? { ...current } : null;
  }

  public displayProgress(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
  ): number | null {
    return this.displayedProgress.get(keyFor(matchId, roundNumber, side)) ?? null;
  }


  public noteStartIntent(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
    eventId: string,
    startedAtMs: number,
    observedAtMs: number,
  ): void {
    this.startIntents.set(keyFor(matchId, roundNumber, side), {
      eventId,
      startedAtMs,
      observedAtMs,
    });
  }

  public startIntent(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
  ): AttemptStartIntentSnapshot | null {
    const intent = this.startIntents.get(keyFor(matchId, roundNumber, side));
    return intent ? { ...intent } : null;
  }

  public clearStartIntent(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
    eventId?: string,
  ): void {
    const key = keyFor(matchId, roundNumber, side);
    const current = this.startIntents.get(key);
    if (!current) return;
    if (!eventId || current.eventId === eventId) this.startIntents.delete(key);
  }

  public clearRound(matchId: string, roundNumber: number): void {
    this.attempts.delete(keyFor(matchId, roundNumber, "A"));
    this.attempts.delete(keyFor(matchId, roundNumber, "B"));
    this.displayedProgress.delete(keyFor(matchId, roundNumber, "A"));
    this.displayedProgress.delete(keyFor(matchId, roundNumber, "B"));
    this.startIntents.delete(keyFor(matchId, roundNumber, "A"));
    this.startIntents.delete(keyFor(matchId, roundNumber, "B"));
  }
}
