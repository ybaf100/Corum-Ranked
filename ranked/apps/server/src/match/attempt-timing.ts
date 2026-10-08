import type { RoundState } from "@corum-ranked/rules";

// Transport reconciliation is not extra gameplay time. The client still has to
// prove that the visual attempt started before the authoritative start deadline.
// Keep a generous delivery window because free-host / mobile paths can stall for
// several seconds, while the domain deadline itself remains unchanged.
export const ATTEMPT_TRANSPORT_GRACE_MS = 5_000;
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
