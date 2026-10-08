import { Injectable } from "@nestjs/common";
import type { PlayerSide } from "@corum-ranked/rules";

export const MATCH_RUNTIME_STATE = Symbol("MATCH_RUNTIME_STATE");

export interface AttemptProgressSnapshot {
  readonly attemptId: string;
  readonly progressPercent: number;
  readonly updatedAtMs: number;
}

export interface EndedAttemptSnapshot { readonly attemptId: string; readonly cleared: boolean; readonly endedAtMs: number; }

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
  endedAttempt?(matchId: string, roundNumber: number, side: PlayerSide): EndedAttemptSnapshot | null;
  displayProgress(matchId: string, roundNumber: number, side: PlayerSide): number | null;
  finished(matchId: string, roundNumber: number, side: PlayerSide): boolean;
  setPractice(matchId: string, roundNumber: number, side: PlayerSide, practice: boolean): void;
  practice(matchId: string, roundNumber: number, side: PlayerSide): boolean;
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
  private readonly endedAttempts = new Map<string, EndedAttemptSnapshot>();
  private readonly finishedAttempts = new Map<string, boolean>();
  private readonly practicePresence = new Map<string, boolean>();
  private readonly startIntents = new Map<string, AttemptStartIntentSnapshot>();

  public beginAttempt(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
    attemptId: string,
    atMs: number,
  ): void {
    const key = keyFor(matchId, roundNumber, side);
    this.attempts.set(key, {
      attemptId,
      progressPercent: 0,
      updatedAtMs: atMs,
    });
    this.displayedProgress.set(key, 0);
    this.endedAttempts.delete(key);
    this.finishedAttempts.set(key, false);
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
      this.endedAttempts.delete(key);
      this.finishedAttempts.set(key, false);
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
    this.endedAttempts.set(key, { attemptId, cleared, endedAtMs: Date.now() });
    this.displayedProgress.set(key, cleared ? 100 : this.displayedProgress.get(key) ?? 0);
    this.finishedAttempts.set(key, true);
  }

  public progress(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
  ): AttemptProgressSnapshot | null {
    const current = this.attempts.get(keyFor(matchId, roundNumber, side));
    return current ? { ...current } : null;
  }

  public endedAttempt(matchId: string, roundNumber: number, side: PlayerSide): EndedAttemptSnapshot | null {
    return this.endedAttempts.get(keyFor(matchId, roundNumber, side)) ?? null;
  }

  public displayProgress(
    matchId: string,
    roundNumber: number,
    side: PlayerSide,
  ): number | null {
    return this.displayedProgress.get(keyFor(matchId, roundNumber, side)) ?? null;
  }

  public finished(matchId: string, roundNumber: number, side: PlayerSide): boolean {
    return this.finishedAttempts.get(keyFor(matchId, roundNumber, side)) ?? false;
  }

  public setPractice(matchId: string, roundNumber: number, side: PlayerSide, practice: boolean): void {
    const key = keyFor(matchId, roundNumber, side);
    if (practice) this.practicePresence.set(key, true);
    else this.practicePresence.delete(key);
  }

  public practice(matchId: string, roundNumber: number, side: PlayerSide): boolean {
    return this.practicePresence.get(keyFor(matchId, roundNumber, side)) ?? false;
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
    for (const side of ["A", "B"] as const) {
      const key = keyFor(matchId, roundNumber, side);
      this.attempts.delete(key);
      this.displayedProgress.delete(key);
      this.finishedAttempts.delete(key);
      this.endedAttempts.delete(key);
      this.practicePresence.delete(key);
      this.startIntents.delete(key);
    }
  }
}
