import type { RoundState } from "@corum-ranked/rules";

// Keep this deliberately short. It is transport reconciliation, not extra
// gameplay time: a client may only claim a start timestamp that is very close
// to server receipt time, and the timestamp still has to be before the actual
// round/LAST ATTEMPT deadline in the domain rules.
export const ATTEMPT_TRANSPORT_GRACE_MS = 2_000;
export const resolveAttemptStartTime = (
  clientStartedAt: string | undefined,
  now: Date,
): Date => {
  if (!clientStartedAt) return now;
  const parsed = new Date(clientStartedAt);
  if (Number.isNaN(parsed.getTime())) return now;

  const delta = now.getTime() - parsed.getTime();
  if (delta < 0 || delta > ATTEMPT_TRANSPORT_GRACE_MS) return now;
  return parsed;
};

const hasActiveAttempt = (state: RoundState): boolean =>
  (["A", "B"] as const).some((side) =>
    state.attempts[side].some(
      (attempt) => attempt.valid && attempt.endedAtMs === null,
    ),
  );

export const shouldHoldRoundForAttemptTransport = (
  state: RoundState,
  nowMs: number,
): boolean => {
  if (state.phase === "ROUND_RESULT" || hasActiveAttempt(state)) return false;
  const startDeadlineMs = state.lastAttemptWindow?.endsAtMs ?? state.finalWindowEndAtMs;
  return nowMs >= startDeadlineMs && nowMs < startDeadlineMs + ATTEMPT_TRANSPORT_GRACE_MS;
};
