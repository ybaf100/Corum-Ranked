import {
  BadRequestException,
  ConflictException,
  ForbiddenException,
  Inject,
  Injectable,
} from "@nestjs/common";
import type { PlayerSide } from "@corum-ranked/rules";
import type { RankedSessionContext } from "../session/session.types.js";
import { MatchService } from "./match.service.js";
import {
  MATCH_RUNTIME_STATE,
  type MatchRuntimeStatePort,
} from "./match-runtime-state.js";
import {
  SPECTATOR_LIVE_BUFFER_MS,
  SPECTATOR_LIVE_MAX_PACKETS,
  SPECTATOR_LIVE_MAX_SEQUENCE_GAP,
  SPECTATOR_LIVE_PROTOCOL_VERSION,
  SPECTATOR_LIVE_TIMEOUT_MS,
  decorateSpectatorState,
  spectatorLiveContext,
  spectatorOpponentSide,
  type SpectatorPublicState,
} from "./spectator-live.policy.js";

interface SpectatorInputEdge {
  readonly elapsedMs: number;
  readonly button: number;
  readonly down: boolean;
  readonly player: 1 | 2;
}

export interface SpectatorPoseKeyframe {
  readonly x: number;
  readonly y: number;
  readonly rotation: number;
  readonly dead: boolean;
  readonly finished: boolean;
  readonly velocityX?: number;
  readonly velocityY?: number;
  readonly mode?: string;
  readonly gravity?: number;
  readonly cameraX?: number;
  readonly cameraY?: number;
  readonly dual?: boolean;
  readonly player2X?: number;
  readonly player2Y?: number;
  readonly player2Rotation?: number;
}

export interface SpectatorLivePacket {
  readonly protocolVersion: number;
  readonly matchId: string;
  readonly roundNumber: number;
  readonly deathmatchSequence: number | null;
  readonly attemptId: string;
  readonly generation: number;
  readonly sequence: number;
  readonly elapsedMs: number;
  readonly inputEdges: readonly SpectatorInputEdge[];
  readonly keyframe: SpectatorPoseKeyframe;
}

interface BufferedPacket extends SpectatorLivePacket {
  readonly receivedAtMs: number;
}

interface LiveStream {
  readonly matchId: string;
  readonly slot: number;
  readonly side: PlayerSide;
  readonly attemptId: string;
  generation: number;
  lastSequence: number;
  lastReceivedAtMs: number;
  packets: BufferedPacket[];
}

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const finiteNumber = (value: unknown, field: string): number => {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    throw new BadRequestException(`${field} must be a finite number`);
  }
  return value;
};

const integer = (value: unknown, field: string, minimum: number): number => {
  const parsed = finiteNumber(value, field);
  if (!Number.isSafeInteger(parsed) || parsed < minimum) {
    throw new BadRequestException(`${field} must be an integer >= ${minimum}`);
  }
  return parsed;
};

const optionalFiniteNumber = (value: unknown): number | undefined =>
  typeof value === "number" && Number.isFinite(value) ? value : undefined;

const optionalString = (value: unknown): string | undefined =>
  typeof value === "string" && value.length <= 32 ? value : undefined;

const parseKeyframe = (value: unknown): SpectatorPoseKeyframe => {
  if (!isRecord(value)) throw new BadRequestException("keyframe is required");
  const x = finiteNumber(value.x, "keyframe.x");
  const y = finiteNumber(value.y, "keyframe.y");
  const rotation = finiteNumber(value.rotation, "keyframe.rotation");
  if (Math.abs(x) > 10_000_000 || Math.abs(y) > 10_000_000 || Math.abs(rotation) > 100_000) {
    throw new BadRequestException("keyframe pose is outside the relay safety range");
  }
  const velocityX = optionalFiniteNumber(value.velocityX);
  const velocityY = optionalFiniteNumber(value.velocityY);
  const gravity = optionalFiniteNumber(value.gravity);
  const cameraX = optionalFiniteNumber(value.cameraX);
  const cameraY = optionalFiniteNumber(value.cameraY);
  const player2X = optionalFiniteNumber(value.player2X);
  const player2Y = optionalFiniteNumber(value.player2Y);
  const player2Rotation = optionalFiniteNumber(value.player2Rotation);
  const mode = optionalString(value.mode);
  return {
    x,
    y,
    rotation,
    dead: value.dead === true,
    finished: value.finished === true,
    ...(velocityX === undefined ? {} : { velocityX }),
    ...(velocityY === undefined ? {} : { velocityY }),
    ...(mode === undefined ? {} : { mode }),
    ...(gravity === undefined ? {} : { gravity }),
    ...(cameraX === undefined ? {} : { cameraX }),
    ...(cameraY === undefined ? {} : { cameraY }),
    ...(typeof value.dual === "boolean" ? { dual: value.dual } : {}),
    ...(player2X === undefined ? {} : { player2X }),
    ...(player2Y === undefined ? {} : { player2Y }),
    ...(player2Rotation === undefined ? {} : { player2Rotation }),
  };
};

const parseInputEdges = (value: unknown): readonly SpectatorInputEdge[] => {
  if (value === undefined || value === null) return [];
  if (!Array.isArray(value) || value.length > 8) {
    throw new BadRequestException("inputEdges must contain at most 8 edges");
  }
  return value.map((item, index) => {
    if (!isRecord(item)) throw new BadRequestException(`inputEdges[${index}] is invalid`);
    const player = integer(item.player, `inputEdges[${index}].player`, 1);
    if (player !== 1 && player !== 2) {
      throw new BadRequestException(`inputEdges[${index}].player must be 1 or 2`);
    }
    return {
      elapsedMs: integer(item.elapsedMs, `inputEdges[${index}].elapsedMs`, 0),
      button: integer(item.button, `inputEdges[${index}].button`, 0),
      down: item.down === true,
      player,
    };
  });
};

const parsePacket = (value: unknown, matchId: string): SpectatorLivePacket => {
  if (!isRecord(value)) throw new BadRequestException("spectator packet body is required");
  const bodyMatchId = typeof value.matchId === "string" ? value.matchId : "";
  const attemptId = typeof value.attemptId === "string" ? value.attemptId : "";
  if (!bodyMatchId || bodyMatchId !== matchId) {
    throw new BadRequestException("spectator packet matchId does not match the route");
  }
  if (!attemptId || attemptId.length > 128) {
    throw new BadRequestException("spectator packet attemptId is invalid");
  }
  const deathmatchSequence = value.deathmatchSequence === null || value.deathmatchSequence === undefined
    ? null
    : integer(value.deathmatchSequence, "deathmatchSequence", 1);
  return {
    protocolVersion: integer(value.protocolVersion, "protocolVersion", 1),
    matchId: bodyMatchId,
    roundNumber: integer(value.roundNumber, "roundNumber", 0),
    deathmatchSequence,
    attemptId,
    generation: integer(value.generation, "generation", 1),
    sequence: integer(value.sequence, "sequence", 0),
    elapsedMs: integer(value.elapsedMs, "elapsedMs", 0),
    inputEdges: parseInputEdges(value.inputEdges),
    keyframe: parseKeyframe(value.keyframe),
  };
};

const streamKey = (matchId: string, slot: number, side: PlayerSide): string =>
  `${matchId}:${slot}:${side}`;

const queryInteger = (value: string | undefined): number | null => {
  if (value === undefined || value === "") return null;
  const parsed = Number(value);
  return Number.isSafeInteger(parsed) && parsed >= 0 ? parsed : null;
};

@Injectable()
export class SpectatorLiveService {
  private readonly streams = new Map<string, LiveStream>();

  public constructor(
    private readonly matches: MatchService,
    @Inject(MATCH_RUNTIME_STATE) private readonly runtimeState: MatchRuntimeStatePort,
  ) {}

  public decorateState(raw: unknown): SpectatorPublicState {
    return decorateSpectatorState(raw);
  }

  public async publish(
    matchId: string,
    matchToken: string,
    session: RankedSessionContext,
    body: unknown,
  ) {
    const packet = parsePacket(body, matchId);
    if (packet.protocolVersion !== SPECTATOR_LIVE_PROTOCOL_VERSION) {
      throw new ConflictException("SPECTATOR_PROTOCOL_VERSION_MISMATCH");
    }
    const state = this.decorateState(await this.matches.state(matchId, matchToken, session));
    const context = spectatorLiveContext(state);
    if (!context) throw new ConflictException(`SPECTATOR_PUBLISH_NOT_ACCEPTED_IN_${state.state}`);
    if (packet.roundNumber !== context.roundNumber || packet.deathmatchSequence !== context.deathmatchSequence) {
      throw new ConflictException("SPECTATOR_CONTEXT_MISMATCH");
    }
    if (this.runtimeState.practice(matchId, context.slot, state.side)) {
      this.streams.delete(streamKey(matchId, context.slot, state.side));
      throw new ForbiddenException("PRACTICE_STREAM_SUPPRESSED");
    }
    const authoritative = this.runtimeState.progress(matchId, context.slot, state.side);
    if (!authoritative || authoritative.attemptId !== packet.attemptId) {
      throw new ConflictException("SPECTATOR_ATTEMPT_MISMATCH");
    }

    const key = streamKey(matchId, context.slot, state.side);
    const nowMs = Date.now();
    let stream = this.streams.get(key);
    if (!stream || stream.attemptId !== packet.attemptId) {
      if (packet.sequence !== 0) {
        throw new ConflictException("SPECTATOR_STREAM_MUST_START_AT_SEQUENCE_ZERO");
      }
      stream = {
        matchId,
        slot: context.slot,
        side: state.side,
        attemptId: packet.attemptId,
        generation: packet.generation,
        lastSequence: -1,
        lastReceivedAtMs: nowMs,
        packets: [],
      };
      this.streams.set(key, stream);
    } else if (packet.generation < stream.generation) {
      throw new ConflictException("SPECTATOR_STALE_GENERATION");
    } else if (packet.generation > stream.generation) {
      if (packet.sequence !== 0) {
        throw new ConflictException("SPECTATOR_NEW_GENERATION_MUST_START_AT_SEQUENCE_ZERO");
      }
      stream.generation = packet.generation;
      stream.lastSequence = -1;
      stream.packets = [];
    }

    if (packet.sequence <= stream.lastSequence) {
      throw new ConflictException("SPECTATOR_STALE_SEQUENCE");
    }
    if (
      stream.lastSequence >= 0 &&
      packet.sequence - stream.lastSequence > SPECTATOR_LIVE_MAX_SEQUENCE_GAP
    ) {
      this.streams.delete(key);
      throw new ConflictException("SPECTATOR_SEQUENCE_GAP");
    }

    const buffered: BufferedPacket = { ...packet, receivedAtMs: nowMs };
    stream.lastSequence = packet.sequence;
    stream.lastReceivedAtMs = nowMs;
    stream.packets.push(buffered);
    const cutoff = nowMs - SPECTATOR_LIVE_BUFFER_MS;
    stream.packets = stream.packets.filter((item) => item.receivedAtMs >= cutoff);
    if (stream.packets.length > SPECTATOR_LIVE_MAX_PACKETS) {
      stream.packets.splice(0, stream.packets.length - SPECTATOR_LIVE_MAX_PACKETS);
    }
    return {
      accepted: true,
      protocolVersion: SPECTATOR_LIVE_PROTOCOL_VERSION,
      attemptId: stream.attemptId,
      generation: stream.generation,
      sequence: stream.lastSequence,
    };
  }

  public async subscribe(
    matchId: string,
    matchToken: string,
    session: RankedSessionContext,
    protocolVersionText?: string,
    attemptId?: string,
    generationText?: string,
    afterSequenceText?: string,
  ) {
    const state = this.decorateState(await this.matches.state(matchId, matchToken, session));
    if (!state.spectator.active) {
      this.clearViewerObsoleteStreams(state);
      throw new ForbiddenException("SPECTATOR_NOT_ELIGIBLE");
    }
    const context = spectatorLiveContext(state);
    if (!context) {
      this.clearMatch(matchId);
      return this.fallback("SPECTATOR_CONTEXT_ENDED");
    }
    const requestedVersion = protocolVersionText === undefined
      ? SPECTATOR_LIVE_PROTOCOL_VERSION
      : queryInteger(protocolVersionText);
    if (requestedVersion !== SPECTATOR_LIVE_PROTOCOL_VERSION) {
      return this.fallback("UNSUPPORTED_PROTOCOL_VERSION");
    }

    const opponentSide = spectatorOpponentSide(state.side);
    if (this.runtimeState.practice(matchId, context.slot, opponentSide)) {
      this.streams.delete(streamKey(matchId, context.slot, opponentSide));
      return {
        ...this.fallback("PRACTICE_STREAM_SUPPRESSED"),
        waiting: true,
      };
    }
    const authoritative = this.runtimeState.progress(matchId, context.slot, opponentSide);
    if (!authoritative) {
      return {
        protocolVersion: SPECTATOR_LIVE_PROTOCOL_VERSION,
        active: true,
        waiting: true,
        fallback: false,
        reason: "OPPONENT_ATTEMPT_NOT_ACTIVE",
        bufferWindowMs: SPECTATOR_LIVE_BUFFER_MS,
        maxPackets: SPECTATOR_LIVE_MAX_PACKETS,
        stream: null,
        packets: [],
      };
    }

    const key = streamKey(matchId, context.slot, opponentSide);
    const stream = this.streams.get(key);
    if (!stream || stream.attemptId !== authoritative.attemptId) {
      if (stream) this.streams.delete(key);
      return {
        protocolVersion: SPECTATOR_LIVE_PROTOCOL_VERSION,
        active: true,
        waiting: true,
        fallback: false,
        reason: "STREAM_NOT_READY",
        bufferWindowMs: SPECTATOR_LIVE_BUFFER_MS,
        maxPackets: SPECTATOR_LIVE_MAX_PACKETS,
        stream: null,
        packets: [],
      };
    }
    const nowMs = Date.now();
    if (nowMs - stream.lastReceivedAtMs > SPECTATOR_LIVE_TIMEOUT_MS) {
      this.streams.delete(key);
      return this.fallback("STREAM_TIMEOUT");
    }

    const requestedGeneration = queryInteger(generationText);
    const afterSequence = queryInteger(afterSequenceText);
    const sameAttempt = !attemptId || attemptId === stream.attemptId;
    const sameGeneration = requestedGeneration === null || requestedGeneration === stream.generation;
    const reset = !sameAttempt || !sameGeneration;
    const effectiveAfter = reset ? null : afterSequence;
    const packets = effectiveAfter === null
      ? stream.packets
      : stream.packets.filter((packet) => packet.sequence > effectiveAfter);
    if (
      effectiveAfter !== null &&
      packets.length > 0 &&
      packets[0]!.sequence > effectiveAfter + 1
    ) {
      this.streams.delete(key);
      return this.fallback("SEQUENCE_GAP");
    }

    return {
      protocolVersion: SPECTATOR_LIVE_PROTOCOL_VERSION,
      active: true,
      waiting: false,
      fallback: false,
      reason: null,
      reset,
      bufferWindowMs: SPECTATOR_LIVE_BUFFER_MS,
      maxPackets: SPECTATOR_LIVE_MAX_PACKETS,
      stream: {
        matchId,
        roundNumber: context.roundNumber,
        deathmatchSequence: context.deathmatchSequence,
        attemptId: stream.attemptId,
        generation: stream.generation,
        lastSequence: stream.lastSequence,
        opponentName: state.spectator.opponentName ?? "Opponent",
      },
      packets,
    };
  }

  public clearMatch(matchId: string): void {
    for (const [key, stream] of this.streams) {
      if (stream.matchId === matchId) this.streams.delete(key);
    }
  }

  private clearViewerObsoleteStreams(state: SpectatorPublicState): void {
    if (
      state.state === "ROUND_RESULT" ||
      state.state === "DEATHMATCH_RESULT" ||
      state.state === "MATCH_RESULT" ||
      state.state === "CANCELLED"
    ) {
      this.clearMatch(state.matchId);
    }
  }

  private fallback(reason: string) {
    return {
      protocolVersion: SPECTATOR_LIVE_PROTOCOL_VERSION,
      active: true,
      waiting: false,
      fallback: true,
      reason,
      bufferWindowMs: SPECTATOR_LIVE_BUFFER_MS,
      maxPackets: SPECTATOR_LIVE_MAX_PACKETS,
      stream: null,
      packets: [],
    };
  }
}
