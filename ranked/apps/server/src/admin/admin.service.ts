import { BadRequestException, ConflictException, ForbiddenException, Inject, Injectable, NotFoundException, Optional } from "@nestjs/common";
import { randomUUID, timingSafeEqual } from "node:crypto";
import { PROGRESSIVE_ACHIEVEMENTS, ONE_SHOT_ACHIEVEMENTS, survivalTierForLp } from "@corum-ranked/rules";
import { lockSurvivalAdmission } from "../survival/survival-admission.js";
import { SurvivalConfigService } from "../config/survival-config.service.js";
import { DATABASE, type DatabasePort, type SqlExecutor } from "../database/database.port.js";
import { SERVER_ENVIRONMENT, type ServerEnvironment } from "../config/server-environment.js";
import { RankedConfigService } from "../config/ranked-config.service.js";
import type { RankedSessionContext } from "../session/session.types.js";
import type { AdminActionDto } from "./admin.dto.js";
import { SERVER_CLOCK, type ServerClock } from "../common/runtime.module.js";
import { sessionAliveSince } from "../session/session-presence.policy.js";
import { readRankedAudioVolumes } from "../config/ranked-audio-volume.js";

@Injectable()
export class AdminService {
  public constructor(@Inject(DATABASE) private readonly db: DatabasePort,
    @Inject(SERVER_ENVIRONMENT) private readonly env: ServerEnvironment, @Inject(RankedConfigService) private readonly config: RankedConfigService,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
    @Optional() private readonly survivalConfig?:SurvivalConfigService) {}

  public async execute(session: RankedSessionContext, body: AdminActionDto) {
    // The standard SessionGuard supplies this DB-backed identity. Client button
    // visibility alone cannot authorize the route. This is a name/password
    // restriction, not proof of ownership of a self-asserted GD account.
    const expected = this.env.debugBot?.password;
    if (!expected) throw new NotFoundException("Admin is disabled");
    const supplied = Buffer.from(body.password), secret = Buffer.from(expected);
    if (session.gdUsername.toLowerCase() !== "hwanhee1" || supplied.length !== secret.length || !timingSafeEqual(supplied,secret)) {
      throw new ForbiddenException("Admin requires hwanhee1 and the debug password");
    }
    if (body.action === "RESET_DB" && body.confirmation !== "RESET") throw new BadRequestException("Confirm database reset first");
    const common=["UNLOCK_ALL","GET_AUDIO_VOLUMES","SET_AUDIO_VOLUME"].includes(body.action);
    const scope=body.scope??(common?"COMMON":"SOLO_RANKED");
    if(["RESET_DB","RESET_HISTORY"].includes(body.action) && !body.scope)throw new BadRequestException("Choose the reset scope explicitly");
    if(common && scope!=="COMMON")throw new BadRequestException("Achievements and audio belong to COMMON");
    if(scope==="COMMON" && ["SET_LP","SET_MMR","SET_BOTH","DELETE_BOTS"].includes(body.action))throw new BadRequestException("Choose a rating mode");
    return this.db.transaction(async tx => {
      if(scope==="SURVIVAL" || (scope==="COMMON" && body.action==="RESET_DB"))await lockSurvivalAdmission(tx);
      await tx.query("SELECT id FROM ranked_players WHERE id=$1 FOR UPDATE",[session.playerId]);
      const now=this.clock.now();
      // Recheck admission inside the mutation transaction: a stale/revoked
      // session must not execute an operation that passed its HTTP guard earlier.
      const caller = await tx.query<{id:string}>(`SELECT s.id FROM ranked_sessions s JOIN ranked_players p ON p.id=s.player_id
        WHERE s.id=$1 AND s.player_id=$2 AND s.revoked_at IS NULL AND s.expires_at>$3
          AND s.last_heartbeat_at>$4 AND lower(p.gd_username)='hwanhee1'
        FOR UPDATE OF s`,[session.sessionId,session.playerId,now.toISOString(),sessionAliveSince(now)]);
      if (!caller.rows.length) throw new ForbiddenException("Admin session expired");
      if(scope==="COMMON" && body.action==="RESET_DB")return this.resetAll(tx);
      if(scope==="SURVIVAL")return this.survivalAction(tx,session,body,now);
      if (body.action === "GET_AUDIO_VOLUMES" || body.action === "SET_AUDIO_VOLUME") {
        const client = this.config.getSnapshot().client;
        if (body.action === "SET_AUDIO_VOLUME") {
          if (!Number.isInteger(body.songId) || body.songId! < 1 || body.songId! > 2147483647 ||
              !Number.isInteger(body.volumePercent) || body.volumePercent! < 0 || body.volumePercent! > 100) {
            throw new BadRequestException("Enter a song ID and an integer volume from 0 to 100");
          }
          if (!client?.audio.resources.some(resource => resource.songId === body.songId)) {
            throw new BadRequestException("This song is no longer configured for Ranked. Reload the song list.");
          }
          await tx.query(`INSERT INTO ranked_audio_volumes(song_id,volume_percent,updated_by_account_id,updated_at)
            VALUES ($1,$2,$3,$4) ON CONFLICT (song_id) DO UPDATE SET
            volume_percent=EXCLUDED.volume_percent,updated_by_account_id=EXCLUDED.updated_by_account_id,updated_at=EXCLUDED.updated_at`,
            [body.songId,body.volumePercent,session.gdAccountId,now.toISOString()]);
        }
        const audioVolumes = await readRankedAudioVolumes(client, tx);
        const songs = audioVolumes.map(volume => {
          const resources = client!.audio.resources.filter(resource => resource.songId === volume.songId);
          return { ...volume, labels: [...new Set(resources.map(resource => resource.label))],
            keys: resources.map(resource => resource.key) };
        });
        return { ok: true, songs, audioVolumes, message: body.action === "SET_AUDIO_VOLUME"
          ? "Song volume saved for all Ranked players." : "Ranked song volumes loaded." };
      }
      if (body.action === "RESET_DB") {
        await tx.query("SELECT id FROM ranked_players ORDER BY id FOR UPDATE");
        const active=await tx.query(`SELECT 1 FROM ranked_matches WHERE state NOT IN ('MATCH_RESULT','CANCELLED')
          UNION ALL SELECT 1 FROM ranked_placement_runs WHERE state NOT IN ('PLACEMENT_RESULT','CANCELLED')
          UNION ALL SELECT 1 FROM ranked_queue_entries WHERE status='QUEUED' LIMIT 1`);
        if(active.rows.length)throw new ConflictException("Finish Solo games and leave Solo queues before resetting Solo");
        await tx.query("DELETE FROM ranked_queue_entries");await tx.query("DELETE FROM ranked_match_tokens");
        await tx.query("DELETE FROM ranked_outbox_events WHERE aggregate_id IN (SELECT id FROM ranked_matches)");
        await tx.query("DELETE FROM ranked_matches");await tx.query("DELETE FROM ranked_placement_runs");
        const players=(await tx.query<{player_id:string}>("SELECT player_id FROM ranked_profiles ORDER BY player_id")).rows;
        for(const player of players)await this.resetSolo(tx,player.player_id,now);
        return {ok:true,reset:true,scope,message:"Solo ratings, Placement and match records reset. Survival and common data retained."};
      }
      if (body.action === "DELETE_BOTS") {
        const bots = await tx.query<{id:string}>("SELECT id FROM ranked_players WHERE is_bot=TRUE ORDER BY id FOR UPDATE");
        const ids=bots.rows.map(row=>row.id);
        await this.deleteHistory(tx,ids);
        await tx.query("DELETE FROM ranked_players WHERE id=ANY($1::uuid[])",[ids]);
        return {ok:true,count:ids.length,message:`Deleted ${ids.length} bot profiles and their match records.`};
      }
      const target = await tx.query<{id:string;gd_username:string;gd_account_id:string}>(
        "SELECT id,gd_username,gd_account_id FROM ranked_players WHERE lower(gd_username)=lower($1) AND is_bot=FALSE ORDER BY id FOR UPDATE",
        [body.username ?? session.gdUsername]);
      if (!target.rows.length) throw new NotFoundException("Player not found");
      if (target.rows.length !== 1) throw new ConflictException("The nickname matches multiple players");
      const player=target.rows[0]!;
      if(scope==="COMMON" && body.action==="RESET_HISTORY") {
        await this.resetCommon(tx,player.id,now);
        return {ok:true,resetPlayer:true,own:player.id===session.playerId,scope,message:`Reset common achievements, AP and title for ${player.gd_username}. Mode data retained.`};
      }
      const queued=await tx.query<{status:string}>("SELECT status FROM ranked_queue_entries WHERE player_id=$1 FOR UPDATE",[player.id]);
      if (queued.rows[0]?.status === "QUEUED") throw new ConflictException("Leave the player's matchmaking queue first");
      await tx.query("SELECT player_id FROM ranked_profiles WHERE player_id=$1 FOR UPDATE",[player.id]);
      const active=await tx.query<{id:string}>(`SELECT id FROM ranked_matches WHERE (player_a_id=$1 OR player_b_id=$1)
        AND state NOT IN ('MATCH_RESULT','CANCELLED') LIMIT 1`,[player.id]);
      if (active.rows.length) throw new ConflictException("Finish the player's current match first");
      if (body.action !== "UNLOCK_ALL") {
        const placement=await tx.query<{id:string}>(`SELECT id FROM ranked_placement_runs WHERE player_id=$1
          AND state NOT IN ('PLACEMENT_RESULT','CANCELLED') LIMIT 1`,[player.id]);
        if (placement.rows.length) throw new ConflictException("Finish the player's current Placement first");
      }
      if (body.action === "RESET_HISTORY") {
        const visible=await tx.query<{count:string}>(`SELECT COUNT(*) AS count FROM ranked_matches m JOIN ranked_profiles p ON p.player_id=$1
          WHERE (m.player_a_id=$1 OR m.player_b_id=$1) AND m.state='MATCH_RESULT' AND m.finished_at>p.history_visible_since`,[player.id]);
        const count=Number(visible.rows[0]?.count ?? 0);
        await tx.query("DELETE FROM ranked_placement_runs WHERE player_id=$1",[player.id]);
        await this.resetSolo(tx,player.id,now);
        await tx.query("DELETE FROM ranked_queue_entries WHERE player_id=$1",[player.id]);
        await tx.query("DELETE FROM ranked_match_tokens WHERE player_id=$1",[player.id]);
        return {ok:true,count,resetPlayer:true,own:player.id===session.playerId,
          scope,message:`Reset Solo LP, MMR, tier, Placement, statistics and visible history for ${player.gd_username}. Survival, achievements, AP, title and session retained.`};
      }
      if (body.action === "UNLOCK_ALL") {
        let count=0;const source=`ADMIN_UNLOCK_ALL:${randomUUID()}`;
        const rewards=[...PROGRESSIVE_ACHIEVEMENTS.flatMap(d=>d.levels.map(l=>({id:d.id,level:l.level,ap:l.ap}))),
          ...ONE_SHOT_ACHIEVEMENTS.map(d=>({id:d.id,level:0,ap:d.ap}))];
        for (const reward of rewards) {
          const result=await tx.query(`INSERT INTO ranked_achievement_unlocks
            (player_id,achievement_id,achievement_level,reward_ap,unlocked_at,source_event_key)
            VALUES ($1,$2,$3,$4,CURRENT_TIMESTAMP,$5) ON CONFLICT DO NOTHING`,[player.id,reward.id,reward.level,reward.ap,source]);
          count+=result.rowCount;
        }
        // No normal match unlock notices are emitted for this source; the
        // requesting UI receives one batch summary. Claims remain normal.
        return {ok:true,count,batchUnlock:true,message:`Unlocked ${count} achievement rewards for ${player.gd_username}.`};
      }
      const lp=body.action === "SET_LP" || body.action === "SET_BOTH";
      const mmr=body.action === "SET_MMR" || body.action === "SET_BOTH";
      if (!lp && !mmr) throw new BadRequestException("Unknown Admin action");
      const valid=(value: number | undefined,minimum=0) => Number.isInteger(value) && value!>=minimum && value!<=2147483647;
      if ((lp && !valid(body.lp)) || (mmr && !valid(body.mmr,-2147483648))) throw new BadRequestException("Enter the requested numeric values");
      if (lp) {
        const settings=this.config.getSnapshot().operational;
        const tier=[...settings.tierBands].sort((a,b)=>b.minInclusive-a.minInclusive).find(b=>body.lp!>=b.minInclusive);
        if (!tier || !settings.mmrPolicy) throw new BadRequestException("Tier configuration unavailable");
        await tx.query(`UPDATE ranked_profiles SET visible_ranked_score=$2, displayed_tier=$3, placement_games=$4,
          promotion_source_tier=NULL,promotion_target_tier=NULL,promotion_cutoff=NULL,promotion_games_required=NULL,
          promotion_wins_required=NULL,promotion_results='[]'::jsonb,demotion_warning=FALSE,updated_at=CURRENT_TIMESTAMP WHERE player_id=$1`,
          [player.id,body.lp,tier.tier,settings.mmrPolicy.placementGames]);
      }
      if (mmr) await tx.query("UPDATE ranked_profiles SET hidden_mmr=$2,updated_at=CURRENT_TIMESTAMP WHERE player_id=$1",[player.id,body.mmr]);
      const result=await tx.query<{displayed_tier:string;visible_ranked_score:number;placement_games:number}>(
        "SELECT displayed_tier,visible_ranked_score,placement_games FROM ranked_profiles WHERE player_id=$1",[player.id]);
      const row=result.rows[0];
      return {ok:true,own:player.id===session.playerId,profile:row ? {displayedTier:row.displayed_tier,
        visibleRankedScore:row.visible_ranked_score,placementGames:row.placement_games} : null,message:`Updated ${player.gd_username}.`};
    });
  }

  private async resetSolo(tx:SqlExecutor,playerId:string,now:Date):Promise<void> {
    const common=(await tx.query<{total_ap:number;equipped_achievement_id:string|null;equipped_achievement_level:number|null}>(
      "SELECT total_ap,equipped_achievement_id,equipped_achievement_level FROM ranked_profiles WHERE player_id=$1 FOR UPDATE",[playerId])).rows[0];
    await tx.query("DELETE FROM ranked_profiles WHERE player_id=$1",[playerId]);
    await tx.query(`INSERT INTO ranked_profiles(player_id,history_visible_since,total_ap,equipped_achievement_id,equipped_achievement_level)
      VALUES($1,$2,$3,$4,$5)`,[playerId,now.toISOString(),common?.total_ap??0,common?.equipped_achievement_id??null,common?.equipped_achievement_level??null]);
  }
  private async resetCommon(tx:SqlExecutor,playerId:string|null,now:Date):Promise<void> {
    const players=playerId?[playerId]:(await tx.query<{player_id:string}>("SELECT player_id FROM ranked_profiles ORDER BY player_id FOR UPDATE")).rows.map(p=>p.player_id);
    await tx.query("SELECT player_id FROM ranked_profiles WHERE player_id=ANY($1::uuid[]) ORDER BY player_id FOR UPDATE",[players]);
    for(const table of ["ranked_survival_achievement_facts","ranked_achievement_unlocks","ranked_achievement_events","ranked_ap_ledger","ranked_achievement_metrics"])
      await tx.query(`DELETE FROM ${table} WHERE player_id=ANY($1::uuid[])`,[players]);
    await tx.query("UPDATE ranked_profiles SET total_ap=0,equipped_achievement_id=NULL,equipped_achievement_level=NULL WHERE player_id=ANY($1::uuid[])",[players]);
    for(const id of players)await tx.query("INSERT INTO ranked_achievement_metrics(player_id,backfilled_at) VALUES($1,$2)",[id,now.toISOString()]);
  }
  private async survivalAction(tx:SqlExecutor,session:RankedSessionContext,body:AdminActionDto,now:Date) {
    if(!this.survivalConfig)throw new BadRequestException("Survival configuration unavailable");
    const captured=this.survivalConfig.getSnapshot(),initial=captured.config.initialLp;
    if(["SET_MMR","SET_BOTH","UNLOCK_ALL","GET_AUDIO_VOLUMES","SET_AUDIO_VOLUME"].includes(body.action))
      throw new BadRequestException("This action does not belong to Survival");
    if(body.action==="DELETE_BOTS")return {ok:true,count:0,scope:"SURVIVAL",message:"Survival bots belong to matches and have no account profiles. Use Survival history/database reset."};
    let players:string[];
    if(body.action==="RESET_DB")players=(await tx.query<{id:string}>("SELECT id FROM ranked_players ORDER BY id FOR UPDATE")).rows.map(p=>p.id);
    else {
      const found=(await tx.query<{id:string}>("SELECT id FROM ranked_players WHERE lower(gd_username)=lower($1) AND is_bot=FALSE ORDER BY id FOR UPDATE",[body.username??session.gdUsername])).rows;
      if(!found.length)throw new NotFoundException("Player not found");if(found.length!==1)throw new ConflictException("The nickname matches multiple players");
      players=found.map(p=>p.id);
    }
    const active=await tx.query(`SELECT 1 FROM sur_match_players p JOIN sur_matches m ON m.id=p.match_id WHERE p.player_id=ANY($1::uuid[])
      AND (m.state NOT IN ('FINAL_RESULT','CANCELLED') OR EXISTS(SELECT 1 FROM sur_debug_matches d WHERE d.match_id=m.id AND d.settled_at IS NULL AND m.state='FINAL_RESULT') OR EXISTS(SELECT 1 FROM sur_online_players o WHERE o.match_id=m.id AND o.player_id=p.player_id AND o.result IS NULL)) LIMIT 1`,[players]);
    if(active.rows.length)throw new ConflictException("Finish and settle Survival games first");
    await tx.query("SELECT player_id FROM sur_profiles WHERE player_id=ANY($1::uuid[]) ORDER BY player_id FOR UPDATE",[players]);
    if(body.action==="SET_LP") {
      if(!Number.isInteger(body.lp) || body.lp!<0 || body.lp!>2147483647)throw new BadRequestException("Enter valid Survival LP");
      const tier=survivalTierForLp(body.lp!,captured.config.tierBands);
      await tx.query(`INSERT INTO sur_profiles(player_id,lp,displayed_tier,lp_reached_at) VALUES($1,$2,$3,$4)
        ON CONFLICT(player_id) DO UPDATE SET lp=EXCLUDED.lp,displayed_tier=EXCLUDED.displayed_tier,demotion_warning=FALSE,lp_reached_at=EXCLUDED.lp_reached_at,updated_at=EXCLUDED.lp_reached_at`,[players[0],body.lp,tier.tier,now.toISOString()]);
      return {ok:true,scope:"SURVIVAL",own:players[0]===session.playerId,message:"Survival LP and tier updated."};
    }
    if(!["RESET_HISTORY","RESET_DB"].includes(body.action))throw new BadRequestException("Unsupported Survival Admin action");
    const tier=survivalTierForLp(initial,captured.config.tierBands);
    await tx.query("DELETE FROM sur_lp_ledger WHERE player_id=ANY($1::uuid[])",[players]);
    if(body.action==="RESET_DB"){
      await tx.query("UPDATE sur_queue_entries SET status='LEFT',matched_match_id=NULL WHERE status IN ('QUEUED','MATCHED')");
      await tx.query("DELETE FROM sur_matches");
      await tx.query("DELETE FROM sur_map_selection_history");
      await tx.query("DELETE FROM sur_queue_fill_requests");
    }
    await tx.query(`UPDATE sur_profiles SET lp=$2,displayed_tier=$3,demotion_warning=FALSE,lp_reached_at=$4,updated_at=$4,history_visible_since=$4
      WHERE player_id=ANY($1::uuid[])`,[players,initial,tier.tier,now.toISOString()]);
    return {ok:true,scope:"SURVIVAL",reset:body.action==="RESET_DB",resetPlayer:body.action==="RESET_HISTORY",own:players.includes(session.playerId),
      message:"Survival profile and visible history reset. Solo, accounts, sessions, achievements, AP and title retained."};
  }

  private async resetAll(tx:SqlExecutor) {
    await tx.query("SELECT id FROM ranked_players ORDER BY id FOR UPDATE");
    // Preserve the existing no-active-game/no-unsettled-LP reset fence.
    const active=await tx.query(`SELECT 1 FROM ranked_matches WHERE state NOT IN ('MATCH_RESULT','CANCELLED')
      UNION ALL SELECT 1 FROM ranked_placement_runs WHERE state NOT IN ('PLACEMENT_RESULT','CANCELLED')
      UNION ALL SELECT 1 FROM ranked_queue_entries WHERE status='QUEUED'
      UNION ALL SELECT 1 FROM sur_queue_entries WHERE status='QUEUED'
      UNION ALL SELECT 1 FROM sur_matches m WHERE m.state NOT IN ('FINAL_RESULT','CANCELLED')
        OR EXISTS(SELECT 1 FROM sur_debug_matches d WHERE d.match_id=m.id AND m.state='FINAL_RESULT' AND d.settled_at IS NULL)
        OR EXISTS(SELECT 1 FROM sur_online_players p WHERE p.match_id=m.id AND p.result IS NULL AND m.state='FINAL_RESULT') LIMIT 1`);
    if(active.rows.length)throw new ConflictException("Finish and settle all games and leave queues before resetting all data");
    const tables=(await tx.query<{tablename:string}>(`SELECT tablename FROM pg_catalog.pg_tables WHERE schemaname=current_schema()
      AND (tablename LIKE 'ranked\\_%' ESCAPE '\\' OR tablename LIKE 'sur\\_%' ESCAPE '\\' OR tablename LIKE 'debug\\_spectate\\_%' ESCAPE '\\')
      AND tablename NOT IN ('ranked_schema_migrations','ranked_achievement_versions') ORDER BY tablename`)).rows;
    // Table identifiers come only from PostgreSQL, never from an Admin payload.
    // All application tables participate in one truncate, preserving FK safety;
    // schema/migration history stays installed and unrelated schemas stay intact.
    const names=tables.map(row=>'"'+row.tablename.replaceAll('"','""')+'"');
    if(names.length)await tx.query(`TRUNCATE TABLE ${names.join(',')} RESTART IDENTITY`);
    return {ok:true,reset:true,scope:"COMMON",accountsDeleted:true,
      message:"All Ranked database data deleted, including accounts, sessions, both modes, records, AP, titles, caches and audio overrides. Sign in again."};
  }
  private async deleteHistory(tx: SqlExecutor, players: readonly string[]) {
    const matches=await tx.query<{id:string}>("SELECT id FROM ranked_matches WHERE player_a_id=ANY($1::uuid[]) OR player_b_id=ANY($1::uuid[]) FOR UPDATE",[players]);
    const ids=matches.rows.map(row=>row.id);
    await tx.query("DELETE FROM ranked_queue_entries WHERE matched_match_id=ANY($1::uuid[])",[ids]);
    await tx.query("DELETE FROM ranked_outbox_events WHERE aggregate_id=ANY($1::uuid[])",[ids]);
    await tx.query("DELETE FROM ranked_matches WHERE id=ANY($1::uuid[])",[ids]);
    return ids.length;
  }
}
