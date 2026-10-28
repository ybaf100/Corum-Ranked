import { randomBytes, timingSafeEqual } from "node:crypto";
import { BadRequestException, ConflictException, Inject, Injectable, Logger,
  NotFoundException, UnauthorizedException, type OnApplicationShutdown, type OnModuleInit } from "@nestjs/common";
import { ID_GENERATOR, SERVER_CLOCK, type IdGenerator, type ServerClock } from "../common/runtime.module.js";
import { TokenService } from "../common/token.service.js";
import { SERVER_ENVIRONMENT, type ServerEnvironment } from "../config/server-environment.js";
import { DATABASE, type DatabasePort } from "../database/database.port.js";
import { SpectatorLiveService, parseSpectatorKeyframe, type SpectatorPoseKeyframe } from "../match/spectator-live.service.js";
import type {SpectatorCosmetics} from "../match/spectator-cosmetics.js";
import { SPECTATOR_LIVE_PROTOCOL_VERSION } from "../match/spectator-live.policy.js";
import {relayFallback} from "../match/spectator-relay.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { DEBUG_SPECTATE_FORMAT_VERSION, DEBUG_SPECTATE_MAX_FRAMES,
  DEBUG_SPECTATE_MAX_SESSIONS, DEBUG_SPECTATE_SAMPLE_MS, DEBUG_SPECTATE_TTL_MS, debugSpectateContextId } from "./debug-spectate.config.js";

interface Frame { readonly sequence: number; readonly elapsedMs: number; readonly keyframe: SpectatorPoseKeyframe }
interface Recording {
  readonly id: string; readonly tokenHash: string; readonly expiresAtMs: number;
  frames: Frame[]; finalizing: Promise<unknown> | null; finalizeCompleted: boolean | null;
}
interface RecordingRow {
  id: string; level_id: number; format_version: number; created_at: Date | string;
  duration_ms: number; frame_count: number; completed: boolean; finalized_at: Date | string | null;
  token_hash: string; expires_at: Date | string;
}
interface Playback {
  readonly id: string; readonly recordingId: string; readonly tokenHash: string; readonly levelId:number;
  readonly expiresAtMs: number; readonly frames: readonly Frame[];
  readonly startElapsedMs:number;lastCosmetics?:SpectatorCosmetics;
  barrierMode:boolean;awaitingReady:boolean;heldElapsedMs:number;
  startedAtMs: number | null; cursor: number; generation: number; sequence: number; completed: boolean;
}
const object = (value: unknown): Record<string, unknown> => {
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new BadRequestException("INVALID_DEBUG_BODY");
  return value as Record<string, unknown>;
};
const integer = (value: unknown, max: number): number => {
  if (typeof value !== "number" || !Number.isSafeInteger(value) || value < 0 || value > max) {
    throw new BadRequestException("INVALID_DEBUG_FRAME_SEQUENCE_OR_TIME");
  }
  return value;
};

/** Never injects Ranked Match/Queue/Placement/Achievement services. Tokens are
 * scoped to these in-memory debug sessions, not ranked_sessions/match tokens. */
@Injectable()
export class DebugSpectateService implements OnModuleInit, OnApplicationShutdown {
  private readonly logger = new Logger("DebugSpectate");
  private readonly recordings = new Map<string, Recording>();
  private readonly playbacks = new Map<string, Playback>();
  private interval: ReturnType<typeof setInterval> | null = null;
  private startingRecordings = 0;
  private startingPlaybacks = 0;
  public constructor(
    @Inject(SERVER_ENVIRONMENT) private readonly environment: ServerEnvironment,
    @Inject(DATABASE) private readonly database: DatabasePort,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
    @Inject(ID_GENERATOR) private readonly ids: IdGenerator,
    @Inject(TokenService) private readonly tokens: TokenService,
    @Inject(SpectatorLiveService) private readonly relay: SpectatorLiveService,
  ) {}
  public onModuleInit(): void {
    if (!this.environment.debugSpectate) return;
    this.interval = setInterval(() => this.tick(), DEBUG_SPECTATE_SAMPLE_MS);
    this.interval.unref?.();
  }
  public onApplicationShutdown(): void {
    if (this.interval) clearInterval(this.interval);
    this.interval = null;
    for (const playback of this.playbacks.values()) this.cleanupPlayback(playback);
    this.playbacks.clear();
    this.recordings.clear(); // DB rows remain incomplete; never eligible for TEST.
  }
  private enabled(clientDebug: string | undefined): void {
    if (!this.environment.debugSpectate || clientDebug !== "1") throw new NotFoundException("DEBUG_SPECTATE_DISABLED");
  }
  private authorizeStart(body: unknown, clientDebug: string | undefined): void {
    this.enabled(clientDebug);
    const value = object(body);
    const password = typeof value.password === "string" ? value.password : "";
    const actual = Buffer.from(password);
    const expected = Buffer.from(this.environment.debugSpectate!.password);
    if (actual.length !== expected.length || !timingSafeEqual(actual, expected)) {
      throw new UnauthorizedException("INVALID_DEBUG_AUTHORIZATION");
    }
    if (typeof value.levelId!=="number" || !Number.isSafeInteger(value.levelId) || value.levelId<1 || value.levelId>2147483647 || value.formatVersion !== DEBUG_SPECTATE_FORMAT_VERSION) {
      throw new BadRequestException("DEBUG_SPECTATE_REQUIRES_VALID_LEVEL_AND_FORMAT");
    }
    this.tick();
  }
  private authorized(token: string | undefined, hash: string, expiresAtMs: number): void {
    if (!token || this.clock.now().getTime() >= expiresAtMs || !this.tokens.matches(token, hash)) {
      throw new UnauthorizedException("INVALID_OR_EXPIRED_DEBUG_SCOPE");
    }
  }
  public async liveSetting(clientDebug?: string) {
    this.enabled(clientDebug);
    return { liveEnabled: await this.relay.getLiveEnabled() };
  }
  public async setLiveSetting(body: unknown, clientDebug?: string) {
    this.authorizeStart(body, clientDebug);
    const value = object(body);
    if (typeof value.liveEnabled !== "boolean") throw new BadRequestException("LIVE_ENABLED_MUST_BE_BOOLEAN");
    return { liveEnabled: await this.relay.setLiveEnabled(value.liveEnabled) };
  }
  public async createRecording(session: RankedSessionContext, body: unknown, clientDebug?: string) {
    this.authorizeStart(body, clientDebug);
    if (this.recordings.size + this.startingRecordings >= DEBUG_SPECTATE_MAX_SESSIONS) throw new ConflictException("DEBUG_RECORDING_CAPACITY");
    ++this.startingRecordings;
    try {
    const id = this.ids.next();
    const token = `crds_record_${randomBytes(32).toString("base64url")}`;
    const now = this.clock.now();
    const expiresAt = new Date(now.getTime() + DEBUG_SPECTATE_TTL_MS);
    await this.database.query(`INSERT INTO debug_spectate_recordings
      (id, level_id, format_version, sample_interval_ms, created_at, recorder_identity, token_hash, expires_at)
      VALUES ($1, $2, $3, $4, $5, $6, $7, $8)`,
    [id, object(body).levelId, DEBUG_SPECTATE_FORMAT_VERSION, DEBUG_SPECTATE_SAMPLE_MS,
      now, `${session.gdAccountId}:${session.gdUsername}`, this.tokens.hash(token), expiresAt]);
    this.recordings.set(id, { id, tokenHash: this.tokens.hash(token), expiresAtMs: expiresAt.getTime(), frames: [], finalizing: null, finalizeCompleted: null });
    this.logger.log(`[DebugSpectate] record started recordingId=${id}`);
    return { recordingId: id, token, levelId: object(body).levelId,
      formatVersion: DEBUG_SPECTATE_FORMAT_VERSION, sampleIntervalMs: DEBUG_SPECTATE_SAMPLE_MS, expiresAt: expiresAt.toISOString() };
    } finally { --this.startingRecordings; }
  }
  public upload(id: string, token: string | undefined, body: unknown, clientDebug?: string) {
    this.enabled(clientDebug);
    const recording = this.recordings.get(id);
    if (!recording) throw new NotFoundException("DEBUG_RECORDING_NOT_ACTIVE");
    this.authorized(token, recording.tokenHash, recording.expiresAtMs);
    if (recording.finalizing) throw new ConflictException("DEBUG_RECORDING_FINALIZING");
    const value = object(body);
    if (value.formatVersion !== DEBUG_SPECTATE_FORMAT_VERSION || !Array.isArray(value.frames) ||
      value.frames.length === 0 || value.frames.length > 16) throw new BadRequestException("INVALID_DEBUG_FRAME_BATCH");
    // Validate the whole batch before mutating memory. Retries must be byte-for-byte
    // equivalent in the canonical production keyframe representation.
    const next = [...recording.frames];
    for (const raw of value.frames) {
      const frame = object(raw);
      const keyframe = object(frame.keyframe);
      if (typeof keyframe.dead !== "boolean" || typeof keyframe.finished !== "boolean" ||
        (keyframe.dead && keyframe.finished)) throw new BadRequestException("INVALID_DEBUG_TERMINAL_FLAGS");
      const parsed: Frame = { sequence: integer(frame.sequence, DEBUG_SPECTATE_MAX_FRAMES - 1),
        elapsedMs: integer(frame.elapsedMs, 600_000), keyframe: parseSpectatorKeyframe(keyframe) };
      if (parsed.sequence < next.length) {
        if (JSON.stringify(next[parsed.sequence]) !== JSON.stringify(parsed)) throw new ConflictException("DEBUG_FRAME_RETRY_CONFLICT");
        continue;
      }
      const previous = next.at(-1);
      if (parsed.sequence !== next.length || next.length >= DEBUG_SPECTATE_MAX_FRAMES ||
        (previous && (parsed.elapsedMs <= previous.elapsedMs || previous.keyframe.dead || previous.keyframe.finished))) {
        throw new ConflictException("DEBUG_FRAME_SEQUENCE_OR_TIMESTAMP_CONFLICT");
      }
      next.push(parsed);
    }
    recording.frames = next;
    return { accepted: true, lastSequence: next.length - 1, frameCount: next.length };
  }
  public async finalize(id: string, token: string | undefined, body: unknown, clientDebug?: string): Promise<unknown> {
    this.enabled(clientDebug);
    const value = object(body);
    if (typeof value.completed !== "boolean") throw new BadRequestException("DEBUG_COMPLETED_REQUIRED");
    const recording = this.recordings.get(id);
    if (!recording) {
      const result = await this.database.query<RecordingRow>("SELECT * FROM debug_spectate_recordings WHERE id = $1", [id]);
      const row = result.rows[0];
      if (!row) throw new NotFoundException("DEBUG_RECORDING_NOT_FOUND");
      this.authorized(token, row.token_hash, new Date(row.expires_at).getTime());
      if (!row.finalized_at) throw new ConflictException("DEBUG_RECORDING_INTERRUPTED");
      if (row.completed !== value.completed) throw new ConflictException("DEBUG_FINALIZE_RETRY_CONFLICT");
      return this.metadata(row); // ACK-loss retry, including after process restart.
    }
    this.authorized(token, recording.tokenHash, recording.expiresAtMs);
    if (recording.finalizing) {
      if (recording.finalizeCompleted !== value.completed) throw new ConflictException("DEBUG_FINALIZE_RETRY_CONFLICT");
      return recording.finalizing;
    }
    const last = recording.frames.at(-1);
    if (value.completed && (!last || (!last.keyframe.dead && !last.keyframe.finished))) {
      throw new ConflictException("DEBUG_COMPLETION_REQUIRES_TERMINAL_FRAME");
    }
    recording.finalizeCompleted = value.completed;
    recording.finalizing = this.persist(recording, value.completed);
    try { return await recording.finalizing; }
    catch (error) { recording.finalizing = null; recording.finalizeCompleted = null; throw error; }
  }
  private async persist(recording: Recording, completed: boolean) {
    const duration = recording.frames.at(-1)?.elapsedMs ?? 0;
    const result = await this.database.transaction(async (sql) => {
      await sql.query(`INSERT INTO debug_spectate_frames (recording_id, sequence, elapsed_ms, keyframe)
        SELECT $1, f.sequence, f."elapsedMs", f.keyframe
        FROM jsonb_to_recordset($2::jsonb) AS f(sequence integer, "elapsedMs" integer, keyframe jsonb)`,
      [recording.id, JSON.stringify(recording.frames)]);
      return sql.query<RecordingRow>(`UPDATE debug_spectate_recordings SET completed = $2,
        finalized_at = $3, duration_ms = $4, frame_count = $5 WHERE id = $1 RETURNING *`,
      [recording.id, completed, this.clock.now(), duration, recording.frames.length]);
    });
    this.recordings.delete(recording.id);
    this.logger.log(`[DebugSpectate] record finalized recordingId=${recording.id} completed=${completed}`);
    this.logger.log(`[DebugSpectate] recording persisted recordingId=${recording.id} frameCount=${recording.frames.length} durationMs=${duration}`);
    return this.metadata(result.rows[0]!);
  }
  private metadata(row: RecordingRow) {
    return { recordingId: row.id, levelId: Number(row.level_id), formatVersion: row.format_version,
      createdAt: new Date(row.created_at).toISOString(), durationMs: row.duration_ms,
      frameCount: row.frame_count, completed: row.completed, sampleIntervalMs: DEBUG_SPECTATE_SAMPLE_MS };
  }
  public async createPlayback(_session: RankedSessionContext, body: unknown, clientDebug?: string) {
    this.authorizeStart(body, clientDebug);
    const requested=object(body).startPercent===undefined?0:object(body).startPercent;
    if(typeof requested!=="number" || !Number.isFinite(requested) || requested<0 || requested>100)
      throw new BadRequestException("START_PERCENT_MUST_BE_0_TO_100");
    if (this.playbacks.size + this.startingPlaybacks >= DEBUG_SPECTATE_MAX_SESSIONS) throw new ConflictException("DEBUG_PLAYBACK_CAPACITY");
    ++this.startingPlaybacks;
    try {
    const rows = await this.database.query<RecordingRow>(`SELECT * FROM debug_spectate_recordings
      WHERE completed AND finalized_at IS NOT NULL AND format_version = $1 ORDER BY finalized_at DESC, created_at DESC, id DESC LIMIT 1`,
    [DEBUG_SPECTATE_FORMAT_VERSION]);
    const row = rows.rows[0];
    if (!row) throw new NotFoundException("NO RECORDED SPECTATE DATA");
    if(!Number.isSafeInteger(Number(row.level_id))||Number(row.level_id)<1||Number(row.level_id)>2147483647)throw new ConflictException("INVALID_RECORDED_LEVEL_ID");
    const frames = await this.database.query<{ sequence: number; elapsed_ms: number; keyframe: SpectatorPoseKeyframe }>(
      "SELECT sequence, elapsed_ms, keyframe FROM debug_spectate_frames WHERE recording_id = $1 ORDER BY sequence", [row.id]);
    if (frames.rows.length !== row.frame_count || frames.rows.length === 0 || frames.rows.length > DEBUG_SPECTATE_MAX_FRAMES) {
      throw new ConflictException("DEBUG_RECORDING_CORRUPT");
    }
    // Strictly revalidate persisted JSONB before feeding the production relay.
    const validated: Frame[] = [];
    for (const f of frames.rows) {
      const previous = validated.at(-1);
      if (f.sequence !== validated.length || !Number.isSafeInteger(f.elapsed_ms) || f.elapsed_ms < 0 ||
        (previous && (f.elapsed_ms <= previous.elapsedMs || previous.keyframe.dead || previous.keyframe.finished))) {
        throw new ConflictException("DEBUG_RECORDING_CORRUPT");
      }
      validated.push({ sequence: f.sequence, elapsedMs: f.elapsed_ms, keyframe: parseSpectatorKeyframe(f.keyframe) });
    }
    const terminal = validated.at(-1)!;
    if (!(terminal.keyframe.dead || terminal.keyframe.finished) || terminal.elapsedMs !== row.duration_ms) {
      throw new ConflictException("DEBUG_RECORDING_CORRUPT");
    }
    const id = this.ids.next();
    const token = `crds_playback_${randomBytes(32).toString("base64url")}`;
    const expiresAt = new Date(this.clock.now().getTime() + DEBUG_SPECTATE_TTL_MS);
    // Preserve persisted format-v2 data and validate its original duration first.
    // Only the playback clock/relayed elapsed times lose the leading blank time.
    const leadingMs = validated[0]!.elapsedMs;
    const normalized = validated.map(frame => ({ ...frame, elapsedMs: frame.elapsedMs - leadingMs }));
    const startIndex=requested===0?0:normalized.findIndex(frame=>frame.keyframe.progress!==undefined && frame.keyframe.progress>=requested);
    if(startIndex<0){
      throw new BadRequestException(normalized.some(frame=>frame.keyframe.progress!==undefined)
        ?"RECORDING_DID_NOT_REACH_START_PERCENT":"RECORDING_HAS_NO_PROGRESS_RECORD_AGAIN");
    }
    const startElapsedMs=normalized[startIndex]!.elapsedMs;
    this.playbacks.set(id, { id, recordingId: row.id, levelId:Number(row.level_id), tokenHash: this.tokens.hash(token),
      expiresAtMs: expiresAt.getTime(), frames: normalized,startElapsedMs,barrierMode:false,awaitingReady:false,heldElapsedMs:startElapsedMs, startedAtMs: null, cursor: 0,
      generation: 1, sequence: 0, completed: false });
    this.logger.log(`[DebugSpectate] playback created playbackId=${id} recordingId=${row.id}`);
    // NO frame file / pose injection. Server clock starts on first in-scene poll.
    return { ...this.metadata(row), playbackId: id, token, contextId: debugSpectateContextId(id),
      protocolVersion: SPECTATOR_LIVE_PROTOCOL_VERSION, playbackLeadingMs: leadingMs,
      playbackDurationMs: terminal.elapsedMs - leadingMs,startPercent:requested,playbackStartElapsedMs:startElapsedMs,playbackStartSequence:startIndex, startedAt: null, expiresAt: expiresAt.toISOString() };
    } finally { --this.startingPlaybacks; }
  }
  private playback(id: string, token: string | undefined, clientDebug?: string): Playback {
    this.enabled(clientDebug);
    const playback = this.playbacks.get(id);
    if (!playback) throw new NotFoundException("DEBUG_PLAYBACK_NOT_ACTIVE");
    this.authorized(token, playback.tokenHash, playback.expiresAtMs);
    return playback;
  }
  public subscribe(id: string, token: string | undefined, clientDebug?: string, protocolVersion?: string,
    attemptId?: string, generation?: string, afterSequence?: string, cosmeticRevision?:string, worldHistory?:string,restoreBarrier?:string,readySequence?:string) {
    const playback = this.playback(id, token, clientDebug);
    if(protocolVersion!=="2")return relayFallback("UNSUPPORTED_PROTOCOL_VERSION");
    const nowMs = this.clock.now().getTime();
    const first=playback.startedAtMs===null;
    if(first){
      playback.barrierMode=restoreBarrier==="1";playback.startedAtMs=nowMs-playback.startElapsedMs;
      this.advance(playback,nowMs);
      if(playback.barrierMode){playback.awaitingReady=true;playback.heldElapsedMs=playback.startElapsedMs;}
      this.logger.log(`[DebugSpectate] playback prepared playbackId=${id} sourceMs=${playback.startElapsedMs} awaitingWorld=${playback.awaitingReady}`);
    }else if(playback.barrierMode&&worldHistory==="1"&&!attemptId&&!playback.awaitingReady){
      this.advance(playback,nowMs);playback.heldElapsedMs=nowMs-playback.startedAtMs!;playback.awaitingReady=true;
    }
    if(playback.awaitingReady&&readySequence!==undefined&&attemptId===playback.recordingId&&generation===String(playback.generation)){
      const ready=Number(readySequence);
      if(Number.isSafeInteger(ready)&&ready===playback.cursor-1){
        playback.startedAtMs=nowMs-playback.heldElapsedMs;playback.awaitingReady=false;
        this.logger.log(`[DebugSpectate] world ready playbackId=${id} sequence=${ready} sourceMs=${playback.heldElapsedMs}`);
      }
    }
    this.advance(playback, nowMs);
    const response = playback.barrierMode?this.readPrefix(playback,attemptId,generation,afterSequence):this.relay.subscribeRecorded(debugSpectateContextId(id), nowMs,
      protocolVersion, attemptId, generation, afterSequence, cosmeticRevision,worldHistory);
    if (!playback.barrierMode&&response.reason === "SEQUENCE_GAP") {
      // Same production bounded-buffer gap. A Recorded Publisher can restart a
      // new generation at its latest reached frame; the next HTTP poll resets.
      ++playback.generation;
      playback.sequence = 0;
      const frame = playback.frames[Math.max(0, playback.cursor - 1)];
      if (frame) this.emit(playback, frame, nowMs);
    }
    return { ...response, playbackComplete: playback.completed,awaitingWorldReady:playback.awaitingReady };
  }
  private readPrefix(playback:Playback,attemptId?:string,generation?:string,afterText?:string){
    const head=playback.cursor-1;
    const same=attemptId===playback.recordingId&&generation===String(playback.generation);
    const after=afterText===undefined?-1:Number(afterText);
    if(same&&(!Number.isSafeInteger(after)||after<0||after>head))return {protocolVersion:2,active:true,waiting:false,fallback:true,reason:"SEQUENCE_GAP",packets:[]};
    const reset=!same;const cursor=reset?-1:after;
    const packets=playback.frames.slice(cursor+1,Math.min(head+1,cursor+65)).map(frame=>({protocolVersion:2,matchId:debugSpectateContextId(playback.id),roundNumber:1,deathmatchSequence:null,attemptId:playback.recordingId,generation:playback.generation,levelId:playback.levelId,sequence:frame.sequence,elapsedMs:frame.elapsedMs,inputEdges:[],keyframe:frame.keyframe}));
    return {protocolVersion:2,active:true,waiting:false,fallback:false,reason:null,reset,bufferWindowMs:3000,maxPackets:72,
      restoration:{available:true,reason:null,caughtUp:(packets.at(-1)?.sequence??cursor)>=head,headSequence:head,startSequence:0,levelId:playback.levelId},
      stream:{matchId:debugSpectateContextId(playback.id),roundNumber:1,deathmatchSequence:null,attemptId:playback.recordingId,generation:playback.generation,lastSequence:head,opponentName:"Recorded Publisher",...(playback.lastCosmetics?{cosmetics:playback.lastCosmetics}:{})},packets};
  }
  public closePlayback(id: string, token: string | undefined, clientDebug?: string) {
    const playback = this.playback(id, token, clientDebug);
    this.cleanupPlayback(playback);
    this.playbacks.delete(id);
    return { closed: true };
  }
  private cleanupPlayback(playback: Playback): void {
    this.relay.clearMatch(debugSpectateContextId(playback.id));
    this.logger.log(`[DebugSpectate] playback cleaned up playbackId=${playback.id}`);
  }
  public tick(): void {
    const now = this.clock.now().getTime();
    for (const [id, recording] of this.recordings) {
      if (now >= recording.expiresAtMs && !recording.finalizing) this.recordings.delete(id);
    }
    for (const [id, playback] of this.playbacks) {
      if (now >= playback.expiresAtMs) { this.cleanupPlayback(playback); this.playbacks.delete(id); }
      else this.advance(playback, now);
    }
  }
  private advance(playback: Playback, nowMs: number): void {
    if (playback.startedAtMs === null || playback.completed || playback.awaitingReady) return;
    const elapsedMs = nowMs - playback.startedAtMs;
    while (playback.cursor < playback.frames.length && playback.frames[playback.cursor]!.elapsedMs <= elapsedMs) {
      this.emit(playback, playback.frames[playback.cursor]!, nowMs);
      ++playback.cursor;
    }
    if (playback.cursor === playback.frames.length) {
      playback.completed = true;
      this.logger.log(`[DebugSpectate] playback completed playbackId=${playback.id}`);
    }
  }
  private emit(playback: Playback, frame: Frame, nowMs: number): void {
    if(frame.keyframe.cosmetics)playback.lastCosmetics=frame.keyframe.cosmetics;
    // A cursor recovery starts a fresh relay generation at the current frame.
    // Sparse metadata may live only in an earlier frame; carry its exact value.
    const descriptor=playback.sequence===0 && !frame.keyframe.cosmetics?playback.lastCosmetics:undefined;
    // The validated recording is already the bounded history owner. A paused
    // restore must not depend on a live relay entry that expires while held.
    if(playback.barrierMode){++playback.sequence;return;}
    this.relay.publishRecorded({ protocolVersion: SPECTATOR_LIVE_PROTOCOL_VERSION,
      matchId: debugSpectateContextId(playback.id), roundNumber: 1, deathmatchSequence: null,
      attemptId: playback.recordingId, generation: playback.generation, sequence: playback.sequence++,
      elapsedMs: frame.elapsedMs,levelId:playback.levelId,...(descriptor?{cosmetics:descriptor}:{}), inputEdges: [], keyframe: frame.keyframe }, nowMs);
    if (playback.cursor === 0 && playback.generation === 1) {
      this.logger.log(`[DebugSpectate] first normalized packet emitted playbackId=${playback.id} elapsedMs=${frame.elapsedMs}`);
    }
  }
}
