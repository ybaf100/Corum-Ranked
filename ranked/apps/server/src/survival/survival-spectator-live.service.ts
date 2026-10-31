import {BadRequestException,ConflictException,ForbiddenException,HttpException,Inject,Injectable,Logger} from '@nestjs/common';
import {survivalCurrentRound,survivalLiveView,type SurvivalMatch} from '@corum-ranked/rules';
import {DATABASE,type DatabasePort} from '../database/database.port.js';
import {SERVER_CLOCK,type ServerClock} from '../common/runtime.module.js';
import type {RankedSessionContext} from '../session/session.types.js';
import {sessionAliveSince} from '../session/session-presence.policy.js';
import {parseSpectatorPacket,type LiveStream} from '../match/spectator-live.protocol.js';
import {appendSpectatorPacket,readSpectatorStream,relayFallback} from '../match/spectator-relay.js';

// Serialized bytes are measured; JS object/allocator overhead is not a resident-memory guarantee.
export const SURVIVAL_RELAY_LIMITS={streams:8,perMatch:4,historyPackets:2400,streamBytes:4*1024*1024,totalBytes:24*1024*1024,viewers:256,rateBuckets:2048,demandMs:8000} as const;
interface Context {roundKey:string;mapGeneration:number;levelId:number;}
interface Stored {context:Context;targetId:string;sessionId:string;stream:LiveStream;bytes:number;traceLogs:number;}
interface Demand {matchId:string;targetId:string;roundKey:string;mapGeneration:number;expires:number;}
const object=(v:unknown):v is Record<string,unknown>=>typeof v==='object'&&v!==null&&!Array.isArray(v);
const same=(a:Context,b:Context)=>a.roundKey===b.roundKey&&a.mapGeneration===b.mapGeneration&&a.levelId===b.levelId;
@Injectable()
export class SurvivalSpectatorLiveService {
  private readonly logger=new Logger(SurvivalSpectatorLiveService.name);
  private readonly streams=new Map<string,Stored>();
  private readonly viewers=new Map<string,Demand>();
  private readonly rates=new Map<string,{at:number;count:number}>();
  public constructor(@Inject(DATABASE)private readonly db:DatabasePort,@Inject(SERVER_CLOCK)private readonly clock:ServerClock){}
  private now(){return this.clock.now().getTime();}
  private prune(){const now=this.now();for(const [key,v] of this.viewers)if(v.expires<=now)this.viewers.delete(key);
    for(const [key,s] of this.streams){const terminal=s.stream.packets.at(-1)?.keyframe;
      if(!this.demand(s.stream.matchId,s.targetId,s.context.roundKey,s.context.mapGeneration)||
        (now-s.stream.lastReceivedAtMs>10000&&!terminal?.dead&&!terminal?.finished))this.streams.delete(key);}
    for(const [key,r] of this.rates)if(now-r.at>10000)this.rates.delete(key);}
  public demand(matchId:string,targetId:string,roundKey:string,mapGeneration:number){
    const now=this.now();return [...this.viewers.values()].some(v=>v.expires>now&&v.matchId===matchId&&v.targetId===targetId&&v.roundKey===roundKey&&v.mapGeneration===mapGeneration);}
  public diagnostics(){this.prune();return {streams:this.streams.size,viewers:this.viewers.size,serializedBytes:[...this.streams.values()].reduce((n,s)=>n+s.bytes,0),limits:SURVIVAL_RELAY_LIMITS};}
  private async authority(id:string,session:RankedSessionContext,role:'viewer'|'publisher'){
    this.prune();const key=session.sessionId+':'+role,now=this.now(),rate=this.rates.get(key);
    if(!rate&&this.rates.size>=SURVIVAL_RELAY_LIMITS.rateBuckets)throw new HttpException('SURVIVAL_SPECTATOR_RATE_CAPACITY',429);
    if(!rate||now-rate.at>=1000)this.rates.set(key,{at:now,count:1});
    else if(++rate.count>80)throw new HttpException('SURVIVAL_SPECTATOR_RATE_LIMIT',429);
    const row=(await this.db.query<{snapshot:SurvivalMatch;participant_id:string;enabled:boolean}>(`SELECT m.snapshot,p.participant_id,
      COALESCE((SELECT enabled FROM ranked_runtime_features WHERE feature='live_spectator'),TRUE) AS enabled
      FROM sur_matches m JOIN sur_online_players p ON p.match_id=m.id
      JOIN ranked_sessions s ON s.id=$3 AND s.player_id=p.player_id
      WHERE m.id=$1 AND p.player_id=$2 AND p.admission_session_id=s.id AND p.acknowledged_at IS NULL
      AND s.revoked_at IS NULL AND s.expires_at>$4 AND s.last_heartbeat_at>$5`,
      [id,session.playerId,session.sessionId,new Date(now).toISOString(),sessionAliveSince(new Date(now))])).rows[0];
    if(!row||!row.enabled)throw new ForbiddenException('SURVIVAL_SPECTATOR_NOT_AUTHORIZED');
    const match=row.snapshot,round=survivalCurrentRound(match),own=match.players.find(p=>p.participantId===row.participant_id);
    if(!match.online||!own||own.kind!=='HUMAN')throw new ForbiddenException('SURVIVAL_SPECTATOR_NOT_AUTHORIZED');
    if(role==='viewer'){
      const participant=round.players.find(p=>p.participantId===own.participantId);
      if(own.eliminationStage!==null||match.state!=='PLAYING'||!participant||participant.forfeited||participant.absent||
        participant.attempts.some(a=>a.endedAt===null)||!participant.attempts.some(a=>a.endedAt!==null&&a.highestProgress===100))
        throw new ForbiddenException('SURVIVAL_SPECTATOR_CLEAR_REQUIRED');
    }
    if(role==='publisher'&&own.eliminationStage!==null)throw new ForbiddenException('SURVIVAL_PUBLISHER_ELIMINATED');
    return {match,round,own,context:{roundKey:round.key,mapGeneration:round.mapGeneration,levelId:round.map?.levelId??0}};
  }
  private check(context:Context,roundKey:unknown,generation:unknown,level:unknown){
    if(roundKey!==context.roundKey||Number(generation)!==context.mapGeneration||Number(level)!==context.levelId)throw new ConflictException('SURVIVAL_SPECTATOR_CONTEXT_CHANGED');}
  public async targets(id:string,session:RankedSessionContext){
    const {match,round,own,context}=await this.authority(id,session,'viewer');
    const view=survivalLiveView(match,this.now());
    const room=this.streams.size<SURVIVAL_RELAY_LIMITS.streams&&[...this.streams.values()].filter(s=>s.stream.matchId===id).length<SURVIVAL_RELAY_LIMITS.perMatch;
    return {context,matchId:id,state:match.state,roundNumber:round.ordinal,displayRoundNumber:round.ordinal,phase:round.phase,endsAt:round.endsAt,cutoff:round.cutoffScore,
      targets:match.players.filter(p=>p.eliminationStage===null&&p.participantId!==own.participantId).map(p=>{
        const state=view.standings.find(s=>s.participantId===p.participantId),rp=round.players.find(r=>r.participantId===p.participantId);
        const attempt=rp?.attempts.find(a=>a.endedAt===null);
        return {participantId:p.participantId,name:p.name,gdAccountId:p.gdAccountId??null,rank:state?.rank??null,
          best:state?.bestProgress??0,now:state?.currentProgress??0,kind:p.kind,
          status:p.kind==='BOT'?'NO_NATIVE_STREAM':!room&&!this.streams.has(id+':'+p.participantId)?'CAPACITY':attempt?'PLAYING':state?.cleared?'CLEARED':'WAITING',
          selectable:p.kind==='HUMAN'&&match.state==='PLAYING'&&(room||this.streams.has(id+':'+p.participantId)),attemptId:attempt?.id??null};})};
  }
  public async publish(id:string,session:RankedSessionContext,body:unknown){
    const {match,round,own,context}=await this.authority(id,session,'publisher');
    if(!object(body)||!Array.isArray(body.packets)||body.packets.length<1||body.packets.length>16)throw new BadRequestException('SPECTATOR_BATCH_SIZE');
    this.check(context,body.roundKey,body.mapGeneration,body.levelId);
    if(match.state!=='PLAYING'||round.endsAt===null||this.now()>=round.endsAt)throw new ConflictException('SURVIVAL_ROUND_ENDED');
    if(!this.demand(id,own.participantId,round.key,round.mapGeneration))throw new ConflictException('SURVIVAL_NO_VIEWERS');
    const packets=body.packets.map(p=>parseSpectatorPacket(p,id)),first=packets[0]!,last=packets.at(-1)!;
    const participant=round.players.find(p=>p.participantId===own.participantId);
    const attempt=participant?.attempts.find(a=>a.id===first.attemptId);
    if(!attempt||participant?.attempts.at(-1)?.id!==first.attemptId||participant?.forfeited||participant?.absent||
      (attempt.endedAt!==null&&this.now()-attempt.endedAt>1500))throw new ConflictException('SURVIVAL_ATTEMPT_NOT_ACCEPTED');
    if(attempt.endedAt!==null&&(last.keyframe.dead||last.keyframe.finished)&&(last.keyframe.finished!==(attempt.highestProgress===100)))throw new ConflictException('SURVIVAL_TERMINAL_MISMATCH');
    const key=id+':'+own.participantId,existing=this.streams.get(key);
    for(let i=0;i<packets.length;++i){const p=packets[i]!,prev=packets[i-1];
      if(p.protocolVersion!==2||p.roundNumber!==round.ordinal||p.deathmatchSequence!==null||p.levelId!==context.levelId||p.attemptId!==first.attemptId||p.generation!==first.generation||
        (prev&&(p.sequence!==prev.sequence+1||p.elapsedMs<prev.elapsedMs||prev.keyframe.dead||prev.keyframe.finished)))throw new ConflictException('SURVIVAL_PACKET_CONTEXT');
      if(Buffer.byteLength(JSON.stringify(p))>8192)throw new BadRequestException('SURVIVAL_PACKET_SIZE');}
    let stream:LiveStream;
    const reset=!existing||!same(existing.context,context)||existing.sessionId!==session.sessionId||existing.stream.attemptId!==first.attemptId||existing.stream.generation!==first.generation||this.now()-existing.stream.lastReceivedAtMs>1500;
    if(reset){
      if(first.sequence!==0)throw new ConflictException('SPECTATOR_STREAM_MUST_START_AT_SEQUENCE_ZERO');
      if(existing&&existing.stream.attemptId===first.attemptId&&first.generation<existing.stream.generation)throw new ConflictException('SPECTATOR_STALE_GENERATION');
      if(!existing&&(this.streams.size>=SURVIVAL_RELAY_LIMITS.streams||[...this.streams.values()].filter(s=>s.stream.matchId===id).length>=SURVIVAL_RELAY_LIMITS.perMatch))throw new ConflictException('SURVIVAL_STREAM_LIMIT');
      stream={matchId:id,slot:round.ordinal,side:'A',attemptId:first.attemptId,generation:first.generation,lastSequence:-1,lastReceivedAtMs:this.now(),packets:[]};
    }else stream={...existing.stream,packets:[...existing.stream.packets],history:[...(existing.stream.history??[])]};
    let traceLogs=reset?0:existing!.traceLogs;
    const initialSequence=stream.lastSequence,oldSamples=stream.history??[];
    for(const p of packets){
      if(p.sequence<=stream.lastSequence){const prior=stream.history?.find(v=>v.sequence===p.sequence);if(!prior)throw new ConflictException('SPECTATOR_STALE_SEQUENCE');
        const {receivedAtMs:_,...plain}=prior;if(JSON.stringify(plain)!==JSON.stringify(p))throw new ConflictException('SPECTATOR_CHANGED_RETRY');continue;}
      if(p.sequence!==stream.lastSequence+1)throw new ConflictException('SPECTATOR_SEQUENCE_GAP');
      appendSpectatorPacket(stream,p,this.now(),SURVIVAL_RELAY_LIMITS.historyPackets,Number.POSITIVE_INFINITY); // Survival keeps its own byte accounting below
      if(context.levelId===143308401&&p.elapsedMs>=70000&&p.elapsedMs<=73000&&traceLogs++<64)
        this.logger.log(JSON.stringify({stage:'RELAY_ACCEPTED',attemptId:p.attemptId,generation:p.generation,sequence:p.sequence,elapsedMs:p.elapsedMs,p1:p.keyframe.discontinuity??null,p2:p.keyframe.player2?.discontinuity??null}));
    }
    const retained=stream.history??[];
    const charge=(p:unknown)=>Buffer.byteLength(JSON.stringify(p));
    let bytes=(reset?0:existing!.bytes)+retained.filter(p=>p.sequence>initialSequence).reduce((n,p)=>n+charge(p),0);
    const firstRetained=retained[0]?.sequence??Infinity;
    bytes-=oldSamples.filter(p=>p.sequence<firstRetained).reduce((n,p)=>n+charge(p),0);
    while(bytes>SURVIVAL_RELAY_LIMITS.streamBytes&&retained.length>64){bytes-=retained.splice(0,64).reduce((n,p)=>n+charge(p),0);}
    const total=[...this.streams.entries()].reduce((n,[k,s])=>n+(k===key?0:s.bytes),bytes);
    if(total>SURVIVAL_RELAY_LIMITS.totalBytes)throw new ConflictException('SURVIVAL_RELAY_BUDGET');
    this.streams.set(key,{context,targetId:own.participantId,sessionId:session.sessionId,stream,bytes,traceLogs});
    return {accepted:true,protocolVersion:2,attemptId:first.attemptId,generation:first.generation,sequence:stream.lastSequence};
  }
  public async subscribe(id:string,session:RankedSessionContext,q:Record<string,string|undefined>){
    const {match,round,own,context}=await this.authority(id,session,'viewer');
    this.check(context,q['roundKey'],q['mapGeneration'],q['levelId']);
    if(match.state!=='PLAYING'||round.endsAt===null||this.now()>=round.endsAt)throw new ConflictException('SURVIVAL_ROUND_ENDED');
    const target=match.players.find(p=>p.participantId===q['targetId']);
    if(!target||target.participantId===own.participantId||target.eliminationStage!==null||target.kind!=='HUMAN')throw new ForbiddenException('SURVIVAL_TARGET_UNAVAILABLE');
    if(q['protocolVersion']!=='2')throw new ConflictException('SPECTATOR_PROTOCOL_VERSION_MISMATCH');
    const prior=this.viewers.get(session.sessionId);
    if(prior?.targetId!==target.participantId){this.viewers.delete(session.sessionId);this.prune();}
    if(!this.streams.has(id+':'+target.participantId)&&(this.streams.size>=SURVIVAL_RELAY_LIMITS.streams||[...this.streams.values()].filter(s=>s.stream.matchId===id).length>=SURVIVAL_RELAY_LIMITS.perMatch))throw new ConflictException('SURVIVAL_STREAM_LIMIT');
    if(!this.viewers.has(session.sessionId)&&this.viewers.size>=SURVIVAL_RELAY_LIMITS.viewers)throw new ConflictException('SURVIVAL_VIEWER_LIMIT');
    this.viewers.set(session.sessionId,{matchId:id,targetId:target.participantId,roundKey:round.key,mapGeneration:round.mapGeneration,expires:this.now()+SURVIVAL_RELAY_LIMITS.demandMs});
    const attempt=round.players.find(p=>p.participantId===target.participantId)?.attempts.at(-1);
    const stored=this.streams.get(id+':'+target.participantId);
    if(!stored||!same(stored.context,context)||stored.stream.attemptId!==attempt?.id){return {...relayFallback('WAITING_FOR_PLAYER'),fallback:false,waiting:true,context,targetId:target.participantId};}
    if(this.now()-stored.stream.lastReceivedAtMs>1500&&!stored.stream.packets.at(-1)?.keyframe.dead&&!stored.stream.packets.at(-1)?.keyframe.finished)return relayFallback('STREAM_TIMEOUT');
    return {...readSpectatorStream('',stored.stream,round.ordinal,null,target.name,q['attemptId'],q['generation'],q['afterSequence'],q['worldHistory']==='1',q['cosmeticRevision']),context,targetId:target.participantId};
  }
  public async leave(id:string,session:RankedSessionContext){await this.authority(id,session,'viewer');const demand=this.viewers.get(session.sessionId);if(demand?.matchId===id)this.viewers.delete(session.sessionId);this.prune();return {ok:true};}
}
