import { BadRequestException, ConflictException, ForbiddenException, Inject, Injectable, NotFoundException } from "@nestjs/common";
import { randomUUID, timingSafeEqual } from "node:crypto";
import { PROGRESSIVE_ACHIEVEMENTS, ONE_SHOT_ACHIEVEMENTS } from "@corum-ranked/rules";
import { DATABASE, type DatabasePort, type SqlExecutor } from "../database/database.port.js";
import { SERVER_ENVIRONMENT, type ServerEnvironment } from "../config/server-environment.js";
import { RankedConfigService } from "../config/ranked-config.service.js";
import type { RankedSessionContext } from "../session/session.types.js";
import type { AdminActionDto } from "./admin.dto.js";
import { SERVER_CLOCK, type ServerClock } from "../common/runtime.module.js";
import { sessionAliveSince } from "../session/session-presence.policy.js";

@Injectable()
export class AdminService {
  public constructor(@Inject(DATABASE) private readonly db: DatabasePort,
    @Inject(SERVER_ENVIRONMENT) private readonly env: ServerEnvironment, @Inject(RankedConfigService) private readonly config: RankedConfigService,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock) {}

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
    return this.db.transaction(async tx => {
      await tx.query("SELECT id FROM ranked_players WHERE id=$1 FOR UPDATE",[session.playerId]);
      const now=this.clock.now();
      // Recheck admission inside the mutation transaction: a stale/revoked
      // session must not execute an operation that passed its HTTP guard earlier.
      const caller = await tx.query<{id:string}>(`SELECT s.id FROM ranked_sessions s JOIN ranked_players p ON p.id=s.player_id
        WHERE s.id=$1 AND s.player_id=$2 AND s.revoked_at IS NULL AND s.expires_at>$3
          AND s.last_heartbeat_at>$4 AND lower(p.gd_username)='hwanhee1'
        FOR UPDATE OF s`,[session.sessionId,session.playerId,now.toISOString(),sessionAliveSince(now)]);
      if (!caller.rows.length) throw new ForbiddenException("Admin session expired");
      if (body.action === "RESET_DB") {
        await tx.query(`TRUNCATE ranked_matches, ranked_players, ranked_outbox_events,
          ranked_forum_records, ranked_forum_refresh_cooldowns, debug_spectate_recordings CASCADE`);
        return { ok:true, reset:true, message:"Ranked player, match and session data reset. Operational configuration retained." };
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
        // Keep shared match evidence for the other participant, but start this
        // player's entire Ranked profile over. New columns inherit fresh-player
        // defaults rather than silently retaining statistics from the old run.
        await tx.query("DELETE FROM ranked_placement_runs WHERE player_id=$1",[player.id]);
        await tx.query("DELETE FROM ranked_achievement_unlocks WHERE player_id=$1",[player.id]);
        await tx.query("DELETE FROM ranked_achievement_events WHERE player_id=$1",[player.id]);
        await tx.query("DELETE FROM ranked_ap_ledger WHERE player_id=$1",[player.id]);
        await tx.query("DELETE FROM ranked_achievement_metrics WHERE player_id=$1",[player.id]);
        await tx.query("DELETE FROM ranked_profiles WHERE player_id=$1",[player.id]);
        await tx.query("INSERT INTO ranked_profiles (player_id,history_visible_since) VALUES ($1,$2)",[player.id,now.toISOString()]);
        // Do not restore the erased achievements from the retained shared
        // matches on the next state request. New results still count normally.
        await tx.query("INSERT INTO ranked_achievement_metrics (player_id,backfilled_at) VALUES ($1,$2)",[player.id,now.toISOString()]);
        await tx.query("DELETE FROM ranked_queue_entries WHERE player_id=$1",[player.id]);
        await tx.query("DELETE FROM ranked_match_tokens WHERE player_id=$1",[player.id]);
        await tx.query("UPDATE ranked_sessions SET revoked_at=$2 WHERE player_id=$1 AND revoked_at IS NULL",[player.id,now.toISOString()]);
        await tx.query("DELETE FROM ranked_forum_records WHERE gd_account_id=$1",[player.gd_account_id]);
        await tx.query("DELETE FROM ranked_forum_refresh_cooldowns WHERE gd_account_id=$1",[player.gd_account_id]);
        return {ok:true,count,resetPlayer:true,own:player.id===session.playerId,
          message:`Reset all Ranked data for ${player.gd_username}: LP, MMR, tier, Placement, statistics, achievements, AP, title and history. Sign in again to begin Placement.`};
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

  private async deleteHistory(tx: SqlExecutor, players: readonly string[]) {
    const matches=await tx.query<{id:string}>("SELECT id FROM ranked_matches WHERE player_a_id=ANY($1::uuid[]) OR player_b_id=ANY($1::uuid[]) FOR UPDATE",[players]);
    const ids=matches.rows.map(row=>row.id);
    await tx.query("DELETE FROM ranked_queue_entries WHERE matched_match_id=ANY($1::uuid[])",[ids]);
    await tx.query("DELETE FROM ranked_outbox_events WHERE aggregate_id=ANY($1::uuid[])",[ids]);
    await tx.query("DELETE FROM ranked_matches WHERE id=ANY($1::uuid[])",[ids]);
    return ids.length;
  }
}
