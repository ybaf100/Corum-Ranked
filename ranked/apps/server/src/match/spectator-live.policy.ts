import type { PlayerSide } from "@corum-ranked/rules";

export const SPECTATOR_LIVE_PROTOCOL_VERSION = 1;
export const SPECTATOR_LIVE_BUFFER_MS = 3_000;
export const SPECTATOR_LIVE_MAX_PACKETS = 72;
export const SPECTATOR_LIVE_TIMEOUT_MS = 1_500;
export const SPECTATOR_LIVE_MAX_SEQUENCE_GAP = 8;

export interface SpectatorView {
  readonly active: boolean;
  readonly opponentName?: string;
  readonly currentProgress?: number | null;
}

export interface SpectatorPlayerView {
  readonly gdUsername?: string;
}

export interface SpectatorDeathmatchView {
  readonly sequence: number;
  readonly attemptsCompleted: Readonly<Record<PlayerSide, number>>;
  readonly activeAttemptNumber: Readonly<Record<PlayerSide, number | null>>;
}

export interface SpectatorRoundView {
  readonly roundNumber: number;
}

export interface SpectatorPublicState extends Record<string, unknown> {
  readonly matchId: string;
  readonly state: string;
  readonly side: PlayerSide;
  readonly spectator: SpectatorView;
  readonly opponentPractice: boolean;
  readonly players: Readonly<Record<PlayerSide, SpectatorPlayerView | null>>;
  readonly currentRound: SpectatorRoundView | null;
  readonly deathmatch: SpectatorDeathmatchView | null;
}

export interface SpectatorLiveContext {
  readonly slot: number;
  readonly roundNumber: number;
  readonly deathmatchSequence: number | null;
}

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const playerSide = (value: unknown): PlayerSide | null =>
  value === "A" || value === "B" ? value : null;

const finiteNumber = (value: unknown, fallback = 0): number =>
  typeof value === "number" && Number.isFinite(value) ? value : fallback;

const nullableNumber = (value: unknown): number | null =>
  value === null
    ? null
    : typeof value === "number" && Number.isFinite(value)
      ? value
      : null;

const oppositeSide = (side: PlayerSide): PlayerSide => side === "A" ? "B" : "A";

const parsePlayers = (
  value: unknown,
): Readonly<Record<PlayerSide, SpectatorPlayerView | null>> => {
  if (!isRecord(value)) return { A: null, B: null };
  const parse = (side: PlayerSide): SpectatorPlayerView | null => {
    const item = value[side];
    if (!isRecord(item)) return null;
    return {
      gdUsername: typeof item.gdUsername === "string" ? item.gdUsername : undefined,
    };
  };
  return { A: parse("A"), B: parse("B") };
};

const parseSpectator = (value: unknown): SpectatorView => {
  if (!isRecord(value)) return { active: false };
  const active = value.active === true;
  const opponentName = typeof value.opponentName === "string" ? value.opponentName : undefined;
  const progress = nullableNumber(value.currentProgress);
  return {
    active,
    ...(opponentName ? { opponentName } : {}),
    ...(progress !== null ? { currentProgress: progress } : {}),
  };
};

const parseRound = (value: unknown): SpectatorRoundView | null => {
  if (!isRecord(value)) return null;
  const roundNumber = finiteNumber(value.roundNumber);
  return roundNumber > 0 ? { roundNumber } : null;
};

const parseDeathmatch = (value: unknown): SpectatorDeathmatchView | null => {
  if (!isRecord(value)) return null;
  const sequence = finiteNumber(value.sequence);
  const completed = isRecord(value.attemptsCompleted) ? value.attemptsCompleted : {};
  const active = isRecord(value.activeAttemptNumber) ? value.activeAttemptNumber : {};
  if (sequence <= 0) return null;
  return {
    sequence,
    attemptsCompleted: {
      A: finiteNumber(completed.A),
      B: finiteNumber(completed.B),
    },
    activeAttemptNumber: {
      A: nullableNumber(active.A),
      B: nullableNumber(active.B),
    },
  };
};

/**
 * Preserve MatchService's Standard viewer-relative spectator projection and add
 * the equivalent Death Match projection.  Death Match becomes spectatable only
 * after the viewer has authoritatively completed all three Attempts and has no
 * active Attempt.  The opponent may either be in an active Attempt or still have
 * an unused Attempt budget.  This projection never changes scoring state.
 */
export const decorateSpectatorState = (raw: unknown): SpectatorPublicState => {
  if (!isRecord(raw)) throw new TypeError("Ranked public state must be an object");
  const side = playerSide(raw.side);
  if (!side) throw new TypeError("Ranked public state is missing viewer side");
  const matchId = typeof raw.matchId === "string" ? raw.matchId : "";
  const state = typeof raw.state === "string" ? raw.state : "";
  const players = parsePlayers(raw.players);
  const currentRound = parseRound(raw.currentRound);
  const deathmatch = parseDeathmatch(raw.deathmatch);
  let spectator = parseSpectator(raw.spectator);

  if (state === "DEATHMATCH_PLAYING" && deathmatch) {
    const opponent = oppositeSide(side);
    const ownCompleted = deathmatch.attemptsCompleted[side];
    const ownActive = deathmatch.activeAttemptNumber[side];
    const opponentCompleted = deathmatch.attemptsCompleted[opponent];
    const opponentActive = deathmatch.activeAttemptNumber[opponent];
    const opponentCanStillPlay = opponentActive !== null || opponentCompleted < 3;
    const active = ownCompleted >= 3 && ownActive === null && opponentCanStillPlay;
    spectator = active
      ? {
          active: true,
          opponentName: players[opponent]?.gdUsername ?? "Opponent",
          currentProgress: null,
        }
      : { active: false };
  }

  return {
    ...raw,
    matchId,
    state,
    side,
    spectator,
    opponentPractice: raw.opponentPractice === true,
    players,
    currentRound,
    deathmatch,
  };
};

export const spectatorLiveContext = (
  state: SpectatorPublicState,
): SpectatorLiveContext | null => {
  if (state.state === "DEATHMATCH_PLAYING" && state.deathmatch) {
    return {
      slot: 1_000 + state.deathmatch.sequence,
      roundNumber: state.currentRound?.roundNumber ?? 0,
      deathmatchSequence: state.deathmatch.sequence,
    };
  }
  if (
    state.currentRound &&
    (state.state === "ROUND_PLAYING" ||
      state.state === "FINAL_ATTEMPT_WINDOW" ||
      state.state === "LAST_ATTEMPT_WINDOW" ||
      state.state === "ROUND_SETTLING")
  ) {
    return {
      slot: state.currentRound.roundNumber,
      roundNumber: state.currentRound.roundNumber,
      deathmatchSequence: null,
    };
  }
  return null;
};

export const spectatorOpponentSide = (side: PlayerSide): PlayerSide => oppositeSide(side);
