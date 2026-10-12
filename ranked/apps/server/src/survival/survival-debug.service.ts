import { BadRequestException, ConflictException, ForbiddenException, Inject, Injectable, Logger, NotFoundException,
  type OnApplicationBootstrap, type OnApplicationShutdown } from "@nestjs/common";
import { timingSafeEqual } from "node:crypto";
import { forceSurvivalDebugRank, finishSurvivalDebugOnElimination, reduceSurvivalMatch, survivalBotPlan, survivalBotTiebreak, survivalCurrentRound,
  survivalDebugLp, survivalLiveView, survivalSettlementInputs, type RandomSource, type SurvivalCommand,
  type SurvivalMatch, type SurvivalRatingProfile } from "@corum-ranked/rules";
import { DATABASE, type DatabasePort, type SqlExecutor } from "../database/database.port.js";
import { RANDOM_SOURCE, SERVER_CLOCK, type ServerClock } from "../common/runtime.module.js";
import { SERVER_ENVIRONMENT, type ServerEnvironment } from "../config/server-environment.js";
import { SurvivalConfigService } from "../config/survival-config.service.js";
import { SessionService } from "../session/session.service.js";
import { sessionAliveSince } from "../session/session-presence.policy.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { SurvivalEngineService, type SurvivalAdmissionPlayer } from "./survival-engine.service.js";
import { SurvivalService } from "./survival.service.js";
import type { CreateSurvivalDebugDto, ForceSurvivalDebugDto, SurvivalDebugEventDto } from "./survival-debug.dto.js";

interface DebugRow {
  match_id:string; human_player_id:string; seed:number|string; average_group:string; force_rank:number|null;
  driver_state:Record<string,number>; result:SurvivalDebugResult|null; acknowledged_at:Date|string|null;
}
export interface SurvivalDebugResult {
  matchId:string; debug:true; rank:number; tiedPlayers:number; requestedDelta:number; actualDelta:number;
  profileBefore:SurvivalRatingProfile; profileAfter:SurvivalRatingProfile;
  promoted:boolean; demoted:boolean; protected:boolean; warningRecoveryLp:number|null;
  bands:{tier:string;minimum:number}[]; standings:(ReturnType<typeof survivalSettlementInputs>[number] & {name:string;kind:string})[];
  completedAt:string; completionKind:string;
}
const terminal=(m:SurvivalMatch)=>["FINAL_RESULT","CANCELLED"].includes(m.state);

@Injectable()
export class SurvivalDebugService implements OnApplicationBootstrap,OnApplicationShutdown {
  private readonly logger=new Logger(SurvivalDebugService.name);
  private timer:ReturnType<typeof setInterval>|null=null;
  private pending:Promise<void>|null=null;
  public constructor(@Inject(DATABASE) private readonly db:DatabasePort,
    @Inject(SERVER_ENVIRONMENT) private readonly env:ServerEnvironment,
    @Inject(SERVER_CLOCK) private readonly clock:ServerClock,
    @Inject(RANDOM_SOURCE) private readonly random:RandomSource,
    private readonly config:SurvivalConfigService, private readonly sessions:SessionService,
    private readonly engine:SurvivalEngineService, private readonly survival:SurvivalService) {
    engine.registerAdapter((tx,match,now)=>this.drive(tx,match,now));
  }
  public onApplicationBootstrap():void {
    // Existing games survive operational OFF and a process restart; the seed and
    // event cursors live in PostgreSQL, never just in this interval's memory.
    this.timer=setInterval(()=>{void this.tickOnce().catch(e=>this.logger.error(String(e)));},250);
    this.timer.unref();
  }
  public async onApplicationShutdown():Promise<void> { if(this.timer)clearInterval(this.timer);this.timer=null;await this.pending; }
  private password(session:RankedSessionContext,value:string):void {
    const expected=this.env.debugBot?.password;
    if(!expected)throw new NotFoundException("Debug Bot Match is disabled");
    if(session.clientBuildChannel!=="debug")throw new ForbiddenException("Survival debug requires the debug client");
    const a=Buffer.from(value),b=Buffer.from(expected);
    if(a.length!==b.length || !timingSafeEqual(a,b))throw new ForbiddenException("Invalid debug password");
  }
  private async owned(id:string,session:RankedSessionContext):Promise<DebugRow> {
    const row=(await this.db.query<DebugRow>("SELECT * FROM sur_debug_matches WHERE match_id=$1",[id])).rows[0];
    if(!row)throw new NotFoundException("Survival debug match not found");
    if(row.human_player_id!==session.playerId || session.clientBuildChannel!=="debug")throw new ForbiddenException("Not the debug match owner");
    return row;
  }
  public async create(session:RankedSessionContext,body:CreateSurvivalDebugDto) {
    this.password(session,body.password);this.sessions.assertEnvironment(body.installedMods);
    await this.survival.profile(session.playerId);
    const creationKey=`sur-debug:${session.playerId}:${body.requestId}`;
    const match=await this.config.withAdmission(()=>this.db.transaction(async tx=>{
      await tx.query("SELECT id FROM ranked_players WHERE id=$1 FOR UPDATE",[session.playerId]);
      const now=this.clock.now();
      const live=await tx.query(`SELECT id FROM ranked_sessions WHERE id=$1 AND player_id=$2 AND revoked_at IS NULL
        AND expires_at>$3 AND last_heartbeat_at>$4`,[session.sessionId,session.playerId,now.toISOString(),sessionAliveSince(now)]);
      if(!live.rows.length)throw new ForbiddenException("Debug session expired");
      const previous=(await tx.query<{snapshot:SurvivalMatch;average_group:string;force_rank:number|null}>(
        `SELECT m.snapshot,d.average_group,d.force_rank FROM sur_matches m JOIN sur_debug_matches d ON d.match_id=m.id WHERE m.creation_key=$1`,[creationKey])).rows[0];
      if(previous) {
        if(previous.average_group!==body.averageGroup || previous.force_rank!==(body.forceRank??null))throw new ConflictException("Debug creation payload changed");
        return previous.snapshot;
      }
      const busy=await tx.query(`SELECT 1 FROM ranked_queue_entries WHERE player_id=$1 AND status='QUEUED'
        UNION ALL SELECT 1 FROM ranked_matches WHERE (player_a_id=$1 OR player_b_id=$1) AND state NOT IN ('MATCH_RESULT','CANCELLED')
        UNION ALL SELECT 1 FROM ranked_placement_runs WHERE player_id=$1 AND state NOT IN ('PLACEMENT_RESULT','CANCELLED')
        UNION ALL SELECT 1 FROM sur_match_players WHERE player_id=$1 AND status='ACTIVE' LIMIT 1`,[session.playerId]);
      if(busy.rows.length)throw new ConflictException("Finish or recover the current game first");
      const profile=(await tx.query<{lp:number}>("SELECT lp FROM sur_profiles WHERE player_id=$1 FOR UPDATE",[session.playerId])).rows[0]!;
      const seed=Math.floor(this.random.next()*0x100000000),captured=this.config.getSnapshot();
      const lp=survivalDebugLp(seed,Number(profile.lp),body.averageGroup,captured.config);
      const players:SurvivalAdmissionPlayer[]=[{participantId:"human",kind:"HUMAN",playerId:session.playerId},
        ...lp.bots.map((value,i)=>({participantId:`bot-${String(i+1).padStart(2,"0")}`,kind:"BOT" as const,name:`Bot ${i+1}`,lp:value}))];
      const created=await this.engine.createWithin(tx,creationKey,players,captured,lp.humanSnapshotLp,true);
      await tx.query(`INSERT INTO sur_debug_matches(match_id,human_player_id,admission_session_id,seed,average_group,
        virtual_human_lp,human_actual_lp,force_rank,last_seen_at,created_at) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$9)`,
        [created.id,session.playerId,session.sessionId,seed,body.averageGroup,lp.virtualHuman,profile.lp,body.forceRank??null,now.toISOString()]);
      return created;
    }));
    return this.view(match.id,session);
  }
  private async drive(tx:SqlExecutor,before:SurvivalMatch,now:number):Promise<SurvivalMatch> {
    if(terminal(before))return before;
    const row=(await tx.query<DebugRow>("SELECT * FROM sur_debug_matches WHERE match_id=$1",[before.id])).rows[0];
    if(!row)return before;
    let match=before;
    const human=match.players.find(p=>p.playerId===row.human_player_id)!;
    if(row.force_rank!==null)return forceSurvivalDebugRank(match,human.participantId,row.force_rank,now);
    if(human.eliminationStage!==null)return finishSurvivalDebugOnElimination(match,human.participantId,now);
    const round=survivalCurrentRound(match),own=round.players.find(p=>p.participantId===human.participantId);
    const bots=match.players.filter(p=>p.kind==="BOT");
    const active=bots.filter(p=>round.players.some(r=>r.participantId===p.participantId && !r.forfeited));
    const context={roundKey:round.key,mapGeneration:round.mapGeneration,levelId:round.map?.levelId??0};
    const apply=(command:SurvivalCommand,at=now)=>{match=reduceSurvivalMatch(match,command,Math.max(match.updatedAt,at));};
    // Simulated participants certify resources/entry only after the actual human
    // does. They cannot start the 5s/10s clock ahead of the real native scene or
    // turn a failed human-only download into a misleading ready participant.
    if(match.state==="PREPARING" && own?.preparationFailure && own.assetsReadyAt===null) {
      for(const p of active)apply({type:"PREPARATION_FAILED",...context,participantId:p.participantId,reason:own.preparationFailure});
    } else if(match.state==="PREPARING" && own?.assetsReadyAt!==null && own?.assetsReadyAt!==undefined) {
      for(const p of active)apply({type:"ASSETS_READY",...context,participantId:p.participantId,certificate:round.map!});
    }
    if(match.state==="ENTERING" && own?.enteredAt!==null && own?.enteredAt!==undefined) {
      for(const p of active)apply({type:"ENTERED",...context,participantId:p.participantId});
    }
    const cursor={...row.driver_state};
    if(match.state==="PLAYING" && round.startsAt!==null && round.endsAt!==null) {
      const at=Math.min(now,round.endsAt-1);
      for(const p of active) {
        const index=bots.findIndex(b=>b.participantId===p.participantId),key=`${round.key}:${round.mapGeneration}:${p.participantId}`;
        const used=cursor[key]??-1;
        if(round.phase==="TIEBREAK") {
          if(used>=2)continue;
          for(const [attemptIndex,progress] of survivalBotTiebreak(Number(row.seed),`${round.key}:${round.mapGeneration}`,index).entries()) {
            if(attemptIndex<=used)continue;
            const attemptId=`bot-dm-${attemptIndex+1}`;
            apply({type:"ATTEMPT_START",...context,participantId:p.participantId,attemptId},at);
            apply({type:"ATTEMPT_END",...context,participantId:p.participantId,attemptId,progress},at);
            cursor[key]=attemptIndex;
          }
        } else {
          const events=survivalBotPlan(Number(row.seed),`${round.key}:${round.mapGeneration}`,index).filter(e=>e.offsetMs<=now-round.startsAt! && e.index>used);
          for(const event of events) {
            if(used<0 && event.index===0)apply({type:"ATTEMPT_START",...context,participantId:p.participantId,attemptId:"bot-normal"},at);
            apply({type:"PROGRESS",...context,participantId:p.participantId,attemptId:"bot-normal",progress:event.best},at);
            cursor[key]=event.index;
          }
        }
      }
    }
    if(JSON.stringify(cursor)!==JSON.stringify(row.driver_state))await tx.query("UPDATE sur_debug_matches SET driver_state=$2::jsonb WHERE match_id=$1",[match.id,JSON.stringify(cursor)]);
    return match;
  }
  public async view(id:string,session:RankedSessionContext) {
    await this.owned(id,session);
    await this.db.query("UPDATE sur_debug_matches SET last_seen_at=$2 WHERE match_id=$1",[id,this.clock.now().toISOString()]);
    const match=await this.engine.tick(id);
    this.engine.ensureMapSelection(match);
    const mapSelection=match.state==="MAP_SELECTION"?await this.engine.mapSelectionStatus(id):null;
    const result=await this.settle(match);
    const own=survivalCurrentRound(match).players.find(p=>p.participantId==="human");
    const active=own?.attempts.find(a=>a.endedAt===null);
    return {...survivalLiveView(match,this.clock.now().getTime(),"human"),debug:true,myParticipantId:"human",mapSelection,
      myActiveAttemptId:active?.id??null,myActiveAttemptProgress:active?.highestProgress??0,result};
  }
  public async active(session:RankedSessionContext) {
    if(session.clientBuildChannel!=="debug")return {match:null};
    const row=(await this.db.query<{id:string}>(`SELECT m.id FROM sur_matches m JOIN sur_debug_matches d ON d.match_id=m.id
      WHERE d.human_player_id=$1 AND d.acknowledged_at IS NULL ORDER BY m.created_at DESC LIMIT 1`,[session.playerId])).rows[0];
    return {match:row?await this.view(row.id,session):null};
  }
  public async event(id:string,session:RankedSessionContext,body:SurvivalDebugEventDto) {
    await this.owned(id,session);
    if(["ASSETS_READY","ENTERED","ATTEMPT_START"].includes(body.type))this.sessions.assertEnvironment(body.installedMods??[]);
    const context={roundKey:body.roundKey,mapGeneration:body.mapGeneration,levelId:body.levelId,participantId:"human"};
    let command:SurvivalCommand;
    if(body.type==="PRESENTATION_COMPLETE")command={type:body.type,roundKey:body.roundKey,mapGeneration:body.mapGeneration,participantId:"human"};
    else if(body.type==="ASSETS_READY") {if(!body.certificate)throw new BadRequestException("Map certificate required");command={type:body.type,...context,certificate:body.certificate};}
    else if(body.type==="PREPARATION_FAILED")command={type:body.type,...context,reason:body.reason??"NATIVE_PREPARATION_FAILED"};
    else if(body.type==="ENTERED")command={type:body.type,...context};
    else if(body.type==="FORFEIT")command={type:body.type,roundKey:body.roundKey,mapGeneration:body.mapGeneration,participantIds:["human"],reason:"DEBUG_HUMAN_SURRENDER"};
    else {
      if(!body.attemptId)throw new BadRequestException("Attempt ID required");
      if(body.type==="ATTEMPT_START")command={type:body.type,...context,attemptId:body.attemptId};
      else {if(body.progress===undefined)throw new BadRequestException("Progress required");command={type:body.type,...context,attemptId:body.attemptId,progress:body.progress};}
    }
    await this.engine.dispatch(id,`human:${body.eventId}`,command,session);
    return this.view(id,session);
  }
  public async force(id:string,session:RankedSessionContext,body:ForceSurvivalDebugDto) {
    this.password(session,body.password);await this.owned(id,session);
    if(!Number.isInteger(body.rank)||body.rank<1||body.rank>16)throw new BadRequestException("Force rank must be 1..16");
    await this.db.transaction(async tx=>{
      await tx.query("SELECT id FROM ranked_players WHERE id=$1 FOR UPDATE",[session.playerId]);
      const now=this.clock.now();
      const live=await tx.query(`SELECT id FROM ranked_sessions WHERE id=$1 AND player_id=$2 AND revoked_at IS NULL
        AND expires_at>$3 AND last_heartbeat_at>$4`,[session.sessionId,session.playerId,now.toISOString(),sessionAliveSince(now)]);
      if(!live.rows.length)throw new ForbiddenException("Debug session expired");
      const match=(await tx.query<{state:string}>("SELECT state FROM sur_matches WHERE id=$1 FOR UPDATE",[id])).rows[0];
      if(match && !["FINAL_RESULT","CANCELLED"].includes(match.state))await tx.query(`UPDATE sur_debug_matches SET force_rank=$2 WHERE match_id=$1 AND result IS NULL`,[id,body.rank]);
    });
    return this.view(id,session);
  }
  public async acknowledge(id:string,session:RankedSessionContext) {
    await this.owned(id,session);
    await this.db.query("UPDATE sur_debug_matches SET acknowledged_at=$2 WHERE match_id=$1 AND settled_at IS NOT NULL",[id,this.clock.now().toISOString()]);
    return {ok:true};
  }
  public tickOnce():Promise<void> {
    if(this.pending)return this.pending;
    this.pending=(async()=>{
      const rows=await this.db.query<{match_id:string}>("SELECT match_id FROM sur_debug_matches WHERE settled_at IS NULL ORDER BY created_at");
      for(const row of rows.rows)try {await this.settle(await this.engine.tick(row.match_id));}catch(e){this.logger.error(`Survival debug ${row.match_id}: ${String(e)}`);}
    })().finally(()=>{this.pending=null;});return this.pending;
  }
  private async settle(match:SurvivalMatch):Promise<SurvivalDebugResult|null> {
    if(match.state!=="FINAL_RESULT")return null;
    const row=(await this.db.query<DebugRow>("SELECT * FROM sur_debug_matches WHERE match_id=$1",[match.id])).rows[0];
    if(!row)return null;if(row.result)return row.result;
    const inputs=survivalSettlementInputs(match),human=inputs.find(p=>p.playerId===row.human_player_id)!;
    const rating=await this.survival.settleLp(row.human_player_id,human.eventKey,human.delta,{generation:match.configGeneration,config:match.config});
    const ledger=(await this.db.query<{lp_before:number;tier_before:string;warning_before:boolean}>(
      "SELECT lp_before,tier_before,warning_before FROM sur_lp_ledger WHERE player_id=$1 AND event_key=$2",[row.human_player_id,human.eventKey])).rows[0]!;
    const groups=new Set<string>();
    const bands=match.config.tierBands.filter(b=>{if(groups.has(b.group))return false;groups.add(b.group);return true;}).map(b=>({tier:b.group,minimum:b.minimumLp}));
    const result:SurvivalDebugResult={matchId:match.id,debug:true,rank:human.rank,tiedPlayers:human.tiedPlayers,requestedDelta:human.delta,
      actualDelta:rating.actualDelta,profileBefore:{lp:Number(ledger.lp_before),tier:ledger.tier_before,demotionWarning:ledger.warning_before},
      profileAfter:rating.profile,promoted:rating.promoted,demoted:rating.demoted,protected:rating.protected,
      warningRecoveryLp:rating.warningRecoveryLp,bands,standings:inputs.map(p=>{const participant=match.players.find(v=>v.participantId===p.participantId)!;
        return {...p,name:participant.name,kind:participant.kind};}),completedAt:new Date(match.finishedAt!).toISOString(),completionKind:match.completionKind??"NORMAL"};
    await this.db.transaction(async tx=>{
      // The ledger's unique key closes a crash between payment and this result
      // projection. Replaying settlement restores the same rating/result.
      await tx.query("UPDATE sur_debug_matches SET result=$2::jsonb,settled_at=$3 WHERE match_id=$1 AND result IS NULL",[match.id,JSON.stringify(result),this.clock.now().toISOString()]);
      for(const p of inputs)await tx.query("UPDATE sur_match_players SET result_lp_delta=$3 WHERE match_id=$1 AND participant_id=$2",[match.id,p.participantId,p.delta]);
    });
    return result;
  }
}
