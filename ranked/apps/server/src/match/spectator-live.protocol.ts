// Shared validated visual wire schema. No Solo/Survival authority or persistence dependency.
import {BadRequestException} from "@nestjs/common";
import {parseSpectatorCosmetics,type SpectatorCosmetics} from "./spectator-cosmetics.js";
import type {PlayerSide} from "@corum-ranked/rules";
import {parseSpectatorRandomEvents,type SpectatorRandomEvent} from './spectator-world-events.js';
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
  readonly nativeEvents?:readonly {readonly type:"TELEPORT"|"SPIDER_SNAP";readonly fromX:number;readonly fromY:number;readonly toX:number;readonly toY:number;readonly beforeMode:SpectatorGameMode;readonly afterMode:SpectatorGameMode;readonly beforeGravity:boolean;readonly afterGravity:boolean;readonly trace?:{event:number;tick:number;step:number;elapsedMs:number;callback:string}}[];
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
  readonly audio?: {readonly trackHash:string;readonly positionMs:number};
  readonly simulation?: {readonly elapsedMs:number;readonly tick:number;readonly started:boolean;readonly resetDelay:boolean};
  readonly randomEvents?:readonly SpectatorRandomEvent[];
  readonly followHistory?:{readonly index:number;readonly remainder:number;readonly y:readonly number[]};
  readonly cameraX?: number;
  readonly cameraY?: number;
  readonly cameraSnap?: boolean;
  readonly cameraZoom?: number;
  readonly cameraAngle?: number;
  readonly cameraLayers?: readonly {readonly present:boolean;readonly scaleX:number;readonly scaleY:number;readonly rotation:number}[];
  readonly discontinuity?: SpectatorSnapEvidence;
  readonly visible?: boolean;
  readonly dual: boolean;
  readonly player2?: SpectatorPoseKeyframe;
  readonly effects?: number;
  readonly progress?: number;
  readonly cosmetics?: SpectatorCosmetics;
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
  readonly cosmetics?: SpectatorCosmetics;
  readonly inputEdges: readonly SpectatorInputEdge[];
  readonly keyframe: SpectatorPoseKeyframe;
}

export interface BufferedPacket extends SpectatorLivePacket {
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
  readonly cosmetics?: SpectatorCosmetics;
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

export interface LiveStream {
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
  cosmetics?: SpectatorCosmetics;
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
  let audio: SpectatorPoseKeyframe["audio"];
  let simulation:SpectatorPoseKeyframe["simulation"];
  let followHistory:SpectatorPoseKeyframe["followHistory"];
  if(value.followHistory!==undefined){
    if(!isRecord(value.followHistory)||!Array.isArray(value.followHistory.y)||value.followHistory.y.length!==200)throw new BadRequestException("Invalid native Follow Player Y history");
    const h=value.followHistory,index=integer(h.index,"followHistory.index",0),remainder=finiteNumber(h.remainder,"followHistory.remainder");
    if(index>1000000000||remainder<0||remainder>1)throw new BadRequestException("Invalid native Follow history clock");
    const y=(h.y as unknown[]).map(v=>{const n=finiteNumber(v,"followHistory.y");if(Math.abs(n)>10000000)throw new BadRequestException("Invalid native Follow sample");return n;});followHistory={index,remainder,y};
  }
  const randomEvents=value.randomEvents===undefined?undefined:parseSpectatorRandomEvents(value.randomEvents);
  if(value.simulation!==undefined){
    if(!isRecord(value.simulation))throw new BadRequestException("Invalid native simulation clock");
    const c=value.simulation,time=finiteNumber(c.elapsedMs,"simulation.elapsedMs"),tick=integer(c.tick,"simulation.tick",0);
    if(time<0||time>86400000||tick>1000000000)throw new BadRequestException("Invalid native simulation clock");
    simulation={elapsedMs:time,tick,started:requiredBoolean(c.started,"simulation.started"),resetDelay:requiredBoolean(c.resetDelay,"simulation.resetDelay")};
  }
  if(randomEvents?.length&&!simulation)throw new BadRequestException("Random events require a native source clock");
  if(value.audio!==undefined){
    if(!isRecord(value.audio))throw new BadRequestException("Invalid spectator audio sample");
    const sample=value.audio;
    const time=finiteNumber(sample.positionMs,"audio.positionMs");
    if(typeof sample.trackHash!=="string"||!/^[a-f0-9]{64}$/.test(sample.trackHash)||time<0||time>86_400_000)throw new BadRequestException("Invalid spectator audio sample");
    audio={trackHash:sample.trackHash,positionMs:time};
  }
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
    let nativeEvents:SpectatorSnapEvidence["nativeEvents"];
    if(snap.nativeEvents!==undefined){
      if(!Array.isArray(snap.nativeEvents)||snap.nativeEvents.length<1||snap.nativeEvents.length>8)throw new BadRequestException("Invalid native event chain");
      nativeEvents=snap.nativeEvents.map(raw=>{if(!isRecord(raw)||(raw.type!=="TELEPORT"&&raw.type!=="SPIDER_SNAP")||!SPECTATOR_GAME_MODES.includes(raw.beforeMode as SpectatorGameMode)||!SPECTATOR_GAME_MODES.includes(raw.afterMode as SpectatorGameMode))throw new BadRequestException("Invalid native event chain");
        const x=finiteNumber(raw.fromX,"native.fromX"),y=finiteNumber(raw.fromY,"native.fromY"),tx=finiteNumber(raw.toX,"native.toX"),ty=finiteNumber(raw.toY,"native.toY"),d=Math.hypot(tx-x,ty-y);
        const beforeGravity=requiredBoolean(raw.beforeGravity,"native.beforeGravity"),afterGravity=requiredBoolean(raw.afterGravity,"native.afterGravity");
        if([x,y,tx,ty].some(v=>Math.abs(v)>10000000)||d===0||(raw.type==="TELEPORT"?d>100000:Math.abs(tx-x)>400||Math.abs(ty-y)>2400)||
           (raw.type==="SPIDER_SNAP"&&(raw.afterMode!=="SPIDER"||(raw.beforeMode===raw.afterMode&&beforeGravity===afterGravity))))throw new BadRequestException("Unverified native event chain");
        let trace:{event:number;tick:number;step:number;elapsedMs:number;callback:string}|undefined;
        if(raw.trace!==undefined){const t=raw.trace;
          if(!isRecord(t)||![t.event,t.tick,t.step].every(n=>typeof n==='number'&&Number.isSafeInteger(n)&&n>=0&&n<=1000000000)||Number(t.event)<1||typeof t.elapsedMs!=='number'||!Number.isFinite(t.elapsedMs)||t.elapsedMs<0||t.elapsedMs>86400000||!['PORTAL','TELEPORT_FLAG','SPIDER'].includes(String(t.callback)))throw new BadRequestException('Invalid native diagnostic identity');
          trace={event:Number(t.event),tick:Number(t.tick),step:Number(t.step),elapsedMs:t.elapsedMs,callback:String(t.callback)};
        }
        return {type:raw.type,fromX:x,fromY:y,toX:tx,toY:ty,beforeMode:raw.beforeMode as SpectatorGameMode,afterMode:raw.afterMode as SpectatorGameMode,beforeGravity,afterGravity,...(trace?{trace}:{})};
      });
      if(Math.hypot(nativeEvents[0]!.fromX-fromX,nativeEvents[0]!.fromY-fromY)>.1||Math.hypot(nativeEvents.at(-1)!.toX-toX,nativeEvents.at(-1)!.toY-toY)>.1||nativeEvents.some((e,i)=>i>0&&Math.hypot(e.fromX-nativeEvents![i-1]!.toX,e.fromY-nativeEvents![i-1]!.toY)>400))throw new BadRequestException("Noncontiguous native event chain");
    }
    discontinuity = { type: snap.type, fromX, fromY, toX, toY,...(nativeEvents?{nativeEvents}:{}),
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
  let player2:SpectatorPoseKeyframe|undefined;
  if(dual){
    if(!isRecord(value.player2)||value.player2.dual!==false||value.player2.player2!==undefined||typeof value.player2.visible!=="boolean")
      throw new BadRequestException("INCOMPLETE_DUAL_POSE");
    player2=parseSpectatorKeyframe(value.player2);
  }else if(value.player2!==undefined)throw new BadRequestException("UNEXPECTED_DUAL_POSE");
  const effects=value.effects===undefined?undefined:integer(value.effects,"keyframe.effects",0);
  if(effects!==undefined&&effects>32767)throw new BadRequestException("INVALID_VISUAL_MASK");
  const progress=value.progress===undefined?undefined:finiteNumber(value.progress,"keyframe.progress");
  if(progress!==undefined&&(progress<0||progress>100))throw new BadRequestException("INVALID_VISUAL_PROGRESS");
  if (dead && finished) throw new BadRequestException("keyframe terminal flags conflict");
  return {
    x,
    y,
    rotation,
    mode, mini, upsideDown, flipped, dual, dead, finished,
    ...(value.cosmetics===undefined?{}:{cosmetics:parseSpectatorCosmetics(value.cosmetics)}),
    ...(player2?{player2}:{}),...(effects===undefined?{}:{effects}),...(progress===undefined?{}:{progress}),
    ...(velocityX === undefined ? {} : { velocityX }),
    ...(velocityY === undefined ? {} : { velocityY }),
    ...(gravity === undefined ? {} : { gravity }),
    ...(audio ? {audio} : {}),
    ...(simulation?{simulation}:{}),
    ...(randomEvents?{randomEvents}:{}),
    ...(followHistory?{followHistory}:{}),
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

export const parseSpectatorPacket = (value: unknown, matchId: string): SpectatorLivePacket => {
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
    ...(value.cosmetics===undefined?{}:{cosmetics:parseSpectatorCosmetics(value.cosmetics)}),
    keyframe: parseSpectatorKeyframe(value.keyframe),
  };
};
