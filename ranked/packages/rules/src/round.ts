import { RankedDomainError } from "./errors.js";
import { compareRoundScores, scoreAttempt } from "./scoring.js";
import {
  otherSide,
  type PlayerSide,
  type RankedMapSnapshot,
  type RankedRulesConfig,
  type RoundResult,
} from "./types.js";

export type RoundPhase =
  | "ROUND_PLAYING"
  | "FINAL_ATTEMPT_WINDOW"
  | "LAST_ATTEMPT_WINDOW"
  | "ROUND_SETTLING"
  | "ROUND_RESULT";

export type RoundResultReason =
  | "SCORE"
  | "TWO_CLEAR_LAST_ATTEMPT"
  | "LAST_ATTEMPT_CLEAR"
  | "LAST_ATTEMPT_EXPIRED";

export interface RoundAttempt {
  id: string;
  side: PlayerSide;
  sequence: number;
  startEventId: string;
  serverAcceptedStartAtMs: number;
  endEventId: string | null;
  endedAtMs: number | null;
  progressPercent: number | null;
  cleared: boolean;
  awardedScore: number;
  valid: boolean;
  invalidReason: string | null;
}

export interface LastAttemptWindow {
  triggerSide: PlayerSide;
  targetSide: PlayerSide;
  startedAtMs: number;
  endsAtMs: number;
}

export interface RoundOutcome {
  result: RoundResult;
  winner: PlayerSide | null;
  reason: RoundResultReason;
  settledAtMs: number;
}

export interface RoundState {
  roundNumber: 1 | 2 | 3;
  map: RankedMapSnapshot;
  rules: RankedRulesConfig;
  phase: RoundPhase;
  startedAtMs: number;
  normalEndAtMs: number;
  finalWindowEndAtMs: number;
  lastEvaluatedAtMs: number;
  attempts: Record<PlayerSide, RoundAttempt[]>;
  scores: Record<PlayerSide, number>;
  clears: Record<PlayerSide, number>;
  lastAttemptWindow: LastAttemptWindow | null;
  outcome: RoundOutcome | null;
  stateVersion: number;
}

export interface AttemptStartDecision {
  readonly state: RoundState;
  readonly accepted: boolean;
  readonly duplicate: boolean;
  readonly attemptId: string | null;
  readonly reason: string | null;
}

export interface AttemptEndDecision {
  readonly state: RoundState;
  readonly accepted: boolean;
  readonly duplicate: boolean;
  readonly reason: string | null;
}

const cloneRound = (state: RoundState): RoundState => ({
  ...state,
  attempts: {
    A: state.attempts.A.map((attempt) => ({ ...attempt })),
    B: state.attempts.B.map((attempt) => ({ ...attempt })),
  },
  scores: { ...state.scores },
  clears: { ...state.clears },
  lastAttemptWindow: state.lastAttemptWindow ? { ...state.lastAttemptWindow } : null,
  outcome: state.outcome ? { ...state.outcome } : null,
});

const assertServerTime = (state: RoundState, atMs: number): void => {
  if (!Number.isFinite(atMs) || atMs < state.lastEvaluatedAtMs) {
    throw new RankedDomainError("INVALID_ROUND_TRANSITION", "Server time must be monotonic", {
      atMs,
      lastEvaluatedAtMs: state.lastEvaluatedAtMs,
    });
  }
};

const activeAttempts = (state: RoundState, side?: PlayerSide): RoundAttempt[] => {
  const attempts = side ? state.attempts[side] : [...state.attempts.A, ...state.attempts.B];
  return attempts.filter((attempt) => attempt.valid && attempt.endedAtMs === null);
};

const finishRound = (
  state: RoundState,
  result: RoundResult,
  reason: RoundResultReason,
  atMs: number,
): RoundState => {
  const next = cloneRound(state);
  next.phase = "ROUND_RESULT";
  next.outcome = {
    result,
    winner: result === "DRAW" ? null : result,
    reason,
    settledAtMs: atMs,
  };
  next.lastEvaluatedAtMs = atMs;
  next.stateVersion += 1;
  return next;
};

const finishByScore = (state: RoundState, atMs: number): RoundState => {
  const comparison = compareRoundScores(state.scores.A, state.scores.B);
  return finishRound(state, comparison.result, "SCORE", atMs);
};

export const createRound = (
  roundNumber: 1 | 2 | 3,
  map: RankedMapSnapshot,
  rules: RankedRulesConfig,
  startedAtMs: number,
): RoundState => {
  if (!Number.isFinite(startedAtMs)) {
    throw new RankedDomainError("INVALID_ROUND_TRANSITION", "Round start time must be finite");
  }
  const normalEndAtMs = startedAtMs + rules.roundSeconds * 1_000;
  return {
    roundNumber,
    map: { ...map },
    rules: { ...rules },
    phase: "ROUND_PLAYING",
    startedAtMs,
    normalEndAtMs,
    finalWindowEndAtMs: normalEndAtMs + rules.finalAttemptWindowSeconds * 1_000,
    lastEvaluatedAtMs: startedAtMs,
    attempts: { A: [], B: [] },
    scores: { A: 0, B: 0 },
    clears: { A: 0, B: 0 },
    lastAttemptWindow: null,
    outcome: null,
    stateVersion: 1,
  };
};

export const advanceRoundClock = (state: RoundState, atMs: number): RoundState => {
  assertServerTime(state, atMs);
  if (state.phase === "ROUND_RESULT") return cloneRound(state);

  const next = cloneRound(state);
  next.lastEvaluatedAtMs = atMs;

  if (next.lastAttemptWindow) {
    if (atMs < next.lastAttemptWindow.endsAtMs) {
      next.phase = "LAST_ATTEMPT_WINDOW";
      return next;
    }
    if (activeAttempts(next, next.lastAttemptWindow.targetSide).length > 0) {
      next.phase = "ROUND_SETTLING";
      return next;
    }
    return finishRound(next, next.lastAttemptWindow.triggerSide, "LAST_ATTEMPT_EXPIRED", atMs);
  }

  if (atMs < next.normalEndAtMs) {
    next.phase = "ROUND_PLAYING";
    return next;
  }
  if (atMs < next.finalWindowEndAtMs) {
    next.phase = "FINAL_ATTEMPT_WINDOW";
    return next;
  }
  if (activeAttempts(next).length > 0) {
    next.phase = "ROUND_SETTLING";
    return next;
  }
  return finishByScore(next, atMs);
};

const findStartedEvent = (
  state: RoundState,
  side: PlayerSide,
  eventId: string,
): RoundAttempt | undefined => state.attempts[side].find((attempt) => attempt.startEventId === eventId);

const canStartAttempt = (state: RoundState, side: PlayerSide, atMs: number): string | null => {
  if (state.phase === "ROUND_RESULT") return "ROUND_ALREADY_FINISHED";
  if (atMs < state.startedAtMs) return "ROUND_NOT_STARTED";
  if (activeAttempts(state, side).length > 0) return "ATTEMPT_ALREADY_ACTIVE";
  if (state.lastAttemptWindow) {
    if (side !== state.lastAttemptWindow.targetSide) return "LAST_ATTEMPT_TARGET_ONLY";
    return atMs < state.lastAttemptWindow.endsAtMs ? null : "ATTEMPT_START_WINDOW_CLOSED";
  }
  return atMs < state.finalWindowEndAtMs ? null : "ATTEMPT_START_WINDOW_CLOSED";
};

export const startRoundAttempt = (
  state: RoundState,
  side: PlayerSide,
  serverAcceptedAtMs: number,
  clientEventId: string,
): AttemptStartDecision => {
  if (!clientEventId.trim()) {
    throw new RankedDomainError("INVALID_ATTEMPT", "clientEventId is required");
  }
  const existing = findStartedEvent(state, side, clientEventId);
  if (existing) {
    return {
      state: cloneRound(state),
      accepted: true,
      duplicate: true,
      attemptId: existing.id,
      reason: null,
    };
  }

  const advanced = advanceRoundClock(state, serverAcceptedAtMs);
  const reason = canStartAttempt(advanced, side, serverAcceptedAtMs);
  if (reason) {
    return {
      state: advanced,
      accepted: false,
      duplicate: false,
      attemptId: null,
      reason,
    };
  }

  const next = cloneRound(advanced);
  const sequence = next.attempts[side].length + 1;
  const attemptId = `${side}-${sequence}`;
  next.attempts[side].push({
    id: attemptId,
    side,
    sequence,
    startEventId: clientEventId,
    serverAcceptedStartAtMs: serverAcceptedAtMs,
    endEventId: null,
    endedAtMs: null,
    progressPercent: null,
    cleared: false,
    awardedScore: 0,
    valid: true,
    invalidReason: null,
  });
  next.stateVersion += 1;
  return { state: next, accepted: true, duplicate: false, attemptId, reason: null };
};

const findEndEvent = (
  state: RoundState,
  side: PlayerSide,
  eventId: string,
): RoundAttempt | undefined => state.attempts[side].find((attempt) => attempt.endEventId === eventId);

const enterLastAttemptWindow = (
  state: RoundState,
  triggerSide: PlayerSide,
  atMs: number,
): RoundState => {
  const next = cloneRound(state);
  next.lastAttemptWindow = {
    triggerSide,
    targetSide: otherSide(triggerSide),
    startedAtMs: atMs,
    endsAtMs: atMs + next.rules.lastAttemptWindowSeconds * 1_000,
  };
  next.phase = "LAST_ATTEMPT_WINDOW";
  next.stateVersion += 1;
  return next;
};

export const startRoundAttemptFromIntent = (
  state: RoundState,
  side: PlayerSide,
  observedStartAtMs: number,
  serverNowMs: number,
  clientEventId: string,
): AttemptStartDecision => {
  if (!clientEventId.trim()) {
    throw new RankedDomainError("INVALID_ATTEMPT", "clientEventId is required");
  }
  const existing = findStartedEvent(state, side, clientEventId);
  if (existing) {
    return {
      state: cloneRound(state),
      accepted: true,
      duplicate: true,
      attemptId: existing.id,
      reason: null,
    };
  }
  if (state.phase === "ROUND_RESULT") {
    return {
      state: cloneRound(state),
      accepted: false,
      duplicate: false,
      attemptId: null,
      reason: "ROUND_ALREADY_FINISHED",
    };
  }
  if (!Number.isFinite(observedStartAtMs) || observedStartAtMs < state.startedAtMs) {
    return {
      state: cloneRound(state),
      accepted: false,
      duplicate: false,
      attemptId: null,
      reason: "ROUND_NOT_STARTED",
    };
  }
  if (activeAttempts(state, side).length > 0) {
    return {
      state: cloneRound(state),
      accepted: false,
      duplicate: false,
      attemptId: null,
      reason: "ATTEMPT_ALREADY_ACTIVE",
    };
  }
  if (state.lastAttemptWindow && side !== state.lastAttemptWindow.targetSide) {
    return {
      state: cloneRound(state),
      accepted: false,
      duplicate: false,
      attemptId: null,
      reason: "LAST_ATTEMPT_TARGET_ONLY",
    };
  }
  const deadline = state.lastAttemptWindow?.endsAtMs ?? state.finalWindowEndAtMs;
  if (observedStartAtMs >= deadline) {
    return {
      state: cloneRound(state),
      accepted: false,
      duplicate: false,
      attemptId: null,
      reason: "ATTEMPT_START_WINDOW_CLOSED",
    };
  }

  const next = cloneRound(state);
  const sequence = next.attempts[side].length + 1;
  const attemptId = `${side}-${sequence}`;
  next.attempts[side].push({
    id: attemptId,
    side,
    sequence,
    startEventId: clientEventId,
    serverAcceptedStartAtMs: observedStartAtMs,
    endEventId: null,
    endedAtMs: null,
    progressPercent: null,
    cleared: false,
    awardedScore: 0,
    valid: true,
    invalidReason: null,
  });
  next.stateVersion += 1;

  // The event was observed before the real start deadline, but the serialized
  // HTTP request may reach the server after the deadline. Advance from the
  // current monotonic server clock only after the accepted active attempt has
  // been restored; the round will therefore settle instead of forcibly ending
  // the still-running visual attempt.
  return {
    state: advanceRoundClock(next, Math.max(serverNowMs, next.lastEvaluatedAtMs)),
    accepted: true,
    duplicate: false,
    attemptId,
    reason: null,
  };
};

export const endRoundAttempt = (
  state: RoundState,
  side: PlayerSide,
  attemptId: string,
  serverAcceptedAtMs: number,
  progressPercent: number,
  cleared: boolean,
  clientEventId: string,
): AttemptEndDecision => {
  if (!clientEventId.trim()) {
    throw new RankedDomainError("INVALID_ATTEMPT", "clientEventId is required");
  }
  const duplicate = findEndEvent(state, side, clientEventId);
  if (duplicate) {
    return { state: cloneRound(state), accepted: true, duplicate: true, reason: null };
  }

  const advanced = advanceRoundClock(state, serverAcceptedAtMs);
  if (advanced.phase === "ROUND_RESULT") {
    return {
      state: advanced,
      accepted: false,
      duplicate: false,
      reason: "ROUND_ALREADY_FINISHED",
    };
  }
  const next = cloneRound(advanced);
  const attempt = next.attempts[side].find((candidate) => candidate.id === attemptId);
  if (!attempt || !attempt.valid) {
    return { state: next, accepted: false, duplicate: false, reason: "ATTEMPT_NOT_FOUND" };
  }
  if (attempt.endedAtMs !== null) {
    return { state: next, accepted: false, duplicate: false, reason: "ATTEMPT_ALREADY_ENDED" };
  }
  if (serverAcceptedAtMs < attempt.serverAcceptedStartAtMs) {
    throw new RankedDomainError("INVALID_ATTEMPT", "Attempt cannot end before it starts", {
      attemptId,
    });
  }

  const awardedScore = scoreAttempt(progressPercent, cleared, next.map.qualifyingPercent);
  attempt.endEventId = clientEventId;
  attempt.endedAtMs = serverAcceptedAtMs;
  attempt.progressPercent = progressPercent;
  attempt.cleared = cleared;
  attempt.awardedScore = awardedScore;
  next.scores[side] += awardedScore;
  if (cleared) next.clears[side] += 1;
  next.stateVersion += 1;

  if (cleared && next.lastAttemptWindow?.targetSide === side) {
    // alpha.10: a target that entered LAST ATTEMPT with zero clears does not
    // draw immediately on its first clear. The 10-second *start* window remains
    // open, so another attempt may start before the deadline. Only reaching the
    // trigger side's two clears produces the draw.
    if (next.clears[side] >= next.clears[next.lastAttemptWindow.triggerSide]) {
      return {
        state: finishRound(next, "DRAW", "LAST_ATTEMPT_CLEAR", serverAcceptedAtMs),
        accepted: true,
        duplicate: false,
        reason: null,
      };
    }
    return {
      state: advanceRoundClock(next, serverAcceptedAtMs),
      accepted: true,
      duplicate: false,
      reason: null,
    };
  }

  if (cleared && next.clears[side] === 2 && !next.lastAttemptWindow) {
    const opponentClears = next.clears[otherSide(side)];
    if (opponentClears <= 1) {
      return {
        state: enterLastAttemptWindow(next, side, serverAcceptedAtMs),
        accepted: true,
        duplicate: false,
        reason: null,
      };
    }
  }

  return {
    state: advanceRoundClock(next, serverAcceptedAtMs),
    accepted: true,
    duplicate: false,
    reason: null,
  };
};

export const invalidateRoundAttempt = (
  state: RoundState,
  side: PlayerSide,
  attemptId: string,
  serverAcceptedAtMs: number,
  invalidReason: string,
): RoundState => {
  if (!invalidReason.trim()) {
    throw new RankedDomainError("INVALID_ATTEMPT", "An invalidation reason is required");
  }
  const advanced = advanceRoundClock(state, serverAcceptedAtMs);
  const next = cloneRound(advanced);
  const attempt = next.attempts[side].find((candidate) => candidate.id === attemptId);
  if (!attempt || attempt.endedAtMs !== null) {
    throw new RankedDomainError("INVALID_ATTEMPT", "Only an active attempt can be invalidated", {
      attemptId,
    });
  }
  attempt.valid = false;
  attempt.invalidReason = invalidReason;
  attempt.endedAtMs = serverAcceptedAtMs;
  next.stateVersion += 1;
  return advanceRoundClock(next, serverAcceptedAtMs);
};

export const roundDeadlineAtMs = (state: RoundState): number | null => {
  if (state.phase === "ROUND_RESULT" || state.phase === "ROUND_SETTLING") return null;
  if (state.lastAttemptWindow) return state.lastAttemptWindow.endsAtMs;
  if (state.phase === "ROUND_PLAYING") return state.normalEndAtMs;
  return state.finalWindowEndAtMs;
};
