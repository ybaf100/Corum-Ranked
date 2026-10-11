import type { PlayerSide, RoundState } from "@corum-ranked/rules";

export interface DeathFeedEvent {
  readonly id: string;
  readonly side: PlayerSide;
  readonly progressPercent: number;
  readonly endedAtMs: number;
}

// Presentation only: accepted native deaths, never Clears, quit, practice,
// rejected End, or attempts invalidated by an already-decided victory.
export const recentRoundDeaths = (state: RoundState, nowMs: number): readonly DeathFeedEvent[] =>
  [...state.attempts.A, ...state.attempts.B]
    .filter(a => a.valid && a.nativeDeath && !a.cleared && a.endEventId && a.endedAtMs !== null &&
      a.endedAtMs > nowMs - 5_000 && a.endedAtMs <= nowMs)
    .map(a => ({ id: a.endEventId!, side: a.side, progressPercent: a.progressPercent ?? 0, endedAtMs: a.endedAtMs! }))
    .sort((a, b) => a.endedAtMs - b.endedAtMs || a.id.localeCompare(b.id)).slice(-3);
