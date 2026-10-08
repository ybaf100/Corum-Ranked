export const MATCH_PAUSE_BUDGET_MS = 30_000;
export interface MatchPauseState {
  usedMs: number;
  leaseId: string;
  startedAtMs: number | null;
  retiredLeaseIds?: string[];
}
export const emptyMatchPause = (): MatchPauseState => ({ usedMs: 0, leaseId: "", startedAtMs: null });
export const matchPauseView = (state: MatchPauseState, nowMs: number) => {
  const elapsed = state.startedAtMs === null ? 0 : Math.max(0, nowMs - state.startedAtMs);
  const remainingMs = Math.max(0, MATCH_PAUSE_BUDGET_MS - state.usedMs - elapsed);
  return { budgetMs: MATCH_PAUSE_BUDGET_MS, remainingMs,
    active: state.startedAtMs !== null && remainingMs > 0, leaseId: state.leaseId };
};
export const changeMatchPause = (state: MatchPauseState, leaseId: string, paused: boolean, nowMs: number): MatchPauseState => {
  // Retain a closed ID: a retried Start ACK cannot reopen its finished pause.
  if (paused && state.leaseId === leaseId) return state;
  if (paused && state.retiredLeaseIds?.includes(leaseId)) return state;
  if (!paused && state.leaseId !== leaseId) return state; // stale End cannot close a newer lease
  const usedMs = Math.min(MATCH_PAUSE_BUDGET_MS, state.usedMs +
    (state.startedAtMs === null ? 0 : Math.max(0, nowMs - state.startedAtMs)));
  const retiredLeaseIds = [...(state.retiredLeaseIds ?? [])];
  if (state.leaseId && state.startedAtMs !== null && !retiredLeaseIds.includes(state.leaseId)) retiredLeaseIds.push(state.leaseId);
  return { usedMs, retiredLeaseIds, leaseId: paused ? leaseId : state.leaseId,
    startedAtMs: paused ? nowMs : null };
};

// A closed lease never forfeits, even when its budget is fully spent. An open
// lease expires by server wall time; Resume after the deadline cannot erase it.
export const expiredMatchPauseSide = (
  states: Partial<Record<'A' | 'B', MatchPauseState>>, nowMs: number,
): 'A' | 'B' | null => {
  let expired: 'A' | 'B' | null = null;
  let earliest = Infinity;
  for (const side of ['A', 'B'] as const) {
    const state = states[side];
    if (!state || state.startedAtMs === null) continue;
    const deadline = state.startedAtMs + Math.max(0, MATCH_PAUSE_BUDGET_MS - state.usedMs);
    if (nowMs >= deadline && deadline < earliest) { expired = side; earliest = deadline; }
  }
  return expired;
};
