// Shared visual relay only. Authority belongs to the Solo/Survival adapters.
import { ConflictException } from "@nestjs/common";
import type { BufferedPacket,LiveStream,SpectatorLivePacket,SpectatorLiveSubscribeResponse } from "./spectator-live.protocol.js";
import { SPECTATOR_HISTORY_MAX_BYTES,SPECTATOR_HISTORY_MAX_PACKETS,SPECTATOR_HISTORY_PAGE_PACKETS,SPECTATOR_LIVE_BUFFER_MS,SPECTATOR_LIVE_MAX_PACKETS,SPECTATOR_LIVE_PROTOCOL_VERSION } from "./spectator-live.policy.js";
const queryInteger=(value:string|undefined):number|null=>{if(value===undefined||value==="")return null;const n=Number(value);return Number.isSafeInteger(n)&&n>=0?n:null;};
const publicPacket=({receivedAtMs:_,...packet}:BufferedPacket):SpectatorLivePacket=>packet;
export const relayFallback=(reason:string):SpectatorLiveSubscribeResponse=>({protocolVersion:SPECTATOR_LIVE_PROTOCOL_VERSION,active:true,waiting:false,fallback:true,reason,bufferWindowMs:SPECTATOR_LIVE_BUFFER_MS,maxPackets:SPECTATOR_LIVE_MAX_PACKETS,stream:null,packets:[]});
export function appendSpectatorPacket(stream: LiveStream, packet: SpectatorLivePacket, nowMs: number, historyLimit: number = SPECTATOR_HISTORY_MAX_PACKETS, historyByteLimit: number = SPECTATOR_HISTORY_MAX_BYTES): void {
    const last = stream.packets.at(-1);
    if (last?.keyframe.dead || last?.keyframe.finished) throw new ConflictException("SPECTATOR_TERMINAL_STREAM");
    if (last && packet.elapsedMs < last.elapsedMs) throw new ConflictException("SPECTATOR_REVERSE_TIMESTAMP");
    if(last && (packet.sequence!==last.sequence+1 || packet.levelId!==last.levelId || packet.elapsedMs-last.elapsedMs>250))stream.historyContinuous=false;
    const cosmetic=packet.cosmetics??packet.keyframe.cosmetics;
    if(cosmetic){
      if(stream.cosmetics && (cosmetic.revision<stream.cosmetics.revision ||
          (cosmetic.revision===stream.cosmetics.revision && JSON.stringify(cosmetic)!==JSON.stringify(stream.cosmetics))))
        throw new ConflictException("STALE_COSMETIC_DESCRIPTOR");
      stream.cosmetics=cosmetic;
    }
    const buffered={...packet,receivedAtMs:nowMs};
    const size=(p:BufferedPacket)=>Buffer.byteLength(JSON.stringify(p));
    (stream.history??=[]).push(buffered);
    stream.historyBytes=(stream.historyBytes??stream.history.slice(0,-1).reduce((n,p)=>n+size(p),0))+size(buffered);
    if(stream.history.length>historyLimit)stream.historyBytes-=stream.history.splice(0,stream.history.length-historyLimit).reduce((n,p)=>n+size(p),0);
    // Trimming the oldest packets makes history[0].sequence > 0, which the read
    // path reports as HISTORY_WINDOW_EXPIRED (partial), never a full restore.
    while(stream.historyBytes>historyByteLimit&&stream.history.length>1)stream.historyBytes-=size(stream.history.shift()!);
    stream.lastSequence = packet.sequence;
    stream.lastReceivedAtMs = nowMs;
    stream.packets.push(buffered);
    stream.packets = stream.packets.filter((item) => item.receivedAtMs >= nowMs - SPECTATOR_LIVE_BUFFER_MS);
    if (stream.packets.length > SPECTATOR_LIVE_MAX_PACKETS) {
      stream.packets.splice(0, stream.packets.length - SPECTATOR_LIVE_MAX_PACKETS);
    }
  }

export function readSpectatorStream(_key: string, stream: LiveStream, roundNumber: number, deathmatchSequence: number | null,
    opponentName: string, attemptId?: string, generationText?: string,
    afterSequenceText?: string, worldHistory=false, cosmeticRevisionText?:string): SpectatorLiveSubscribeResponse {
    const requestedGeneration = queryInteger(generationText);
    const afterSequence = queryInteger(afterSequenceText);
    const hasStreamIdentity = Boolean(attemptId) && requestedGeneration !== null;
    const sameAttempt = attemptId === stream.attemptId;
    const sameGeneration = requestedGeneration === stream.generation;
    // A late join has no prior stream identity and must apply the bounded buffer
    // as a reset before switching to incremental live packets.
    const history=stream.history??stream.packets;
    const terminal=stream.packets.at(-1)?.keyframe;
    const interrupted=worldHistory && Boolean(terminal?.dead || terminal?.finished) &&
      hasStreamIdentity && sameAttempt && sameGeneration && afterSequence!==null && stream.lastSequence-afterSequence>SPECTATOR_HISTORY_PAGE_PACKETS;
    const available=!interrupted && history[0]?.sequence===0 && history[0].elapsedMs<=250 && stream.historyContinuous!==false;
    const source=worldHistory && available?history:stream.packets;
    const missedWindow = hasStreamIdentity && sameAttempt && sameGeneration && afterSequence !== null &&
      stream.lastSequence > afterSequence && (source[0]?.sequence ?? Infinity) > afterSequence + 1;
    const reset = interrupted || !hasStreamIdentity || !sameAttempt || !sameGeneration || missedWindow;
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
      return relayFallback("SEQUENCE_GAP");
    }
    if (
      effectiveAfter !== null &&
      effectiveAfter > stream.lastSequence
    ) {
      return relayFallback("SEQUENCE_GAP");
    }
    const packets = bufferedPackets.map(publicPacket);

    return {
      protocolVersion: SPECTATOR_LIVE_PROTOCOL_VERSION,
      active: true,
      waiting: false,
      fallback: false,
      reason: null,
      reset,
      ...(worldHistory?{restoration:{available,reason:interrupted?"ATTEMPT_ENDED_DURING_RESTORE":available?null:stream.historyContinuous===false?"HISTORY_GAP":history[0]?.sequence!==0?"HISTORY_WINDOW_EXPIRED":"ATTEMPT_START_MISSING",
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
        ...(stream.cosmetics && (reset || queryInteger(cosmeticRevisionText)!==stream.cosmetics.revision)?{cosmetics:stream.cosmetics}:{}),
      },
      packets,
    };
  }
