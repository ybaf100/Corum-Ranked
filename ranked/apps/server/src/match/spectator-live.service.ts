export * from "./spectator-live.protocol.js";
import {parseSpectatorKeyframe,parseSpectatorPacket,type LiveStream,type BufferedPacket,type SpectatorLivePacket,type SpectatorLiveSubscribeResponse,type SpectatorLivePublishResponse} from "./spectator-live.protocol.js";
import {appendSpectatorPacket,readSpectatorStream} from "./spectator-relay.js";
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
  SPECTATOR_MAX_STREAMS,
  decorateSpectatorState,
  spectatorLiveContext,
  spectatorOpponentSide,
  type SpectatorPublicState,
} from "./spectator-live.policy.js";

const isRecord=(value:unknown):value is Record<string,unknown>=>typeof value==="object"&&value!==null&&!Array.isArray(value);

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
    const packets = values.map(value => parseSpectatorPacket(value, matchId));
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
        delete stream.cosmetics;
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
    cosmeticRevisionText?: string,
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
          state.spectator.opponentName ?? "Opponent", attemptId, generationText, afterSequenceText, worldHistoryText==="1",cosmeticRevisionText);
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
      state.spectator.opponentName ?? "Opponent", attemptId, generationText, afterSequenceText, worldHistoryText==="1",cosmeticRevisionText), paused: state.opponentPaused === true };
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
    attemptId?: string, generationText?: string, afterSequenceText?: string, cosmeticRevisionText?:string, worldHistoryText?:string): SpectatorLiveSubscribeResponse {
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
      generationText, afterSequenceText,worldHistoryText==="1",cosmeticRevisionText);
  }

  private appendPacket(stream: LiveStream, packet: SpectatorLivePacket, nowMs: number): void {
    appendSpectatorPacket(stream,packet,nowMs);
  }
  private readStream(key:string,stream:LiveStream,roundNumber:number,deathmatchSequence:number|null,
    opponentName:string,attemptId?:string,generationText?:string,afterSequenceText?:string,worldHistory=false,cosmeticRevisionText?:string):SpectatorLiveSubscribeResponse {
    return readSpectatorStream(key,stream,roundNumber,deathmatchSequence,opponentName,attemptId,generationText,afterSequenceText,worldHistory,cosmeticRevisionText);
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
