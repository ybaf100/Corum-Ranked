import { BadRequestException, ConflictException,ForbiddenException,Inject,Injectable,NotFoundException,Logger,type OnApplicationBootstrap,type OnApplicationShutdown } from "@nestjs/common";
import { createHash,timingSafeEqual } from "node:crypto";
import { survivalOnlineBotLp,type RandomSource } from "@corum-ranked/rules";
import { DATABASE,type DatabasePort,type SqlExecutor } from "../database/database.port.js";
import { SERVER_CLOCK,RANDOM_SOURCE,type ServerClock } from "../common/runtime.module.js";
import { SERVER_ENVIRONMENT,type ServerEnvironment } from "../config/server-environment.js";
import { SurvivalConfigService } from "../config/survival-config.service.js";
import { SessionService } from "../session/session.service.js";
import { sessionAliveSince } from "../session/session-presence.policy.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { REQUIRED_RANKED_CLIENT_VERSION } from "../session/client-version.js";
import type { EnvironmentRecheckDto } from "../session/session.dto.js";
import type { FillSurvivalBotsDto } from "./survival-online.dto.js";
import { lockSurvivalAdmission,assertSurvivalAdmission } from "./survival-admission.js";
import { SurvivalEngineService,type SurvivalAdmissionPlayer } from "./survival-engine.service.js";
import { SurvivalService } from "./survival.service.js";
import { survivalQueueView } from "./survival-queue.view.js";
interface QueueRow {entry_id:string;player_id:string;session_id:string;status:string;joined_at:Date|string;wait_started_at:Date|string;last_heartbeat_at:Date|string;matched_match_id:string|null;}
@Injectable()
export class SurvivalQueueService implements OnApplicationBootstrap,OnApplicationShutdown {
  private readonly logger=new Logger(SurvivalQueueService.name);
  private timer:ReturnType<typeof setInterval>|null=null;
  private pending:Promise<void>|null=null;
  public constructor(@Inject(DATABASE)private readonly db:DatabasePort,@Inject(SERVER_CLOCK)private readonly clock:ServerClock,
    @Inject(RANDOM_SOURCE)private readonly random:RandomSource,@Inject(SERVER_ENVIRONMENT)private readonly env:ServerEnvironment,
    private readonly config:SurvivalConfigService,private readonly sessions:SessionService,private readonly engine:SurvivalEngineService,private readonly survival:SurvivalService){}
  public onApplicationBootstrap():void {this.timer=setInterval(()=>{void this.tickOnce().catch(e=>this.logger.error(String(e)));},500);this.timer.unref();}
  public async onApplicationShutdown():Promise<void>{if(this.timer)clearInterval(this.timer);this.timer=null;await this.pending;}
  private async actor(tx:SqlExecutor,session:RankedSessionContext):Promise<void>{
    await tx.query("SELECT id FROM ranked_players WHERE id=$1 FOR UPDATE",[session.playerId]);
    const now=this.clock.now();const rows=await tx.query(`SELECT id FROM ranked_sessions WHERE id=$1 AND player_id=$2 AND revoked_at IS NULL AND expires_at>$3 AND last_heartbeat_at>$4`,
      [session.sessionId,session.playerId,now.toISOString(),sessionAliveSince(now)]);
    if(!rows.rows.length)throw new ForbiddenException("Survival session expired; reconnect");
  }
  private async busy(tx:SqlExecutor,playerId:string):Promise<boolean>{
    const rows=await tx.query(`SELECT 1 FROM ranked_queue_entries WHERE player_id=$1 AND status='QUEUED'
      UNION ALL SELECT 1 FROM ranked_matches WHERE (player_a_id=$1 OR player_b_id=$1) AND state NOT IN ('MATCH_RESULT','CANCELLED')
      UNION ALL SELECT 1 FROM ranked_placement_runs WHERE player_id=$1 AND state NOT IN ('PLACEMENT_RESULT','CANCELLED')
      UNION ALL SELECT 1 FROM sur_match_players p JOIN sur_matches m ON m.id=p.match_id LEFT JOIN sur_online_players o ON o.match_id=p.match_id AND o.player_id=p.player_id
      WHERE p.player_id=$1 AND m.state NOT IN ('FINAL_RESULT','CANCELLED') AND (p.status='ACTIVE' OR (o.player_id IS NOT NULL AND o.result IS NULL)) LIMIT 1`,[playerId]);
    return rows.rows.length>0;
  }
  public async join(session:RankedSessionContext,body:EnvironmentRecheckDto){
    this.sessions.assertEnvironment(body.installedMods);await this.survival.profile(session.playerId);
    await this.config.withAdmission(()=>this.db.transaction(async tx=>{
      await lockSurvivalAdmission(tx);await this.actor(tx,session);
      const own=(await tx.query<QueueRow>("SELECT * FROM sur_queue_entries WHERE player_id=$1 FOR UPDATE",[session.playerId])).rows[0];
      if(own?.status==="MATCHED")return;
      const pending=(await tx.query<{match_id:string}>("SELECT match_id FROM sur_online_players WHERE player_id=$1 AND acknowledged_at IS NULL ORDER BY last_seen_at DESC LIMIT 1",[session.playerId])).rows[0];
      if(pending){await tx.query(`INSERT INTO sur_queue_entries(player_id,session_id,client_version,joined_at,wait_started_at,last_heartbeat_at,status,matched_match_id)
        VALUES($1,$2,$3,$4,$4,$4,'MATCHED',$5) ON CONFLICT(player_id) DO UPDATE SET session_id=EXCLUDED.session_id,status='MATCHED',matched_match_id=EXCLUDED.matched_match_id`,
        [session.playerId,session.sessionId,session.clientVersion??REQUIRED_RANKED_CLIENT_VERSION,this.clock.now().toISOString(),pending.match_id]);return;}
      await assertSurvivalAdmission(tx,this.config.getSnapshot());
      if(await this.busy(tx,session.playerId))throw new ConflictException("Finish or recover the current game before joining Survival");
      const now=this.clock.now().toISOString();
      await tx.query(`INSERT INTO sur_queue_entries(player_id,session_id,client_version,joined_at,wait_started_at,last_heartbeat_at,status)
        VALUES($1,$2,$3,$4,$4,$4,'QUEUED') ON CONFLICT(player_id) DO UPDATE SET session_id=EXCLUDED.session_id,client_version=EXCLUDED.client_version,
        joined_at=CASE WHEN sur_queue_entries.status='QUEUED' THEN sur_queue_entries.joined_at ELSE EXCLUDED.joined_at END,
        wait_started_at=CASE WHEN sur_queue_entries.status='QUEUED' THEN sur_queue_entries.wait_started_at ELSE EXCLUDED.wait_started_at END,
        entry_id=CASE WHEN sur_queue_entries.status='QUEUED' THEN sur_queue_entries.entry_id ELSE EXCLUDED.entry_id END,
        last_heartbeat_at=EXCLUDED.last_heartbeat_at,status='QUEUED',matched_match_id=NULL`,[session.playerId,session.sessionId,session.clientVersion??REQUIRED_RANKED_CLIENT_VERSION,now]);
    }));
    await this.tickOnce();return this.status(session);
  }
  public async leave(session:RankedSessionContext){
    await this.db.transaction(async tx=>{
      await lockSurvivalAdmission(tx);await this.actor(tx,session);
      await tx.query("UPDATE sur_queue_entries SET status='LEFT' WHERE player_id=$1 AND session_id=$2 AND status='QUEUED'",[session.playerId,session.sessionId]);
    });return this.status(session);
  }
  public async heartbeat(session:RankedSessionContext){
    await this.db.transaction(async tx=>{
      await lockSurvivalAdmission(tx);await this.actor(tx,session);
      await tx.query("UPDATE sur_queue_entries SET last_heartbeat_at=$3 WHERE player_id=$1 AND session_id=$2 AND status='QUEUED'",[session.playerId,session.sessionId,this.clock.now().toISOString()]);
    });await this.tickOnce();return this.status(session);
  }
  public async status(session:RankedSessionContext){return survivalQueueView(this.db,session,this.clock.now(),this.config);}
  private async candidates(tx:SqlExecutor):Promise<QueueRow[]>{
    const now=this.clock.now();
    await tx.query(`UPDATE sur_queue_entries q SET status='EXPIRED' WHERE q.status='QUEUED' AND (q.last_heartbeat_at<=$1 OR
      NOT EXISTS(SELECT 1 FROM ranked_sessions s WHERE s.id=q.session_id AND s.revoked_at IS NULL AND s.expires_at>$2 AND s.last_heartbeat_at>$1 AND ltrim(s.client_version,'v')=ltrim($3,'v')))`,
      [sessionAliveSince(now),now.toISOString(),REQUIRED_RANKED_CLIENT_VERSION]);
    return [...(await tx.query<QueueRow>("SELECT * FROM sur_queue_entries WHERE status='QUEUED' ORDER BY joined_at,player_id LIMIT 16")).rows];
  }
  private async createFrom(tx:SqlExecutor,rows:readonly QueueRow[],fill:boolean,botCount?:number){
    // Lock accounts before queue/session/profile rows, matching session rotation.
    // Never hold one joiner's account while acquiring a second account.
    for(const id of rows.map(q=>q.player_id).sort())await tx.query("SELECT id FROM ranked_players WHERE id=$1 FOR UPDATE",[id]);
    const valid:QueueRow[]=[];
    for(const row of rows){
      const current=(await tx.query<QueueRow>("SELECT * FROM sur_queue_entries WHERE player_id=$1 FOR UPDATE",[row.player_id])).rows[0];
      if(!current||current.status!=="QUEUED"||current.session_id!==row.session_id)continue;
      const now=this.clock.now();const live=await tx.query("SELECT id FROM ranked_sessions WHERE id=$1 AND revoked_at IS NULL AND expires_at>$2 AND last_heartbeat_at>$3",[row.session_id,now.toISOString(),sessionAliveSince(now)]);
      if(!live.rows.length || await this.busy(tx,row.player_id))continue;valid.push(current);
    }
    if((!fill && valid.length<2)||(!valid.length))return null;
    if(fill && botCount!==undefined && (!Number.isInteger(botCount)||botCount<0||botCount>15||valid.length+botCount<2||valid.length+botCount>16))
      throw new BadRequestException("Requested Bot Count requires 2–16 total participants");
    if(!fill && valid.length<16 && this.clock.now().getTime()-new Date(valid[0]!.wait_started_at).getTime()<180000)return null;
    const captured=this.config.getSnapshot();await assertSurvivalAdmission(tx,captured);
    const seed=Math.floor(this.random.next()*0x100000000);
    const profiles=(await tx.query<{player_id:string;lp:number}>("SELECT player_id,lp FROM sur_profiles WHERE player_id=ANY($1::uuid[]) ORDER BY player_id FOR UPDATE",[valid.map(q=>q.player_id)])).rows;
    const botLp=fill?survivalOnlineBotLp(seed,valid.map(q=>Number(profiles.find(p=>p.player_id===q.player_id)!.lp)),captured.config,botCount):[];
    const entrants:SurvivalAdmissionPlayer[]=[...valid.map(q=>({participantId:q.player_id,kind:"HUMAN" as const,playerId:q.player_id})),
      ...botLp.map((lp,i)=>({participantId:`bot-${String(i+1).padStart(2,"0")}`,kind:"BOT" as const,name:`Bot ${i+1}`,lp}))];
    const key="sur-online:"+createHash("sha256").update(JSON.stringify({entries:valid.map(q=>q.entry_id),fill,botCount:botLp.length})).digest("hex");
    const match=await this.engine.createWithin(tx,key,entrants,captured,undefined,true,true);
    const now=this.clock.now().toISOString();
    await tx.query("INSERT INTO sur_online_matches(match_id,bot_filled,seed,created_at) VALUES($1,$2,$3,$4)",[match.id,fill&&botLp.length>0,seed,now]);
    for(const q of valid){
      await tx.query(`INSERT INTO sur_online_players(match_id,player_id,participant_id,admission_session_id,last_seen_at) VALUES($1,$2::uuid,$2::text,$3,$4)`,[match.id,q.player_id,q.session_id,now]);
      await tx.query("UPDATE sur_queue_entries SET status='MATCHED',matched_match_id=$2 WHERE player_id=$1 AND status='QUEUED'",[q.player_id,match.id]);
    }
    return match;
  }
  private password(session:RankedSessionContext,password:string):void{
    const expected=this.env.debugBot?.password;if(!expected)throw new NotFoundException("Debug Bot is disabled");
    if(session.clientBuildChannel!=="debug")throw new ForbiddenException("Bot fill requires a debug client");
    const a=Buffer.from(password),b=Buffer.from(expected);if(a.length!==b.length||!timingSafeEqual(a,b))throw new ForbiddenException("Invalid debug password");
  }
  public async fill(session:RankedSessionContext,body:FillSurvivalBotsDto){
    this.password(session,body.password);this.sessions.assertEnvironment(body.installedMods);
    return this.config.withAdmission(()=>this.db.transaction(async tx=>{
      await lockSurvivalAdmission(tx);await this.actor(tx,session);
      const prior=(await tx.query<{match_id:string;bot_count:number|null}>("SELECT match_id,bot_count FROM sur_queue_fill_requests WHERE player_id=$1 AND request_id=$2",[session.playerId,body.requestId])).rows[0];
      if(prior){if(prior.bot_count!==(body.botCount??null))throw new ConflictException("Bot fill payload changed");return {status:"MATCHED",matchId:prior.match_id};}
      const own=(await tx.query<QueueRow>("SELECT * FROM sur_queue_entries WHERE player_id=$1",[session.playerId])).rows[0];
      if(own?.status==="MATCHED")return {status:"MATCHED",matchId:own.matched_match_id};
      await assertSurvivalAdmission(tx,this.config.getSnapshot());
      if(!own||own.status!=="QUEUED"||own.session_id!==session.sessionId)throw new ConflictException("Join the Survival queue before filling bots");
      const rows=await this.candidates(tx);
      if(!rows.some(q=>q.player_id===session.playerId))throw new ConflictException("Your queue batch is not at the front yet");
      const match=await this.createFrom(tx,rows,true,body.botCount);if(!match)throw new ConflictException("Survival queue changed; retry");
      await tx.query("INSERT INTO sur_queue_fill_requests(player_id,request_id,match_id,created_at,bot_count) VALUES($1,$2,$3,$4,$5)",[session.playerId,body.requestId,match.id,this.clock.now().toISOString(),body.botCount??null]);
      return {status:"MATCHED",matchId:match.id};
    }));
  }
  public tickOnce():Promise<void>{
    if(this.pending)return this.pending;
    this.pending=this.config.withAdmission(()=>this.db.transaction(async tx=>{
      await lockSurvivalAdmission(tx);
      if(!this.config.getStatus().enabled)return;
      await assertSurvivalAdmission(tx,this.config.getSnapshot());
      for(let batch=0;batch<4;batch++){
        const rows=await this.candidates(tx);if(!rows.length)return;
        const expired=this.clock.now().getTime()-new Date(rows[0]!.wait_started_at).getTime()>=180000;
        if(rows.length<16 && !expired)return;
        if(rows.length===1){await tx.query("UPDATE sur_queue_entries SET wait_started_at=$2 WHERE player_id=$1 AND status='QUEUED'",[rows[0]!.player_id,this.clock.now().toISOString()]);return;}
        if(!await this.createFrom(tx,rows,false))return;
      }
    })).finally(()=>{this.pending=null;});return this.pending;
  }
}
