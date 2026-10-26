import {
  BadRequestException,
  ConflictException,
  ForbiddenException,
  Inject,
  Injectable,
  Optional,
} from "@nestjs/common";
import { DATABASE, type DatabasePort } from "../database/database.port.js";
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
  SPECTATOR_HISTORY_MAX_PACKETS,
  SPECTATOR_HISTORY_PAGE_PACKETS,
  SPECTATOR_MAX_STREAMS,
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

export const SPECTATOR_GAME_MODES = ["CUBE", "SHIP", "BALL", "UFO", "WAVE", "ROBOT", "SPIDER", "SWING"] as const;
export type SpectatorGameMode = typeof SPECTATOR_GAME_MODES[number];
export interface SpectatorSnapEvidence {
  readonly type: "TELEPORT" | "SPIDER_SNAP";
  readonly fromX: number; readonly fromY: number; readonly toX: number; readonly toY: number;
  readonly cameraFromX?: number; readonly cameraFromY?: number;
}

export interface SpectatorPoseKeyframe {
  readonly x: number;
  readonly y: number;
  readonly rotation: number;
  readonly dead: boolean;
  readonly finished: boolean;
  readonly velocityX?: number;
  readonly velocityY?: number;
  readonly mode: SpectatorGameMode;
  readonly mini: boolean;
  readonly upsideDown: boolean;
  readonly flipped: boolean;
  readonly gravity?: number;
  readonly cameraX?: number;
  readonly cameraY?: number;
  readonly cameraSnap?: boolean;
  readonly cameraZoom?: number;
  readonly cameraAngle?: number;
  readonly cameraLayers?: readonly {readonly present:boolean;readonly scaleX:number;readonly scaleY:number;readonly rotation:number}[];
  readonly discontinuity?: SpectatorSnapEvidence;
  readonly visible?: boolean;
  readonly dual: boolean;
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
  readonly levelId?: number;
  readonly inputEdges: readonly SpectatorInputEdge[];
  readonly keyframe: SpectatorPoseKeyframe;
}

interface BufferedPacket extends SpectatorLivePacket {
  readonly receivedAtMs: number;
}

export interface SpectatorLivePublishResponse {
  readonly accepted: true;
  readonly protocolVersion: number;
  readonly attemptId: string;
  readonly generation: number;
  readonly sequence: number;
}

export interface SpectatorLiveStreamDescriptor {
  readonly matchId: string;
  readonly roundNumber: number;
  readonly deathmatchSequence: number | null;
  readonly attemptId: string;
  readonly generation: number;
  readonly lastSequence: number;
  readonly opponentName: string;
}

export interface SpectatorLiveSubscribeResponse {
  readonly protocolVersion: number;
  readonly active: boolean;
  readonly waiting: boolean;
  readonly fallback: boolean;
  readonly reason: string | null;
  readonly reset?: boolean;
  readonly paused?: boolean;
  readonly bufferWindowMs: number;
  readonly maxPackets: number;
  readonly stream: SpectatorLiveStreamDescriptor | null;
  readonly packets: readonly SpectatorLivePacket[];
  readonly restoration?: {readonly available:boolean;readonly reason:string|null;readonly caughtUp:boolean;readonly headSequence:number;readonly startSequence:number;readonly levelId:number|null};
}

interface LiveStream {
  readonly matchId: string;
  readonly slot: number;
  readonly side: PlayerSide;
  readonly attemptId: string;
  generation: number;
  lastSequence: number;
  lastReceivedAtMs: number;
  pausedAtMs?: number | null;
  packets: BufferedPacket[];
  history?: BufferedPacket[];
  historyContinuous?: boolean;
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

const requiredBoolean = (value: unknown, field: string): boolean => {
  if (typeof value !== "boolean") throw new BadRequestException(`${field} must be a boolean`);
  return value;
};

export const parseSpectatorKeyframe = (value: unknown): SpectatorPoseKeyframe => {
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
  const validCamera = cameraX !== undefined && cameraY !== undefined &&
    Math.abs(cameraX) <= 10_000_000 && Math.abs(cameraY) <= 10_000_000;
  let cameraTransform: Pick<SpectatorPoseKeyframe,"cameraZoom"|"cameraAngle"|"cameraLayers"> = {};
  if(value.cameraZoom!==undefined || value.cameraAngle!==undefined || value.cameraLayers!==undefined){
    const zoom=finiteNumber(value.cameraZoom,"cameraZoom"),angle=finiteNumber(value.cameraAngle,"cameraAngle");
    if(!validCamera || zoom<.01 || zoom>100 || Math.abs(angle)>100000 || !Array.isArray(value.cameraLayers) || value.cameraLayers.length!==3)
      throw new BadRequestException("Invalid camera transform");
    const layers=value.cameraLayers.map((layer:unknown)=>{
      if(!isRecord(layer))throw new BadRequestException("Invalid camera layer");
      const present=requiredBoolean(layer.present,"camera layer present");
      const scaleX=finiteNumber(layer.scaleX,"camera scaleX"),scaleY=finiteNumber(layer.scaleY,"camera scaleY"),rotation=finiteNumber(layer.rotation,"camera rotation");
      if((present && (Math.abs(scaleX)<.001 || Math.abs(scaleY)<.001)) || Math.abs(scaleX)>100 || Math.abs(scaleY)>100 || Math.abs(rotation)>100000)
        throw new BadRequestException("Camera layer outside bounds");
      return {present,scaleX,scaleY,rotation};
    });
    if(!layers[0]!.present)throw new BadRequestException("Camera world layer missing");
    cameraTransform={cameraZoom:zoom,cameraAngle:angle,cameraLayers:layers};
  }
  let discontinuity: SpectatorSnapEvidence | undefined;
  if (value.discontinuity !== undefined) {
    const snap = value.discontinuity;
    if (!isRecord(snap) || (snap.type !== "TELEPORT" && snap.type !== "SPIDER_SNAP")) {
      throw new BadRequestException("keyframe.discontinuity requires a supported native event");
    }
    const fromX = finiteNumber(snap.fromX, "discontinuity.fromX"), fromY = finiteNumber(snap.fromY, "discontinuity.fromY");
    const toX = finiteNumber(snap.toX, "discontinuity.toX"), toY = finiteNumber(snap.toY, "discontinuity.toY");
    const dx = toX - fromX, dy = toY - fromY, distance = Math.hypot(dx, dy);
    if ([fromX, fromY, toX, toY].some(v => Math.abs(v) > 10_000_000) || distance === 0 ||
      (snap.type === "TELEPORT" ? distance > 100_000 : Math.abs(dx) > 400 || Math.abs(dy) > 2400)) {
      throw new BadRequestException("keyframe.discontinuity is outside native event bounds");
    }
    const cameraFromX = optionalFiniteNumber(snap.cameraFromX), cameraFromY = optionalFiniteNumber(snap.cameraFromY);
    const sourceCamera = cameraFromX !== undefined && cameraFromY !== undefined &&
      Math.abs(cameraFromX) <= 10_000_000 && Math.abs(cameraFromY) <= 10_000_000;
    discontinuity = { type: snap.type, fromX, fromY, toX, toY,
      ...(sourceCamera ? { cameraFromX, cameraFromY } : {}) };
  }
  const player2X = optionalFiniteNumber(value.player2X);
  const player2Y = optionalFiniteNumber(value.player2Y);
  const player2Rotation = optionalFiniteNumber(value.player2Rotation);
  if (typeof value.mode !== "string" || !SPECTATOR_GAME_MODES.includes(value.mode as SpectatorGameMode)) {
    throw new BadRequestException("keyframe.mode is unsupported");
  }
  const mode = value.mode as SpectatorGameMode;
  const mini = requiredBoolean(value.mini, "keyframe.mini");
  const upsideDown = requiredBoolean(value.upsideDown, "keyframe.upsideDown");
  const flipped = requiredBoolean(value.flipped, "keyframe.flipped");
  const dual = requiredBoolean(value.dual, "keyframe.dual");
  const dead = requiredBoolean(value.dead, "keyframe.dead");
  const finished = requiredBoolean(value.finished, "keyframe.finished");
  if (dead && finished) throw new BadRequestException("keyframe terminal flags conflict");
  return {
    x,
    y,
    rotation,
    mode, mini, upsideDown, flipped, dual, dead, finished,
    ...(velocityX === undefined ? {} : { velocityX }),
    ...(velocityY === undefined ? {} : { velocityY }),
    ...(gravity === undefined ? {} : { gravity }),
    ...(validCamera ? { cameraX, cameraY, cameraSnap: value.cameraSnap === true } : {}),
    ...cameraTransform,
    ...(discontinuity === undefined ? {} : { discontinuity }),
    ...(value.visible === undefined ? {} : { visible: requiredBoolean(value.visible, "keyframe.visible") }),
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
    ...(value.levelId===undefined?{}:{levelId:integer(value.levelId,"levelId",1)}),
    inputEdges: parseInputEdges(value.inputEdges),
    keyframe: parseSpectatorKeyframe(value.keyframe),
  };
};

const streamKey = (matchId: string, slot: number, side: PlayerSide): string =>
  `${matchId}:${slot}:${side}`;

const publicPacket = ({ receivedAtMs: _receivedAtMs, ...packet }: BufferedPacket): SpectatorLivePacket =>
  packet;

const queryInteger = (value: string | undefined): number | null => {
  if (value === undefined || value === "") return null;
  const parsed = Number(value);
  return Number.isSafeInteger(parsed) && parsed >= 0 ? parsed : null;
};

@Injectable()
export class SpectatorLiveService {
  private readonly streams = new Map<string, LiveStream>();
  private liveEnabled = true;
  private liveSettingVersion = 0;

  public constructor(
    private readonly matches: MatchService,
    @Inject(MATCH_RUNTIME_STATE) private readonly runtimeState: MatchRuntimeStatePort,
    @Optional() @Inject(DATABASE) private readonly database?: DatabasePort,
  ) {}

  public decorateState(raw: unknown): SpectatorPublicState {
    const state = decorateSpectatorState(raw);
    state.spectator = { ...state.spectator, liveEnabled: this.liveEnabled, liveSettingVersion: this.liveSettingVersion };
    this.reconcileStreams(state);
    return state;
  }

  public notePublisherPause(matchId: string, side: PlayerSide, paused: boolean): void {
    const now = Date.now();
    for (const stream of this.streams.values()) {
      if (stream.matchId !== matchId || stream.side !== side) continue;
      if (paused) stream.pausedAtMs ??= now;
      else if (stream.pausedAtMs != null) { stream.pausedAtMs = null; stream.lastReceivedAtMs = now; }
    }
  }

  public async getLiveEnabled(): Promise<boolean> {
    if (this.database) {
      const result = await this.database.query<{ enabled: boolean; revision: number | string }>(
        "SELECT enabled, revision FROM ranked_runtime_features WHERE feature = 'live_spectator'",
      );
      this.liveEnabled = result.rows[0]?.enabled ?? true;
      this.liveSettingVersion = Number(result.rows[0]?.revision ?? 0);
    }
    if (!this.liveEnabled) {
      for (const [key] of this.streams) if (!key.startsWith("recorded:")) this.streams.delete(key);
    }
    return this.liveEnabled;
  }

  public async setLiveEnabled(enabled: boolean): Promise<boolean> {
    if (!this.database) throw new Error("Persistent spectator settings unavailable");
    await this.database.query(`INSERT INTO ranked_runtime_features (feature, enabled)
      VALUES ('live_spectator', $1) ON CONFLICT (feature) DO UPDATE SET enabled = EXCLUDED.enabled,
      revision = ranked_runtime_features.revision + CASE WHEN ranked_runtime_features.enabled <> EXCLUDED.enabled THEN 1 ELSE 0 END`, [enabled]);
    this.liveEnabled = enabled;
    return this.getLiveEnabled();
  }

  public async decorateConfiguredState(raw: unknown): Promise<SpectatorPublicState> {
    await this.getLiveEnabled();
    return this.decorateState(raw);
  }

  public async publish(
    matchId: string,
    matchToken: string,
    session: RankedSessionContext,
    body: unknown,
  ): Promise<SpectatorLivePublishResponse> {
    const batched = isRecord(body) && Array.isArray(body.packets);
    const values = batched ? (body as { packets: unknown[] }).packets : [body];
    if (values.length < 1 || values.length > 16) throw new BadRequestException("SPECTATOR_BATCH_SIZE");
    const packets = values.map(value => parsePacket(value, matchId));
    const packet = packets[0]!;
    for (let i = 0; i < packets.length; ++i) {
      const item = packets[i]!;
      if (item.protocolVersion !== SPECTATOR_LIVE_PROTOCOL_VERSION) throw new ConflictException("SPECTATOR_PROTOCOL_VERSION_MISMATCH");
      if (item.attemptId !== packet.attemptId || item.generation !== packet.generation ||
          item.roundNumber !== packet.roundNumber || item.deathmatchSequence !== packet.deathmatchSequence) {
        throw new BadRequestException("SPECTATOR_BATCH_CONTEXT_MISMATCH");
      }
      const previous = packets[i - 1];
      if (previous && (item.sequence !== previous.sequence + 1 || item.elapsedMs < previous.elapsedMs ||
          previous.keyframe.dead || previous.keyframe.finished)) throw new BadRequestException("SPECTATOR_BATCH_ORDER");
    }
    const state = await this.decorateConfiguredState(await this.matches.state(matchId, matchToken, session));
    if (!this.liveEnabled) throw new ForbiddenException("LIVE_SPECTATOR_DISABLED");
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
    const ended = this.runtimeState.endedAttempt?.(matchId, context.slot, state.side);
    // A scene-independent ordered upload can arrive after Attempt End ACK. Only
    // this exact recently ended authoritative Attempt may drain its presentation
    // queue; these packets never enter the scoring/persistence path.
    const recentlyEnded = ended?.attemptId === packet.attemptId &&
      Date.now() - ended.endedAtMs <= SPECTATOR_LIVE_TIMEOUT_MS;
    if ((!authoritative || authoritative.attemptId !== packet.attemptId) && !recentlyEnded) {
      throw new ConflictException("SPECTATOR_ATTEMPT_MISMATCH");
    }
    const terminal = packets.at(-1)!;
    if (recentlyEnded && (terminal.keyframe.dead || terminal.keyframe.finished) &&
        (ended!.cleared ? !terminal.keyframe.finished : !terminal.keyframe.dead)) {
      throw new ConflictException("SPECTATOR_ATTEMPT_MISMATCH");
    }
    const key = streamKey(matchId, context.slot, state.side), nowMs = Date.now();
    for(const [other,stream] of this.streams)if(nowMs-stream.lastReceivedAtMs>31_500)this.streams.delete(other);
    if(!this.streams.has(key) && this.streams.size>=SPECTATOR_MAX_STREAMS)throw new ConflictException("SPECTATOR_STREAM_LIMIT");
    const mapValue=(context.deathmatchSequence===null?state.currentRound:state.deathmatch)?.map;
    const map=isRecord(mapValue)?mapValue:null;
    const expectedLevel=Number(map?.playableLevelId??map?.canonicalLevelId??map?.levelId);
    if(packets.some(item=>item.levelId!==undefined && Number.isSafeInteger(expectedLevel) && expectedLevel>0 && item.levelId!==expectedLevel))
      throw new ConflictException("SPECTATOR_LEVEL_MISMATCH");
    const existing = this.streams.get(key);
    let stream: LiveStream;
    if (!existing || existing.attemptId !== packet.attemptId || nowMs - existing.lastReceivedAtMs > SPECTATOR_LIVE_TIMEOUT_MS &&
        !(existing.pausedAtMs != null && nowMs - existing.pausedAtMs <= 31_500)) {
      if (packet.sequence !== 0) throw new ConflictException("SPECTATOR_STREAM_MUST_START_AT_SEQUENCE_ZERO");
      stream = { matchId, slot: context.slot, side: state.side, attemptId: packet.attemptId,
        generation: packet.generation, lastSequence: -1, lastReceivedAtMs: nowMs, packets: [] };
    } else {
      if (packet.generation < existing.generation) throw new ConflictException("SPECTATOR_STALE_GENERATION");
      if (packet.generation > existing.generation) {
        if (existing.packets.at(-1)?.keyframe.dead || existing.packets.at(-1)?.keyframe.finished) throw new ConflictException("SPECTATOR_TERMINAL_STREAM");
        if (packet.sequence !== 0) throw new ConflictException("SPECTATOR_NEW_GENERATION_MUST_START_AT_SEQUENCE_ZERO");
        stream = { ...existing, generation: packet.generation, lastSequence: -1, packets: [], history:[], historyContinuous:true };
      } else stream = { ...existing, packets: [...existing.packets], history:[...(existing.history??[])] };
    }
    for (const item of packets) {
      if (item.sequence <= stream.lastSequence) {
        const prior = (stream.history??stream.packets).find(sample => sample.sequence === item.sequence);
        if (!prior || JSON.stringify(publicPacket(prior)) !== JSON.stringify(item) ||
            (!batched && !(item.keyframe.dead || item.keyframe.finished))) throw new ConflictException("SPECTATOR_STALE_SEQUENCE");
        continue; // immutable batch ACK-loss retry
      }
      if (stream.lastSequence >= 0 && item.sequence - stream.lastSequence > SPECTATOR_LIVE_MAX_SEQUENCE_GAP) {
        throw new ConflictException("SPECTATOR_SEQUENCE_GAP");
      }
      this.appendPacket(stream, item, nowMs);
    }
    // All validation and appends happen on a copy; a rejected batch is atomic.
    if ((state.pause as { active?: boolean } | undefined)?.active) stream.pausedAtMs ??= nowMs;
    this.streams.set(key, stream);
    return { accepted: true, protocolVersion: SPECTATOR_LIVE_PROTOCOL_VERSION,
      attemptId: stream.attemptId, generation: stream.generation, sequence: stream.lastSequence };
  }

  public async subscribe(
    matchId: string,
    matchToken: string,
    session: RankedSessionContext,
    protocolVersionText?: string,
    attemptId?: string,
    generationText?: string,
    afterSequenceText?: string,
    worldHistoryText?: string,
  ): Promise<SpectatorLiveSubscribeResponse> {
    const state = await this.decorateConfiguredState(await this.matches.state(matchId, matchToken, session));
    if (!this.liveEnabled) throw new ForbiddenException("LIVE_SPECTATOR_DISABLED");
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
      const key = streamKey(matchId, context.slot, opponentSide);
      const terminalStream = this.streams.get(key);
      const ended = this.runtimeState.endedAttempt?.(matchId, context.slot, opponentSide);
      const terminal = terminalStream?.packets.at(-1)?.keyframe;
      if (terminalStream && ended && ended.attemptId === terminalStream.attemptId &&
        Date.now() - ended.endedAtMs <= SPECTATOR_LIVE_TIMEOUT_MS && (terminal?.dead || terminal?.finished)) {
        return this.readStream(key, terminalStream, context.roundNumber, context.deathmatchSequence,
          state.spectator.opponentName ?? "Opponent", attemptId, generationText, afterSequenceText, worldHistoryText==="1");
      }
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
    if (!state.opponentPaused && nowMs - stream.lastReceivedAtMs > SPECTATOR_LIVE_TIMEOUT_MS) {
      this.streams.delete(key);
      return this.fallback("STREAM_TIMEOUT");
    }

    return { ...this.readStream(key, stream, context.roundNumber, context.deathmatchSequence,
      state.spectator.opponentName ?? "Opponent", attemptId, generationText, afterSequenceText, worldHistoryText==="1"), paused: state.opponentPaused === true };
  }

  // Internal authorization/context adapter for the isolated Recorded Publisher.
  // No MatchService, Attempt, profile, score or outbox operation is invoked here.
  public publishRecorded(packet: SpectatorLivePacket, nowMs: number): void {
    if (!packet.matchId.startsWith("debug-spectate:") || packet.protocolVersion !== SPECTATOR_LIVE_PROTOCOL_VERSION) {
      throw new BadRequestException("INVALID_RECORDED_CONTEXT");
    }
    const keyframe=parseSpectatorKeyframe(packet.keyframe);
    const key = `recorded:${packet.matchId}`;
    for(const [other,s] of this.streams)if(nowMs-s.lastReceivedAtMs>31_500)this.streams.delete(other);
    if(!this.streams.has(key) && this.streams.size>=SPECTATOR_MAX_STREAMS)throw new ConflictException("SPECTATOR_STREAM_LIMIT");
    let stream = this.streams.get(key);
    if (!stream || stream.generation !== packet.generation || stream.attemptId !== packet.attemptId) {
      if (stream && packet.generation < stream.generation) throw new ConflictException("SPECTATOR_STALE_GENERATION");
      if (packet.sequence !== 0) throw new ConflictException("SPECTATOR_STREAM_MUST_START_AT_SEQUENCE_ZERO");
      stream = { matchId: packet.matchId, slot: 1, side: "A", attemptId: packet.attemptId,
        generation: packet.generation, lastSequence: -1, lastReceivedAtMs: nowMs, packets: [] };
      this.streams.set(key, stream);
    }
    if (packet.sequence !== stream.lastSequence + 1) throw new ConflictException("SPECTATOR_SEQUENCE_GAP");
    this.appendPacket(stream, {...packet,keyframe}, nowMs);
  }

  public subscribeRecorded(contextId: string, nowMs: number, protocolVersionText?: string,
    attemptId?: string, generationText?: string, afterSequenceText?: string): SpectatorLiveSubscribeResponse {
    if (protocolVersionText !== undefined && queryInteger(protocolVersionText) !== SPECTATOR_LIVE_PROTOCOL_VERSION) {
      return this.fallback("UNSUPPORTED_PROTOCOL_VERSION");
    }
    const key = `recorded:${contextId}`;
    const stream = this.streams.get(key);
    if (!stream) return { ...this.fallback("STREAM_NOT_READY"), waiting: true, fallback: false };
    const terminal = stream.packets.at(-1)?.keyframe;
    if (!terminal?.dead && !terminal?.finished && nowMs - stream.lastReceivedAtMs > SPECTATOR_LIVE_TIMEOUT_MS) {
      return this.fallback("STREAM_TIMEOUT");
    }
    return this.readStream(key, stream, 1, null, "VOICE Recorded Publisher", attemptId,
      generationText, afterSequenceText);
  }

  private appendPacket(stream: LiveStream, packet: SpectatorLivePacket, nowMs: number): void {
    const last = stream.packets.at(-1);
    if (last?.keyframe.dead || last?.keyframe.finished) throw new ConflictException("SPECTATOR_TERMINAL_STREAM");
    if (last && packet.elapsedMs < last.elapsedMs) throw new ConflictException("SPECTATOR_REVERSE_TIMESTAMP");
    if(last && (packet.sequence!==last.sequence+1 || packet.levelId!==last.levelId || packet.elapsedMs-last.elapsedMs>250))stream.historyContinuous=false;
    const buffered={...packet,receivedAtMs:nowMs};
    stream.history=[...(stream.history??[]),buffered].slice(-SPECTATOR_HISTORY_MAX_PACKETS);
    stream.lastSequence = packet.sequence;
    stream.lastReceivedAtMs = nowMs;
    stream.packets.push(buffered);
    stream.packets = stream.packets.filter((item) => item.receivedAtMs >= nowMs - SPECTATOR_LIVE_BUFFER_MS);
    if (stream.packets.length > SPECTATOR_LIVE_MAX_PACKETS) {
      stream.packets.splice(0, stream.packets.length - SPECTATOR_LIVE_MAX_PACKETS);
    }
  }

  private readStream(_key: string, stream: LiveStream, roundNumber: number, deathmatchSequence: number | null,
    opponentName: string, attemptId?: string, generationText?: string,
    afterSequenceText?: string, worldHistory=false): SpectatorLiveSubscribeResponse {
    const requestedGeneration = queryInteger(generationText);
    const afterSequence = queryInteger(afterSequenceText);
    const hasStreamIdentity = Boolean(attemptId) && requestedGeneration !== null;
    const sameAttempt = attemptId === stream.attemptId;
    const sameGeneration = requestedGeneration === stream.generation;
    // A late join has no prior stream identity and must apply the bounded buffer
    // as a reset before switching to incremental live packets.
    const history=stream.history??stream.packets;
    const available=history[0]?.sequence===0 && history[0].elapsedMs<=250 && stream.historyContinuous!==false;
    const source=worldHistory && available?history:stream.packets;
    const missedWindow = hasStreamIdentity && sameAttempt && sameGeneration && afterSequence !== null &&
      stream.lastSequence > afterSequence && (source[0]?.sequence ?? Infinity) > afterSequence + 1;
    const reset = !hasStreamIdentity || !sameAttempt || !sameGeneration || missedWindow;
    const effectiveAfter = reset ? null : afterSequence;
    const latestTime = stream.packets.at(-1)?.elapsedMs ?? 0;
    // Seed close to the live edge. Replaying 1.5-3s at a 1x clock never catches
    // up, and the relay's 72 packets exceed the client's 64-sample window.
    const joinWindow = stream.packets.filter(packet => packet.elapsedMs >= latestTime - 350).slice(-16);
    const bufferedPackets = worldHistory && available
      ? source.filter(packet=>packet.sequence>(effectiveAfter??-1)).slice(0,SPECTATOR_HISTORY_PAGE_PACKETS)
      : effectiveAfter===null?joinWindow:source.filter(packet=>packet.sequence>effectiveAfter);
    if (
      effectiveAfter !== null &&
      stream.lastSequence > effectiveAfter &&
      (bufferedPackets.length === 0 || bufferedPackets[0]!.sequence > effectiveAfter + 1)
    ) {
      return this.fallback("SEQUENCE_GAP");
    }
    if (
      effectiveAfter !== null &&
      effectiveAfter > stream.lastSequence
    ) {
      return this.fallback("SEQUENCE_GAP");
    }
    const packets = bufferedPackets.map(publicPacket);

    return {
      protocolVersion: SPECTATOR_LIVE_PROTOCOL_VERSION,
      active: true,
      waiting: false,
      fallback: false,
      reason: null,
      reset,
      ...(worldHistory?{restoration:{available,reason:available?null:stream.historyContinuous===false?"HISTORY_GAP":history[0]?.sequence!==0?"HISTORY_WINDOW_EXPIRED":"ATTEMPT_START_MISSING",
        caughtUp:(bufferedPackets.at(-1)?.sequence??effectiveAfter??-1)>=stream.lastSequence,headSequence:stream.lastSequence,
        startSequence:history[0]?.sequence??-1,levelId:history[0]?.levelId??null}}:{}),
      bufferWindowMs: SPECTATOR_LIVE_BUFFER_MS,
      maxPackets: SPECTATOR_LIVE_MAX_PACKETS,
      stream: {
        matchId: stream.matchId,
        roundNumber,
        deathmatchSequence,
        attemptId: stream.attemptId,
        generation: stream.generation,
        lastSequence: stream.lastSequence,
        opponentName,
      },
      packets,
    };
  }

  public clearMatch(matchId: string): void {
    for (const [key, stream] of this.streams) {
      if (stream.matchId === matchId) this.streams.delete(key);
    }
  }

  public clearAttempt(matchId: string, attemptId: string): void {
    for (const [key, stream] of this.streams) {
      if (stream.matchId === matchId && stream.attemptId === attemptId) {
        this.streams.delete(key);
      }
    }
  }

  private reconcileStreams(state: SpectatorPublicState): void {
    const context = spectatorLiveContext(state);
    if (!context) {
      this.clearMatch(state.matchId);
      return;
    }
    for (const [key, stream] of this.streams) {
      if (stream.matchId !== state.matchId) continue;
      const authoritative = this.runtimeState.progress(state.matchId, context.slot, stream.side);
      const ended = this.runtimeState.endedAttempt?.(state.matchId, context.slot, stream.side);
      // The authoritative End can race the independent visual terminal HTTP
      // request. Retain only its exact ended identity for the bounded relay TTL.
      const recentEnd = ended && ended.attemptId === stream.attemptId &&
        Date.now() - ended.endedAtMs <= SPECTATOR_LIVE_TIMEOUT_MS;
      if (
        stream.slot !== context.slot ||
        ((!authoritative || authoritative.attemptId !== stream.attemptId) && !recentEnd) ||
        this.runtimeState.practice(state.matchId, context.slot, stream.side)
      ) {
        this.streams.delete(key);
      }
    }
  }

  private clearViewerObsoleteStreams(state: SpectatorPublicState): void {
    this.reconcileStreams(state);
  }

  private fallback(reason: string): SpectatorLiveSubscribeResponse {
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
