import { DemandWorker, databaseWork } from "../database/database-work.js";
import type { AchievementUnlockNotice } from "../achievement/achievement.service.js";
import {survivalResultRoster} from "./survival-result-roster.js";
import { BadRequestException, ConflictException, ForbiddenException, Inject, Injectable, Logger, NotFoundException,
  type OnApplicationBootstrap, type OnApplicationShutdown } from "@nestjs/common";
import { timingSafeEqual } from "node:crypto";
import { forceSurvivalDebugRank,  survivalCurrentRound,
  survivalDebugLp, survivalLiveView, survivalSettlementInputs,  type RandomSource, type SurvivalCommand,
  type SurvivalBotDriverState,type SurvivalMatch, type SurvivalRatingProfile } from "@corum-ranked/rules";
import { driveSurvivalBots } from "./survival-bot-driver.js";
import { lockSurvivalAdmission } from "./survival-admission.js";
import { DATABASE, type DatabasePort, type SqlExecutor } from "../database/database.port.js";
import { RANDOM_SOURCE, SERVER_CLOCK, type ServerClock } from "../common/runtime.module.js";
import { SERVER_ENVIRONMENT, type ServerEnvironment } from "../config/server-environment.js";
import { SurvivalConfigService } from "../config/survival-config.service.js";
import { SessionService } from "../session/session.service.js";
import { sessionAliveSince } from "../session/session-presence.policy.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { SurvivalEngineService, type SurvivalAdmissionPlayer } from "./survival-engine.service.js";
import { SurvivalService } from "./survival.service.js";
import type { ClearSurvivalDebugDto, CreateSurvivalDebugDto, ForceSurvivalDebugDto, SurvivalDebugEventDto } from "./survival-debug.dto.js";

interface DebugRow {
  match_id:string; human_player_id:string; seed:number|string; human_actual_lp:number; average_group:string; force_rank:number|null;
  driver_state:SurvivalBotDriverState; result:SurvivalDebugResult|null; acknowledged_at:Date|string|null;
}
export interface SurvivalDebugResult {
  matchId:string; debug:true; won:boolean; rank:number; tiedPlayers:number; requestedDelta:number; actualDelta:number;
  profileBefore:SurvivalRatingProfile; profileAfter:SurvivalRatingProfile;
  promoted:boolean; demoted:boolean; protected:boolean; warningRecoveryLp:number|null;
  bands:{tier:string;minimum:number}[]; standings:ReturnType<typeof survivalResultRoster>;finalStandings?:boolean;
  completedAt:string; completionKind:string;achievementUnlocks?:readonly AchievementUnlockNotice[];
}
const terminal=(m:SurvivalMatch)=>["FINAL_RESULT","CANCELLED"].includes(m.state);

@Injectable()
export class SurvivalDebugService implements OnApplicationBootstrap,OnApplicationShutdown {
  private readonly logger=new Logger(SurvivalDebugService.name);
  private readonly worker:DemandWorker;
  private readonly unsubscribe:()=>void;
  private pending:Promise<void>|null=null;
  private readonly activeMatches=new Set<string>();
  private discovery:Promise<void>|null=null;
  private discoveryRequested=true;
  public constructor(@Inject(DATABASE) private readonly db:DatabasePort,
    @Inject(SERVER_ENVIRONMENT) private readonly env:ServerEnvironment,
    @Inject(SERVER_CLOCK) private readonly clock:ServerClock,
    @Inject(RANDOM_SOURCE) private readonly random:RandomSource,
    private readonly config:SurvivalConfigService, private readonly sessions:SessionService,
    private readonly engine:SurvivalEngineService, private readonly survival:SurvivalService) {
    engine.registerAdapter((tx,match,now)=>this.drive(tx,match,now));
    this.unsubscribe=databaseWork(db).subscribe("survival",()=>{this.discoveryRequested=true;});
    this.worker=new DemandWorker(db,"survival",500,async()=>{await this.tickOnce();return this.activeMatches.size>0;},e=>this.logger.error(String(e)));
  }
  public async onApplicationBootstrap():Promise<void> {
    await this.worker.start();
  }
  public async onApplicationShutdown():Promise<void> {this.unsubscribe();await this.worker.stop();await this.pending;}
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
    if(body.botCount!==undefined && (!Number.isInteger(body.botCount)||body.botCount<1||body.botCount>63))throw new BadRequestException("Invalid bot count");
    await this.survival.profile(session.playerId);
    const creationKey=`sur-debug:${session.playerId}:${body.requestId}`;
    const match=await this.config.withAdmission(()=>this.db.transaction(async tx=>{
      await lockSurvivalAdmission(tx);
      await tx.query("SELECT id FROM ranked_players WHERE id=$1 FOR UPDATE",[session.playerId]);
      const now=this.clock.now();
      const live=await tx.query(`SELECT id FROM ranked_sessions WHERE id=$1 AND player_id=$2 AND revoked_at IS NULL
        AND expires_at>$3 AND last_heartbeat_at>$4`,[session.sessionId,session.playerId,now.toISOString(),sessionAliveSince(now)]);
      if(!live.rows.length)throw new ForbiddenException("Debug session expired");
      const previous=(await tx.query<{snapshot:SurvivalMatch;average_group:string;force_rank:number|null}>(
        `SELECT m.snapshot,d.average_group,d.force_rank FROM sur_matches m JOIN sur_debug_matches d ON d.match_id=m.id WHERE m.creation_key=$1`,[creationKey])).rows[0];
      if(previous) {
        if(previous.snapshot.players.filter(p=>p.kind==="BOT").length!==(body.botCount??previous.snapshot.config.matchPlayerCount-1) || previous.average_group!==body.averageGroup || previous.force_rank!==(body.forceRank??null))throw new ConflictException("Debug creation payload changed");
        return previous.snapshot;
      }
      const busy=await tx.query(`SELECT 1 FROM ranked_queue_entries WHERE player_id=$1 AND status='QUEUED'
        UNION ALL SELECT 1 FROM ranked_matches WHERE (player_a_id=$1 OR player_b_id=$1) AND state NOT IN ('MATCH_RESULT','CANCELLED')
        UNION ALL SELECT 1 FROM ranked_placement_runs WHERE player_id=$1 AND state NOT IN ('PLACEMENT_RESULT','CANCELLED')
        UNION ALL SELECT 1 FROM sur_match_players p JOIN sur_matches m ON m.id=p.match_id WHERE p.player_id=$1 AND m.state NOT IN ('FINAL_RESULT','CANCELLED') AND (p.status='ACTIVE' OR EXISTS(SELECT 1 FROM sur_online_players o WHERE o.match_id=m.id AND o.player_id=$1 AND o.result IS NULL))
        UNION ALL SELECT 1 FROM sur_queue_entries WHERE player_id=$1 AND status='QUEUED' LIMIT 1`,[session.playerId]);
      if(busy.rows.length)throw new ConflictException("Finish or recover the current game first");
      const profile=(await tx.query<{lp:number}>("SELECT lp FROM sur_profiles WHERE player_id=$1 FOR UPDATE",[session.playerId])).rows[0]!;
      const seed=Math.floor(this.random.next()*0x100000000),captured=this.config.getSnapshot();
      const botCount=body.botCount??captured.config.matchPlayerCount-1;
      if(botCount>captured.config.matchPlayerCount-1 || (body.forceRank!==undefined && body.forceRank>botCount+1))throw new BadRequestException("Bot count or Force rank exceeds the current format");
      const lp=survivalDebugLp(seed,Number(profile.lp),body.averageGroup,captured.config,botCount);
      const players:SurvivalAdmissionPlayer[]=[{participantId:"human",kind:"HUMAN",playerId:session.playerId},
        ...lp.bots.map((value,i)=>({participantId:`bot-${String(i+1).padStart(2,"0")}`,kind:"BOT" as const,name:`Bot ${i+1}`,lp:value}))];
      const created=await this.engine.createWithin(tx,creationKey,players,captured,lp.humanSnapshotLp,true,false,botCount!==captured.config.matchPlayerCount-1);
      await tx.query(`INSERT INTO sur_debug_matches(match_id,human_player_id,admission_session_id,seed,average_group,
        virtual_human_lp,human_actual_lp,force_rank,last_seen_at,created_at) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$9)`,
        [created.id,session.playerId,session.sessionId,seed,body.averageGroup,lp.virtualHuman,profile.lp,body.forceRank??null,now.toISOString()]);
      return created;
    }));
    this.activeMatches.add(match.id);
    return this.view(match.id,session);
  }
  private async drive(tx:SqlExecutor,before:SurvivalMatch,now:number):Promise<SurvivalMatch> {
    if(terminal(before))return before;
    const row=(await tx.query<DebugRow>("SELECT * FROM sur_debug_matches WHERE match_id=$1",[before.id])).rows[0];
    if(!row)return before;
    let match=before;
    const human=match.players.find(p=>p.playerId===row.human_player_id)!;
    if(row.force_rank!==null)return forceSurvivalDebugRank(match,human.participantId,row.force_rank,now);
    // Eliminated humans may keep observing the bot tournament. The explicit
    // FORCE RANK branch above retains its immediate debug result behavior.
    const driven=driveSurvivalBots(match,Number(row.seed),row.driver_state,now,[Number(row.human_actual_lp)]);
    if(JSON.stringify(driven.cursor)!==JSON.stringify(row.driver_state))await tx.query("UPDATE sur_debug_matches SET driver_state=$2::jsonb WHERE match_id=$1",[match.id,JSON.stringify(driven.cursor)]);
    return driven.match;
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
      else {if(body.progress===undefined)throw new BadRequestException("Progress required");command={type:body.type,...context,attemptId:body.attemptId,progress:body.progress,...(body.currentProgress===undefined?{}:{currentProgress:body.currentProgress})};}
    }
    await this.engine.dispatch(id,`human:${body.eventId}`,command,session);
    return this.view(id,session);
  }
  public async force(id:string,session:RankedSessionContext,body:ForceSurvivalDebugDto) {
    this.password(session,body.password);await this.owned(id,session);
    const match=await this.engine.get(id);
    if(!Number.isInteger(body.rank)||body.rank<1||body.rank>match.players.length)throw new BadRequestException(`Force rank must be 1..${match.players.length}`);
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
  public async forceClear(id:string,session:RankedSessionContext,body:ClearSurvivalDebugDto) {
    // Admission already verified the shared debug password. The dedicated
    // persisted ownership row and a live debug session are still required here.
    if(!this.env.debugBot?.password)throw new NotFoundException("Debug Bot Match is disabled");
    await this.owned(id,session);
    const before=await this.engine.get(id),current=survivalCurrentRound(before);
    const retry=(await this.db.query("SELECT event_key FROM sur_match_commands WHERE match_id=$1 AND event_key=$2",[id,`force-clear:${body.requestId}`])).rows.length>0;
    if(!retry && (before.state!=="PLAYING" || current.phase==="TIEBREAK" || current.key!==body.roundKey || current.mapGeneration!==body.mapGeneration ||
      current.map?.levelId!==body.levelId || current.endsAt===null || this.clock.now().getTime()>=current.endsAt ||
      current.players.find(p=>p.participantId==="human")?.attempts.some(a=>a.endedAt!==null&&a.highestProgress===100))){
      await this.engine.tick(id);throw new ConflictException("Survival round changed before Force Clear");
    }
    const match=await this.engine.dispatch(id,`force-clear:${body.requestId}`,{type:"DEBUG_FORCE_CLEAR",participantId:"human",
      attemptId:body.requestId,roundKey:body.roundKey,mapGeneration:body.mapGeneration,levelId:body.levelId},session);
    // dispatch durably advances an elapsed server timer even when an arriving
    // command becomes read-only. Such a packet must not be reported as a clear.
    const round=match.rounds.find(r=>r.key===body.roundKey && r.mapGeneration===body.mapGeneration && r.map?.levelId===body.levelId);
    if(!round?.players.find(p=>p.participantId==="human")?.attempts.some(a=>a.endedAt!==null && a.highestProgress===100))
      throw new ConflictException("Survival round changed before Force Clear");
    this.logger.log(`Survival Debug Force Clear match=${id} round=${body.roundKey} generation=${body.mapGeneration} request=${body.requestId}`);
    return this.view(id,session);
  }
  public async endRound(id:string,session:RankedSessionContext,body:ClearSurvivalDebugDto) {
    if(!this.env.debugBot?.password)throw new NotFoundException("Debug Bot Match is disabled");
    await this.owned(id,session);
    const before=await this.engine.get(id),round=survivalCurrentRound(before);
    const retry=(await this.db.query("SELECT event_key FROM sur_match_commands WHERE match_id=$1 AND event_key=$2",[id,`end-round:${body.requestId}`])).rows.length>0;
    if(!retry && (before.state!=="PLAYING" || round.key!==body.roundKey || round.mapGeneration!==body.mapGeneration ||
      round.map?.levelId!==body.levelId || round.endsAt===null || this.clock.now().getTime()>=round.endsAt))
      throw new ConflictException("Survival round changed before Debug round end");
    const match=await this.engine.dispatch(id,`end-round:${body.requestId}`,{type:"DEBUG_END_ROUND",participantId:"human",
      roundKey:body.roundKey,mapGeneration:body.mapGeneration,levelId:body.levelId},session);
    if(!match.rounds.some(r=>r.key===body.roundKey && r.mapGeneration===body.mapGeneration && r.map?.levelId===body.levelId && r.finishedAt!==null))
      throw new ConflictException("Survival round changed before Debug round end");
    this.logger.log(`Survival Debug End Round match=${id} round=${body.roundKey} request=${body.requestId}`);
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
      await this.discover();
      for(const id of [...this.activeMatches])try {
        const match=await this.engine.tick(id);await this.settle(match);
        if(match.state==="CANCELLED")this.activeMatches.delete(id);
        else if(match.state==="FINAL_RESULT") {
          const stored=(await this.db.query<{settled_at:Date|string|null}>("SELECT settled_at FROM sur_debug_matches WHERE match_id=$1",[id])).rows[0];
          if(stored?.settled_at)this.activeMatches.delete(id);
        }
      }catch(e){this.logger.error(`Survival debug ${id}: ${String(e)}`);}
    })().finally(()=>{this.pending=null;});return this.pending;
  }
  private discover(force=false):Promise<void> {
    if(this.discovery)return this.discovery;
    if(!force && !this.discoveryRequested)return Promise.resolve();
    this.discoveryRequested=false;const known=[...this.activeMatches];
    this.discovery=(async()=>{
      const rows=(await this.db.query<{match_id:string}>(`SELECT d.match_id FROM sur_debug_matches d JOIN sur_matches m ON m.id=d.match_id
        WHERE (d.settled_at IS NULL OR COALESCE((d.result->>'finalStandings')::boolean,FALSE)=FALSE OR m.state NOT IN ('FINAL_RESULT','CANCELLED')) AND m.state<>'CANCELLED' ORDER BY d.created_at`)).rows;
      const live=new Set(rows.map(row=>row.match_id));
      for(const id of known)if(!live.has(id))this.activeMatches.delete(id);
      for(const id of live)this.activeMatches.add(id);
    })().catch(e=>{this.discoveryRequested=true;throw e;}).finally(()=>{this.discovery=null;});return this.discovery;
  }
  private async settle(match:SurvivalMatch):Promise<SurvivalDebugResult|null> {
    const row=(await this.db.query<DebugRow>("SELECT * FROM sur_debug_matches WHERE match_id=$1",[match.id])).rows[0];
    if(!row)return null;
    if(row.result){
      if(terminal(match) && (!row.result.finalStandings || (match.state==="FINAL_RESULT" && !Array.isArray(row.result.achievementUnlocks)))){
        const result:SurvivalDebugResult={...row.result,finalStandings:true,standings:survivalResultRoster(match,new Map([[row.human_player_id,row.result]]))};
        await this.db.transaction(async tx=>{
          if(match.state==="FINAL_RESULT")result.achievementUnlocks=await this.survival.recordAchievementsWithin(tx,match.id,row.human_player_id);
          await tx.query("UPDATE sur_debug_matches SET result=$2::jsonb WHERE match_id=$1",[match.id,JSON.stringify(result)]);
        });return result;
      }return row.result;
    }
    const inputs=survivalSettlementInputs(match),human=inputs.find(p=>p.playerId===row.human_player_id);if(!human)return null;
    const rating=await this.survival.settleLp(row.human_player_id,human.eventKey,human.delta,{generation:match.configGeneration,config:match.config});
    const ledger=(await this.db.query<{lp_before:number;tier_before:string;warning_before:boolean}>(
      "SELECT lp_before,tier_before,warning_before FROM sur_lp_ledger WHERE player_id=$1 AND event_key=$2",[row.human_player_id,human.eventKey])).rows[0]!;
    const groups=new Set<string>();
    const bands=match.config.tierBands.filter(b=>{if(groups.has(b.group))return false;groups.add(b.group);return true;}).map(b=>({tier:b.group,minimum:b.minimumLp}));
    const result:SurvivalDebugResult={matchId:match.id,debug:true,won:(match.winnerIds??[match.winnerId]).includes("human"),rank:human.rank,tiedPlayers:human.tiedPlayers,requestedDelta:human.delta,
      actualDelta:rating.actualDelta,profileBefore:{lp:Number(ledger.lp_before),tier:ledger.tier_before,demotionWarning:ledger.warning_before},
      profileAfter:rating.profile,promoted:rating.promoted,demoted:rating.demoted,protected:rating.protected,
      warningRecoveryLp:rating.warningRecoveryLp,bands,standings:survivalResultRoster(match,new Map([[row.human_player_id,{profileAfter:rating.profile,actualDelta:rating.actualDelta}]])),
      completedAt:new Date(match.finishedAt??this.clock.now().getTime()).toISOString(),completionKind:match.completionKind??"NORMAL",finalStandings:terminal(match)};
    await this.db.transaction(async tx=>{
      // The ledger's unique key closes a crash between payment and this result
      // projection. Replaying settlement restores the same rating/result.
      await tx.query("UPDATE sur_debug_matches SET result=$2::jsonb,settled_at=$3 WHERE match_id=$1 AND result IS NULL",[match.id,JSON.stringify(result),this.clock.now().toISOString()]);
      for(const p of inputs)await tx.query("UPDATE sur_match_players SET result_lp_delta=$3 WHERE match_id=$1 AND participant_id=$2",[match.id,p.participantId,p.delta]);
      if(match.state==="FINAL_RESULT"){
        result.achievementUnlocks=await this.survival.recordAchievementsWithin(tx,match.id,row.human_player_id);
        await tx.query("UPDATE sur_debug_matches SET result=$2::jsonb WHERE match_id=$1",[match.id,JSON.stringify(result)]);
      }
    });
    return result;
  }
}
