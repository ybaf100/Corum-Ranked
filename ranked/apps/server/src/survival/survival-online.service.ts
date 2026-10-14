import { SurvivalConfigService } from "../config/survival-config.service.js";
import { survivalQueueView } from "./survival-queue.view.js";
import { BadRequestException,ConflictException,ForbiddenException,Inject,Injectable,Logger,type OnApplicationBootstrap,type OnApplicationShutdown } from "@nestjs/common";
import { reduceSurvivalMatch,survivalCurrentRound,survivalLiveView,survivalMarkAbsent,survivalSettlementInputs,
  survivalStabilizeOnlineRanks,finishSurvivalDebugOnElimination,survivalTierForLp,RankedDomainError,type SurvivalMatch,type SurvivalCommand,type SurvivalRatingProfile } from "@corum-ranked/rules";
import { DATABASE,type DatabasePort,type SqlExecutor } from "../database/database.port.js";
import { SERVER_CLOCK,type ServerClock } from "../common/runtime.module.js";
import { SessionService } from "../session/session.service.js";
import { sessionAliveSince } from "../session/session-presence.policy.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { SurvivalEngineService } from "./survival-engine.service.js";
import { SurvivalService } from "./survival.service.js";
import { driveSurvivalBots } from "./survival-bot-driver.js";
import type { SurvivalOnlineEventDto } from "./survival-online.dto.js";

interface OnlineRow {match_id:string;bot_filled:boolean;seed:number|string;driver_state:Record<string,number>;}
interface PresenceRow {participant_id:string;last_seen_at:Date|string;observed_round_key:string|null;observed_map_generation:number|null;recovering:boolean;}
interface ResultRow {participant_id:string;player_id:string;result:SurvivalOnlineResult|null;}
export interface SurvivalOnlineResult {
  matchId:string;debug:false;botFilled:boolean;rank:number;tiedPlayers:number;won?:boolean;requestedDelta:number;actualDelta:number;
  profileBefore:SurvivalRatingProfile;profileAfter:SurvivalRatingProfile;
  promoted:boolean;demoted:boolean;protected:boolean;warningRecoveryLp:number|null;
  bands:{tier:string;minimum:number}[];
  standings:{participantId:string;playerId:string|null;rank:number|null;name:string;kind:string;gdAccountId:string|null;displayedTier:string;delta:number|null}[];
  completedAt:string;completionKind:string;finalStandings?:boolean;
}
const terminal=(m:SurvivalMatch)=>["FINAL_RESULT","CANCELLED"].includes(m.state);
@Injectable()
export class SurvivalOnlineService implements OnApplicationBootstrap,OnApplicationShutdown {
  private readonly logger=new Logger(SurvivalOnlineService.name);
  private timer:ReturnType<typeof setInterval>|null=null;
  private pending:Promise<void>|null=null;
  public constructor(@Inject(DATABASE)private readonly db:DatabasePort,@Inject(SERVER_CLOCK)private readonly clock:ServerClock,
    private readonly sessions:SessionService,private readonly engine:SurvivalEngineService,private readonly survival:SurvivalService,private readonly config:SurvivalConfigService){
    engine.registerAdapter((tx,m,now)=>this.drive(tx,m,now));
  }
  public onApplicationBootstrap():void {this.timer=setInterval(()=>{void this.tickOnce().catch(e=>this.logger.error(String(e)));},500);this.timer.unref();}
  public async onApplicationShutdown():Promise<void>{if(this.timer)clearInterval(this.timer);this.timer=null;await this.pending;}
  private async owned(id:string,session:RankedSessionContext):Promise<ResultRow>{
    const row=(await this.db.query<ResultRow>("SELECT * FROM sur_online_players WHERE match_id=$1 AND player_id=$2",[id,session.playerId])).rows[0];
    if(!row)throw new ForbiddenException("Not the online Survival participant");return row;
  }
  private async actor(tx:SqlExecutor,session:RankedSessionContext):Promise<void>{
    await tx.query("SELECT id FROM ranked_players WHERE id=$1 FOR UPDATE",[session.playerId]);
    const now=this.clock.now();
    const live=await tx.query("SELECT id FROM ranked_sessions WHERE id=$1 AND player_id=$2 AND revoked_at IS NULL AND expires_at>$3 AND last_heartbeat_at>$4",
      [session.sessionId,session.playerId,now.toISOString(),sessionAliveSince(now)]);
    if(!live.rows.length)throw new ForbiddenException("Survival session expired; reconnect");
  }
  private async drive(tx:SqlExecutor,before:SurvivalMatch,now:number):Promise<SurvivalMatch>{
    if(!before.online||terminal(before))return before;
    const row=(await tx.query<OnlineRow>("SELECT * FROM sur_online_matches WHERE match_id=$1",[before.id])).rows[0];
    if(!row)return before;let match=before;let round=survivalCurrentRound(match);
    // Five-second rosters belong to the shared server clock; a disconnected or
    // eliminated client cannot hold all other players at a presentation barrier.
    const barrierStart=match.presentationGate==="INTRO"?match.createdAt:round.finishedAt;
    if(match.presentationGate && barrierStart!==null && now>=barrierStart+5000){
      const owner=match.players.find(p=>p.kind==="HUMAN"&&p.eliminationStage===null);
      if(owner)match=reduceSurvivalMatch(match,{type:"PRESENTATION_COMPLETE",roundKey:round.key,mapGeneration:round.mapGeneration,participantId:owner.participantId},now);
    }
    round=survivalCurrentRound(match);
    const deadline=match.state==="PREPARING" && round.preparationStartedAt!==null?round.preparationStartedAt+15000:
      match.state==="ENTERING" && round.entryRequestedAt!==null?(round.firstEnteredAt===null?round.entryRequestedAt+5000:round.firstEnteredAt+10000):null;
    if(deadline!==null){
      const presence=(await tx.query<PresenceRow>("SELECT participant_id,last_seen_at,observed_round_key,observed_map_generation,recovering FROM sur_online_players WHERE match_id=$1",[match.id])).rows;
      const absent=round.players.filter(p=>!p.forfeited && !p.absent && match.players.some(e=>e.participantId===p.participantId&&e.kind==="HUMAN") &&
        (match.state==="PREPARING"?p.assetsReadyAt===null:p.enteredAt===null)).filter(p=>{
        const seen=presence.find(v=>v.participant_id===p.participantId);
        return !!seen?.recovering || !seen || now-new Date(seen.last_seen_at).getTime()>10000 ||
          (now>=deadline && (seen.observed_round_key!==round.key || seen.observed_map_generation!==round.mapGeneration));
      }).map(p=>p.participantId);
      if(absent.length)match=survivalMarkAbsent(match,absent,now);
    }
    const driven=driveSurvivalBots(match,Number(row.seed),row.driver_state,now);match=driven.match;
    if(JSON.stringify(driven.cursor)!==JSON.stringify(row.driver_state))await tx.query("UPDATE sur_online_matches SET driver_state=$2::jsonb WHERE match_id=$1",[match.id,JSON.stringify(driven.cursor)]);
    if(row.bot_filled && !terminal(match) && !match.players.some(p=>p.kind==="HUMAN"&&p.eliminationStage===null)){
      const human=match.players.find(p=>p.kind==="HUMAN")!;
      match=finishSurvivalDebugOnElimination(match,human.participantId,now);
      match.completionKind="BOTS_ONLY_RESULT";survivalStabilizeOnlineRanks(match);
      match.winnerIds=match.players.filter(p=>p.kind==="BOT"&&p.finalRank===1).map(p=>p.participantId);match.winnerId=match.winnerIds[0]??null;
    }
    return match;
  }
  public async view(id:string,session:RankedSessionContext,observedRound?:string,observedGeneration?:number){
    const own=await this.owned(id,session);
    await this.db.transaction(async tx=>{
      await this.actor(tx,session);
      const current=survivalCurrentRound((await tx.query<{snapshot:SurvivalMatch}>("SELECT snapshot FROM sur_matches WHERE id=$1",[id])).rows[0]!.snapshot);
      const observed=observedRound===current.key && observedGeneration===current.mapGeneration;
      await tx.query(`UPDATE sur_online_players SET last_seen_at=$3,
        observed_round_key=COALESCE($4,observed_round_key),observed_map_generation=COALESCE($5,observed_map_generation)
        WHERE match_id=$1 AND player_id=$2`,[id,session.playerId,this.clock.now().toISOString(),observed?observedRound:null,observed?observedGeneration:null]);
    });
    const match=await this.engine.tick(id);this.engine.ensureMapSelection(match);await this.settle(match);
    const result=(await this.owned(id,session)).result;
    const round=survivalCurrentRound(match),participant=round.players.find(p=>p.participantId===own.participant_id);
    const attempt=participant?.attempts.find(a=>a.endedAt===null);
    return {...survivalLiveView(match,this.clock.now().getTime(),own.participant_id),debug:false,botFilled:!!match.players.find(p=>p.kind==="BOT"),
      myParticipantId:own.participant_id,myActiveAttemptId:attempt?.id??null,myActiveAttemptProgress:attempt?.highestProgress??0,
      myResultPending:match.players.find(p=>p.participantId===own.participant_id)!.eliminationStage!==null && !result,
      result,mapSelection:match.state==="MAP_SELECTION"?await this.engine.mapSelectionStatus(id):null};
  }
  public async active(session:RankedSessionContext){
    const row=(await this.db.query<{match_id:string}>(`SELECT match_id FROM sur_online_players WHERE player_id=$1 AND acknowledged_at IS NULL
      ORDER BY last_seen_at DESC,match_id DESC LIMIT 1`,[session.playerId])).rows[0];
    return {match:row?await this.view(row.match_id,session):null,queue:await survivalQueueView(this.db,session,this.clock.now(),this.config)};
  }
  public async event(id:string,session:RankedSessionContext,body:SurvivalOnlineEventDto){
    const own=await this.owned(id,session);
    if(["ASSETS_READY","ENTERED","ATTEMPT_START"].includes(body.type))this.sessions.assertEnvironment(body.installedMods??[]);
    const context={roundKey:body.roundKey,mapGeneration:body.mapGeneration,levelId:body.levelId,participantId:own.participant_id};
    let command:SurvivalCommand;
    if(body.type==="PRESENTATION_COMPLETE")command={type:body.type,roundKey:body.roundKey,mapGeneration:body.mapGeneration,participantId:own.participant_id};
    else if(body.type==="ASSETS_READY"){if(!body.certificate)throw new BadRequestException("Map certificate required");command={type:body.type,...context,certificate:body.certificate};}
    else if(body.type==="PREPARATION_FAILED")command={type:body.type,...context,reason:body.reason??"NATIVE_PREPARATION_FAILED"};
    else if(body.type==="ENTERED")command={type:body.type,...context};
    else if(body.type==="FORFEIT")command={type:body.type,roundKey:body.roundKey,mapGeneration:body.mapGeneration,participantIds:[own.participant_id],reason:"ONLINE_SURRENDER"};
    else {
      if(!body.attemptId)throw new BadRequestException("Attempt ID required");
      if(body.type==="ATTEMPT_START")command={type:body.type,...context,attemptId:body.attemptId};
      else {if(body.progress===undefined)throw new BadRequestException("Progress required");command={type:body.type,...context,attemptId:body.attemptId,progress:body.progress};}
    }
    let applied:SurvivalMatch;
    try{applied=await this.engine.dispatch(id,`${own.participant_id}:${body.eventId}`,command,session);}
    catch(error){if(error instanceof RankedDomainError)throw new ConflictException({code:error.code,message:error.message});throw error;}
    const round=survivalCurrentRound(applied),participant=round.players.find(p=>p.participantId===own.participant_id);
    if(["ENTERED","ATTEMPT_START"].includes(body.type) && round.key===body.roundKey && round.mapGeneration===body.mapGeneration && participant && participant.enteredAt!==null)
      await this.db.query("UPDATE sur_online_players SET recovering=FALSE WHERE match_id=$1 AND player_id=$2 AND admission_session_id=$3",[id,session.playerId,session.sessionId]);
    return this.view(id,session,body.roundKey,body.mapGeneration);
  }
  public async acknowledge(id:string,session:RankedSessionContext){
    const row=await this.owned(id,session),match=await this.engine.get(id);
    if(!row.result && !terminal(match))throw new BadRequestException("Your Survival result is not settled yet");
    await this.db.transaction(async tx=>{
      await this.actor(tx,session);
      await tx.query("UPDATE sur_online_players SET acknowledged_at=$3 WHERE match_id=$1 AND player_id=$2",[id,session.playerId,this.clock.now().toISOString()]);
      await tx.query("UPDATE sur_queue_entries SET status='LEFT',matched_match_id=NULL WHERE player_id=$1 AND matched_match_id=$2",[session.playerId,id]);
    });return {ok:true};
  }
  public tickOnce():Promise<void>{
    if(this.pending)return this.pending;
    this.pending=(async()=>{
      const rows=await this.db.query<{match_id:string}>(`SELECT o.match_id FROM sur_online_matches o JOIN sur_matches m ON m.id=o.match_id
        WHERE m.state NOT IN ('FINAL_RESULT','CANCELLED') OR EXISTS(SELECT 1 FROM sur_online_players p WHERE p.match_id=o.match_id AND (p.settled_at IS NULL OR COALESCE((p.result->>'finalStandings')::boolean,FALSE)=FALSE))
        ORDER BY o.created_at`);
      for(const row of rows.rows)try{const match=await this.engine.tick(row.match_id);this.engine.ensureMapSelection(match);await this.settle(match);}catch(e){this.logger.error(`Online Survival ${row.match_id}: ${String(e)}`);}
    })().finally(()=>{this.pending=null;});return this.pending;
  }
  public async settle(match:SurvivalMatch):Promise<void>{
    const inputs=survivalSettlementInputs(match);
    const rows=(await this.db.query<ResultRow>("SELECT player_id,participant_id,result FROM sur_online_players WHERE match_id=$1 AND result IS NULL",[match.id])).rows;
    for(const row of rows){
      const input=inputs.find(p=>p.participantId===row.participant_id);if(!input)continue;
      const rating=await this.survival.settleLp(row.player_id,input.eventKey,input.delta,{generation:match.configGeneration,config:match.config});
      const ledger=(await this.db.query<{lp_before:number;tier_before:string;warning_before:boolean}>("SELECT lp_before,tier_before,warning_before FROM sur_lp_ledger WHERE player_id=$1 AND event_key=$2",[row.player_id,input.eventKey])).rows[0]!;
      const groups=new Set<string>();const bands=match.config.tierBands.filter(b=>{if(groups.has(b.group))return false;groups.add(b.group);return true;}).map(b=>({tier:b.group,minimum:b.minimumLp}));
      const result:SurvivalOnlineResult={matchId:match.id,debug:false,botFilled:match.players.some(p=>p.kind==="BOT"),won:(match.winnerIds??[match.winnerId]).includes(row.participant_id),rank:input.rank,tiedPlayers:input.tiedPlayers,
        requestedDelta:input.delta,actualDelta:rating.actualDelta,profileBefore:{lp:Number(ledger.lp_before),tier:ledger.tier_before,demotionWarning:ledger.warning_before},
        profileAfter:rating.profile,promoted:rating.promoted,demoted:rating.demoted,protected:rating.protected,warningRecoveryLp:rating.warningRecoveryLp,bands,
        standings:match.players.map(p=>({participantId:p.participantId,playerId:p.playerId,rank:p.finalRank,name:p.name,kind:p.kind,gdAccountId:p.gdAccountId??null,
          displayedTier:p.playerId===row.player_id?rating.profile.tier:p.displayedTier??survivalTierForLp(p.lp,match.config.tierBands).group,
          delta:inputs.find(v=>v.participantId===p.participantId)?.delta??null})),completedAt:new Date(match.finishedAt??this.clock.now().getTime()).toISOString(),completionKind:match.completionKind??"NORMAL"};
      await this.db.transaction(async tx=>{
        await tx.query("UPDATE sur_online_players SET result=$3::jsonb,settled_at=$4 WHERE match_id=$1 AND player_id=$2 AND result IS NULL",[match.id,row.player_id,JSON.stringify(result),this.clock.now().toISOString()]);
        await tx.query("UPDATE sur_match_players SET result_lp_delta=$3 WHERE match_id=$1 AND participant_id=$2",[match.id,row.participant_id,input.delta]);
      });
    }
    // Personal payment is immutable. Once the shared match ends, refresh the
    // roster in those already-paid records without applying their LP again.
    if(terminal(match)){
      const paid=(await this.db.query<ResultRow>("SELECT player_id,participant_id,result FROM sur_online_players WHERE match_id=$1 AND result IS NOT NULL AND COALESCE((result->>'finalStandings')::boolean,FALSE)=FALSE",[match.id])).rows;
      for(const row of paid){
        const result={...row.result!,tiedPlayers:inputs.find(p=>p.participantId===row.participant_id)?.tiedPlayers??row.result!.tiedPlayers,finalStandings:true,
          standings:match.players.map(p=>({participantId:p.participantId,playerId:p.playerId,rank:p.finalRank,name:p.name,kind:p.kind,gdAccountId:p.gdAccountId??null,
            displayedTier:p.playerId===row.player_id?row.result!.profileAfter.tier:p.displayedTier??survivalTierForLp(p.lp,match.config.tierBands).group,
            delta:inputs.find(v=>v.participantId===p.participantId)?.delta??null}))};
        await this.db.query("UPDATE sur_online_players SET result=$3::jsonb WHERE match_id=$1 AND player_id=$2",[match.id,row.player_id,JSON.stringify(result)]);
      }
    }

  }
}
