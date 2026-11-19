import type { PlayerSide, RoundState } from "@corum-ranked/rules";

export interface DeathFeedEvent {
  readonly id: string;
  readonly side: PlayerSide;
  readonly progressPercent: number;
  readonly endedAtMs: number;
}

/** A restart while alive shows in the death log only from this progress on, so
 * spamming Restart at a map's start cannot flood it (the client uses the same floor). */
export const RESTART_DEATH_MINIMUM_PERCENT = 2;
export const restartShowsInDeathLog = (restarted: boolean, cleared: boolean, progressPercent: number | null): boolean =>
  restarted && !cleared && progressPercent !== null && Number.isFinite(progressPercent) &&
  progressPercent >= RESTART_DEATH_MINIMUM_PERCENT;

// Presentation only: accepted native deaths and restarts from 2%, never Clears,
// quit, practice, rejected End, or attempts invalidated by an already-decided victory.
export const recentRoundDeaths = (state: RoundState, nowMs: number): readonly DeathFeedEvent[] =>
  [...state.attempts.A, ...state.attempts.B]
    .filter(a => a.valid && (a.nativeDeath || a.restartDeath) && !a.cleared && a.endEventId && a.endedAtMs !== null &&
      a.endedAtMs > nowMs - 5_000 && a.endedAtMs <= nowMs)
    .map(a => ({ id: a.endEventId!, side: a.side, progressPercent: a.progressPercent ?? 0, endedAtMs: a.endedAtMs! }))
    .sort((a, b) => a.endedAtMs - b.endedAtMs || a.id.localeCompare(b.id)).slice(-3);
