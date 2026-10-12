import { createHash } from "node:crypto";
import { BadRequestException, ConflictException, ForbiddenException, Inject, Injectable, Logger,
  NotFoundException, ServiceUnavailableException, type OnApplicationBootstrap, type OnApplicationShutdown } from "@nestjs/common";
import { createSurvivalMatch, reduceSurvivalMatch, survivalCurrentRound, survivalLiveView, survivalMilliPercent,
  type SurvivalCommand, type SurvivalEntrant, type SurvivalMatch } from "@corum-ranked/rules";
import { ID_GENERATOR, SERVER_CLOCK, type IdGenerator, type ServerClock } from "../common/runtime.module.js";
import { SurvivalConfigService, type SurvivalConfigDocument } from "../config/survival-config.service.js";
import { DATABASE, type DatabasePort, type SqlExecutor } from "../database/database.port.js";
import { SURVIVAL_MAP_PROVIDER, SurvivalMapError, type SurvivalMapProvider, type SurvivalMapHttpDiagnostics } from "./survival-map.provider.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { sessionAliveSince } from "../session/session-presence.policy.js";

export type SurvivalAdmissionPlayer =
  | {participantId:string;kind:"HUMAN";playerId:string}
  | {participantId:string;kind:"BOT";name:string;lp:number};
interface MatchRow { snapshot:SurvivalMatch; request_hash:string; map_job_token:string|null;
  map_job_deadline_at:Date|string|null; map_retry_at:Date|string|null; }
const hash = (value:unknown) => createHash("sha256").update(JSON.stringify(value)).digest("hex");
const iso = (milliseconds:number|null) => milliseconds===null ? null : new Date(milliseconds).toISOString();
const changed = (a:unknown,b:unknown) => JSON.stringify(a)!==JSON.stringify(b);
export type SurvivalMatchAdapter = (tx:SqlExecutor,match:SurvivalMatch,now:number)=>Promise<SurvivalMatch>;

/** Trusted application port for M4 bots and M5 synchronized participants.
 * There is no public create, progress, result or map-adoption HTTP endpoint in M3.
 * All normal rounds, bots and human players share the same durable reducer. */
@Injectable()
export class SurvivalEngineService implements OnApplicationBootstrap, OnApplicationShutdown {
  private readonly logger=new Logger(SurvivalEngineService.name);
  private timer:ReturnType<typeof setInterval>|null=null;
  private pending:Promise<void>|null=null;
  private stopping=false;
  private readonly mapJobs=new Map<string,Promise<void>>();
  private readonly adapters:SurvivalMatchAdapter[]=[];
  public registerAdapter(adapter:SurvivalMatchAdapter):void { this.adapters.push(adapter); }
  private async adapt(tx:SqlExecutor,match:SurvivalMatch,now:number):Promise<SurvivalMatch> {
    for(const adapter of this.adapters)match=await adapter(tx,match,now);
    return match;
  }
  public constructor(@Inject(DATABASE) private readonly database:DatabasePort,
    @Inject(SERVER_CLOCK) private readonly clock:ServerClock,
    @Inject(ID_GENERATOR) private readonly ids:IdGenerator,
    private readonly config:SurvivalConfigService,
    @Inject(SURVIVAL_MAP_PROVIDER) private readonly maps:SurvivalMapProvider) {}
  public onApplicationBootstrap():void {
    this.stopping=false;
    this.timer=setInterval(()=>{void this.recoverActiveMatches().catch(e=>this.logger.error(e));},250);
    this.timer.unref();
  }
  public async onApplicationShutdown():Promise<void> {
    this.stopping=true;
    if(this.timer)clearInterval(this.timer);this.timer=null;
    await this.pending;
    await Promise.allSettled(this.mapJobs.values());
  }
  public async create(creationKey:string, players:readonly SurvivalAdmissionPlayer[]):Promise<SurvivalMatch> {
    if (!creationKey.trim() || creationKey.length>200 || players.length!==16 || new Set(players.map(p=>p.participantId)).size!==16)
      throw new BadRequestException("M3 accepts exactly 16 distinct entrants");
    return this.config.withAdmission(()=>this.database.transaction(async tx=>{
      return this.createWithin(tx,creationKey,players,this.config.getSnapshot());
    }));
  }
  /** Trusted admission adapter only. Caller owns the mode admission fence and
   * account/session lock. Virtual LP changes this match snapshot, never a profile. */
  public async createWithin(tx:SqlExecutor,creationKey:string,players:readonly SurvivalAdmissionPlayer[],
    captured:SurvivalConfigDocument, humanLpSnapshot?:number):Promise<SurvivalMatch> {
    if(!creationKey.trim() || creationKey.length>200 || players.length!==16 || new Set(players.map(p=>p.participantId)).size!==16)
      throw new BadRequestException("M3 accepts exactly 16 distinct entrants");
    const requestHash=humanLpSnapshot===undefined?hash(players):hash({players,humanLpSnapshot});
      // Serialize even two first uses of the same key before its row exists.
      await tx.query("SELECT pg_advisory_xact_lock(hashtext($1))",[`survival-create:${creationKey}`]);
      const existing=(await tx.query<MatchRow>("SELECT * FROM sur_matches WHERE creation_key=$1",[creationKey])).rows[0];
      if(existing){if(existing.request_hash!==requestHash)throw new ConflictException("Survival creation key changed");return existing.snapshot;}
      if(!captured.config.enabled)throw new ServiceUnavailableException({code:"SURVIVAL_DISABLED",message:"Survival is disabled"});
      const humans=players.filter((p):p is Extract<SurvivalAdmissionPlayer,{kind:"HUMAN"}>=>p.kind==="HUMAN");
      const profiles=await tx.query<{player_id:string;lp:number;gd_username:string}>(
        `SELECT sp.player_id,sp.lp,p.gd_username FROM sur_profiles sp JOIN ranked_players p ON p.id=sp.player_id
         WHERE sp.player_id=ANY($1::uuid[]) ORDER BY sp.player_id FOR UPDATE OF sp`,[humans.map(p=>p.playerId)]);
      const entrants:SurvivalEntrant[]=players.map(p=>{
        if(p.kind==="BOT")return{...p,playerId:null};
        const profile=profiles.rows.find(row=>row.player_id===p.playerId);
        if(!profile)throw new BadRequestException("Enter Survival before match admission");
        return {...p,name:profile.gd_username,lp:humanLpSnapshot??Number(profile.lp)};
      });
      const match=createSurvivalMatch(this.ids.next(),entrants,captured.config,captured.generation,this.clock.now().getTime());
      await tx.query(`INSERT INTO sur_matches(id,creation_key,request_hash,state,revision,config_generation,config_snapshot,
        average_lp,difficulty_group,current_round_key,snapshot,created_at,updated_at)
        VALUES($1,$2,$3,$4,$5,$6,$7::jsonb,$8,$9,$10,$11::jsonb,$12,$12)`,
      [match.id,creationKey,requestHash,match.state,match.revision,match.configGeneration,JSON.stringify(match.config),
        match.averageLp,match.difficultyGroup,survivalCurrentRound(match).key,JSON.stringify(match),iso(match.createdAt)]);
      await this.project(tx,null,match);
      return match;
  }
  public async get(matchId:string):Promise<SurvivalMatch> {
    const row=(await this.database.query<MatchRow>("SELECT * FROM sur_matches WHERE id=$1",[matchId])).rows[0];
    if(!row)throw new NotFoundException("Survival match not found");return row.snapshot;
  }
  private async lock(tx:SqlExecutor,matchId:string):Promise<MatchRow> {
    const row=(await tx.query<MatchRow>("SELECT * FROM sur_matches WHERE id=$1 FOR UPDATE",[matchId])).rows[0];
    if(!row)throw new NotFoundException("Survival match not found");return row;
  }
  /** Receipt time is sampled while holding the authoritative match lock. A
   * packet that waits past endsAt is late; client timestamps never extend play.
   * Changed duplicate payloads conflict; exact retries return current state. */
  public async dispatch(matchId:string,eventKey:string,command:SurvivalCommand,actor?:RankedSessionContext):Promise<SurvivalMatch> {
    if(!eventKey.trim() || eventKey.length>200)throw new BadRequestException("Invalid Survival event key");
    return this.database.transaction(async tx=>{
      if(actor) {
        await tx.query("SELECT id FROM ranked_players WHERE id=$1 FOR UPDATE",[actor.playerId]);
        const now=this.clock.now();
        const live=await tx.query(`SELECT id FROM ranked_sessions WHERE id=$1 AND player_id=$2 AND revoked_at IS NULL
          AND expires_at>$3 AND last_heartbeat_at>$4`,[actor.sessionId,actor.playerId,now.toISOString(),sessionAliveSince(now)]);
        if(!live.rows.length)throw new ForbiddenException("Survival session expired");
      }
      const row=await this.lock(tx,matchId), fingerprint=hash(command);
      if(actor && !row.snapshot.players.some(p=>p.playerId===actor.playerId &&
        (("participantId" in command && p.participantId===command.participantId) ||
         (command.type==="FORFEIT" && command.participantIds.length===1 && command.participantIds[0]===p.participantId))))
        throw new ForbiddenException("Not the Survival participant");
      const previous=(await tx.query<{request_hash:string}>("SELECT request_hash FROM sur_match_commands WHERE match_id=$1 AND event_key=$2",[matchId,eventKey])).rows[0];
      if(previous){if(previous.request_hash!==fingerprint)throw new ConflictException("Survival event payload changed");return row.snapshot;}
      const now=this.clock.now().getTime();
      let match=await this.adapt(tx,row.snapshot,now);
      match=reduceSurvivalMatch(match,command,now);
      match=await this.adapt(tx,match,now);
      await this.persist(tx,row.snapshot,match);
      await tx.query(`INSERT INTO sur_match_commands(match_id,event_key,request_hash,command,applied_revision,received_at)
        VALUES($1,$2,$3,$4::jsonb,$5,$6)`,[matchId,eventKey,fingerprint,JSON.stringify(command),match.revision,iso(now)]);
      return match;
    });
  }
  public async tick(matchId:string):Promise<SurvivalMatch> {
    return this.database.transaction(async tx=>{
      const row=await this.lock(tx,matchId),now=this.clock.now().getTime();
      let match=await this.adapt(tx,row.snapshot,now);
      match=reduceSurvivalMatch(match,{type:"TICK"},now);
      match=await this.adapt(tx,match,now);
      await this.persist(tx,row.snapshot,match);return match;
    });
  }
  public async viewForPlayer(matchId:string,playerId:string) {
    const before=await this.get(matchId);
    const participant=before.players.find(p=>p.playerId===playerId);
    if(!participant)throw new ForbiddenException("Not a Survival match participant");
    const match=await this.tick(matchId);
    return survivalLiveView(match,this.clock.now().getTime(),participant.participantId);
  }
  public async selectMap(matchId:string):Promise<void> {
    const job=await this.database.transaction(async tx=>{
      const row=await this.lock(tx,matchId),now=this.clock.now().getTime();
      if(row.snapshot.state!=="MAP_SELECTION" || (row.map_retry_at && new Date(row.map_retry_at).getTime()>now) ||
        (row.map_job_token && row.map_job_deadline_at && new Date(row.map_job_deadline_at).getTime()>now))return null;
      const token=this.ids.next(),round=survivalCurrentRound(row.snapshot);
      await tx.query("UPDATE sur_matches SET map_job_token=$2,map_job_deadline_at=$3 WHERE id=$1",[matchId,token,iso(now+40_000)]);
      return {token,roundKey:round.key,mapGeneration:round.mapGeneration,request:{group:row.snapshot.difficultyGroup,
        phase:round.phase,minimumLevelId:row.snapshot.config.minimumLevelId,
        excludedLevelIds:row.snapshot.rounds.flatMap(r=>r.selectedMaps.map(m=>m.levelId))}};
    });
    if(!job)return;
    try {
      const map=await this.maps.select(job.request);
      await this.database.transaction(async tx=>{
        const row=await this.lock(tx,matchId),round=survivalCurrentRound(row.snapshot);
        if(row.map_job_token!==job.token || row.snapshot.state!=="MAP_SELECTION" || round.key!==job.roundKey || round.mapGeneration!==job.mapGeneration)return;
        const match=reduceSurvivalMatch(row.snapshot,{type:"MAP_SELECTED",roundKey:job.roundKey,mapGeneration:job.mapGeneration,map},this.clock.now().getTime());
        await this.persist(tx,row.snapshot,match);
        await tx.query("UPDATE sur_matches SET map_job_token=NULL,map_job_deadline_at=NULL,map_retry_at=NULL,map_error=NULL WHERE id=$1",[matchId]);
      });
    } catch(e) {
      const error=e instanceof SurvivalMapError ? {code:e.code,message:e.message,failures:e.failures,http:e.http} : {code:"API_ERROR",message:e instanceof Error?e.message:String(e)};
      const retryMs=e instanceof SurvivalMapError?Math.min(120_000,Math.max(2_000,e.retryAfterMs)):2_000;
      await this.database.query(`UPDATE sur_matches SET map_job_token=NULL,map_job_deadline_at=NULL,map_retry_at=$3,map_error=$4::jsonb
        WHERE id=$1 AND map_job_token=$2`,[matchId,job.token,iso(this.clock.now().getTime()+retryMs),JSON.stringify(error)]);
      this.logger.warn(`Survival map selection ${matchId}: ${error.code}; retryMs=${retryMs}; ${error.message}`);
    }
  }
  public ensureMapSelection(match:SurvivalMatch):void {
    if(this.stopping || match.state!=="MAP_SELECTION" || this.mapJobs.has(match.id) || this.mapJobs.size>=4)return;
    const job=this.selectMap(match.id).catch(e=>this.logger.error(String(e))).finally(()=>{this.mapJobs.delete(match.id);});
    this.mapJobs.set(match.id,job);
  }
  public async mapSelectionStatus(id:string) {
    const row=(await this.database.query<{map_error:{code:string;message?:string;http?:SurvivalMapHttpDiagnostics|null}|null;map_retry_at:Date|string|null;map_job_deadline_at:Date|string|null}>(
      "SELECT map_error,map_retry_at,map_job_deadline_at FROM sur_matches WHERE id=$1",[id])).rows[0];
    return {errorCode:row?.map_error?.code??null,errorMessage:row?.map_error?.message??null,httpDiagnostics:row?.map_error?.http??null,
      retryAt:row?.map_retry_at?new Date(row.map_retry_at).toISOString():null,
      selecting:!!row?.map_job_deadline_at && new Date(row.map_job_deadline_at).getTime()>this.clock.now().getTime()};
  }
  public recoverActiveMatches():Promise<void> {
    if(this.stopping)return Promise.resolve();
    if(this.pending)return this.pending;
    this.pending=this.recover().finally(()=>{this.pending=null;});return this.pending;
  }
  private async recover():Promise<void> {
    const rows=await this.database.query<{id:string}>(`SELECT id FROM sur_matches WHERE state IN ('MAP_SELECTION','ROUND_RESULT')
      OR (state IN ('PREPARING','ENTERING','COUNTDOWN','PLAYING') AND EXISTS (
        SELECT 1 FROM sur_rounds r WHERE r.match_id=sur_matches.id AND r.round_key=sur_matches.current_round_key AND (
          (sur_matches.state='PREPARING' AND r.preparation_started_at + INTERVAL '15 seconds' <= $1) OR
          (sur_matches.state='ENTERING' AND COALESCE(r.first_entered_at + INTERVAL '10 seconds',r.entry_requested_at + INTERVAL '5 seconds') <= $1) OR
          (sur_matches.state='COUNTDOWN' AND r.starts_at <= $1) OR
          (sur_matches.state='PLAYING' AND r.ends_at <= $1)))) ORDER BY updated_at,id`,[this.clock.now().toISOString()]);
    for(const row of rows.rows){
      try{const match=await this.tick(row.id);
        this.ensureMapSelection(match);
      }
      catch(e){this.logger.error(`Survival recovery ${row.id}: ${String(e)}`);}
    }
    // GD search/download never blocks countdowns or deadlines of other matches.
  }
  private async persist(tx:SqlExecutor,before:SurvivalMatch,match:SurvivalMatch):Promise<void> {
    if(match.revision===before.revision)return;
    await tx.query(`UPDATE sur_matches SET state=$2,revision=$3,current_round_key=$4,winner_id=$5,snapshot=$6::jsonb,
      updated_at=$7,finished_at=$8 WHERE id=$1`,[match.id,match.state,match.revision,survivalCurrentRound(match).key,
      match.winnerId,JSON.stringify(match),iso(match.updatedAt),iso(match.finishedAt)]);
    await this.project(tx,before,match);
  }
  private async project(tx:SqlExecutor,before:SurvivalMatch|null,match:SurvivalMatch):Promise<void> {
    for(const p of match.players){
      if(!changed(before?.players.find(old=>old.participantId===p.participantId),p) && before?.state===match.state)continue;
      const status=match.state==="FINAL_RESULT"?"FINISHED":match.state==="CANCELLED"?"CANCELLED":p.eliminationStage===null?"ACTIVE":"ELIMINATED";
      await tx.query(`INSERT INTO sur_match_players(match_id,participant_id,player_id,player_kind,player_name,lp_snapshot,status,
        elimination_stage,elimination_score,forfeit_reason,final_rank) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11)
        ON CONFLICT(match_id,participant_id) DO UPDATE SET status=EXCLUDED.status,elimination_stage=EXCLUDED.elimination_stage,
        elimination_score=EXCLUDED.elimination_score,forfeit_reason=EXCLUDED.forfeit_reason,final_rank=EXCLUDED.final_rank`,
      [match.id,p.participantId,p.playerId,p.kind,p.name,p.lp,status,p.eliminationStage,p.eliminationScore,p.forfeitReason,p.finalRank]);
    }
    for(const r of match.rounds){
      const old=before?.rounds.find(round=>round.key===r.key);
      if(!changed(old,r))continue;
      await tx.query(`INSERT INTO sur_rounds(match_id,round_key,ordinal,phase,target_qualifiers,map_generation,level_id,map_snapshot,
        selected_maps,preparation_started_at,entry_requested_at,first_entered_at,starts_at,ends_at,finished_at,cutoff_score)
        VALUES($1,$2,$3,$4,$5,$6,$7,$8::jsonb,$9::jsonb,$10,$11,$12,$13,$14,$15,$16)
        ON CONFLICT(match_id,round_key) DO UPDATE SET map_generation=EXCLUDED.map_generation,level_id=EXCLUDED.level_id,
        map_snapshot=EXCLUDED.map_snapshot,selected_maps=EXCLUDED.selected_maps,preparation_started_at=EXCLUDED.preparation_started_at,
        entry_requested_at=EXCLUDED.entry_requested_at,first_entered_at=EXCLUDED.first_entered_at,starts_at=EXCLUDED.starts_at,
        ends_at=EXCLUDED.ends_at,finished_at=EXCLUDED.finished_at,cutoff_score=EXCLUDED.cutoff_score`,
      [match.id,r.key,r.ordinal,r.phase,r.target,r.mapGeneration,r.map?.levelId??null,r.map?JSON.stringify(r.map):null,
        JSON.stringify(r.selectedMaps),iso(r.preparationStartedAt),iso(r.entryRequestedAt),iso(r.firstEnteredAt),iso(r.startsAt),iso(r.endsAt),iso(r.finishedAt),r.cutoffScore]);
      if(r.phase==="TIEBREAK")await tx.query(`INSERT INTO sur_tiebreaks(match_id,round_key,sequence,final_round_key,result) VALUES($1,$2,$3,$4,$5::jsonb)
        ON CONFLICT(match_id,round_key) DO UPDATE SET result=EXCLUDED.result`,[match.id,r.key,
        match.rounds.filter(round=>round.phase==="TIEBREAK" && round.ordinal<=r.ordinal).length,
        match.rounds.find(round=>round.phase==="FINAL")!.key,r.finishedAt===null?null:JSON.stringify(r.players.map(p=>({participantId:p.participantId,rank:p.rank,qualified:p.qualified})))]);
      for(const p of r.players){
        const previous=old?.players.find(player=>player.participantId===p.participantId);
        if(!changed(previous,p))continue;
        await tx.query(`INSERT INTO sur_round_players(match_id,round_key,participant_id,assets_ready_at,preparation_failure,
          entered_at,highest_progress,round_rank,qualified,forfeited) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10)
          ON CONFLICT(match_id,round_key,participant_id) DO UPDATE SET assets_ready_at=EXCLUDED.assets_ready_at,
          preparation_failure=EXCLUDED.preparation_failure,entered_at=EXCLUDED.entered_at,highest_progress=EXCLUDED.highest_progress,
          round_rank=EXCLUDED.round_rank,qualified=EXCLUDED.qualified,forfeited=EXCLUDED.forfeited`,
        [match.id,r.key,p.participantId,iso(p.assetsReadyAt),p.preparationFailure,iso(p.enteredAt),p.highestProgress,p.rank,p.qualified,p.forfeited]);
        for(const a of p.attempts){
          if(!changed(previous?.attempts.find(attempt=>attempt.id===a.id),a))continue;
          if(r.phase==="TIEBREAK")await tx.query(`INSERT INTO sur_tiebreak_attempts(match_id,round_key,participant_id,attempt_id,sequence,
            highest_progress,started_at,ended_at,score_millipercent) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9)
            ON CONFLICT(match_id,round_key,participant_id,attempt_id) DO UPDATE SET highest_progress=EXCLUDED.highest_progress,
            ended_at=EXCLUDED.ended_at,score_millipercent=EXCLUDED.score_millipercent`,
          [match.id,r.key,p.participantId,a.id,a.sequence,a.highestProgress,iso(a.startedAt),iso(a.endedAt),survivalMilliPercent(a.highestProgress)]);
          else await tx.query(`INSERT INTO sur_attempts(match_id,round_key,participant_id,attempt_id,sequence,highest_progress,started_at,ended_at)
            VALUES($1,$2,$3,$4,$5,$6,$7,$8) ON CONFLICT(match_id,round_key,participant_id,attempt_id)
            DO UPDATE SET highest_progress=EXCLUDED.highest_progress,ended_at=EXCLUDED.ended_at`,
          [match.id,r.key,p.participantId,a.id,a.sequence,a.highestProgress,iso(a.startedAt),iso(a.endedAt)]);
        }
      }
    }
  }
}
