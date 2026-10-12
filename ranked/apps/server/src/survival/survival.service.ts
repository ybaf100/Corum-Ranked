import { BadRequestException, ConflictException, Inject, Injectable, NotImplementedException, ServiceUnavailableException } from "@nestjs/common";
import { survivalTierForLp, updateSurvivalRating, type SurvivalConfig, type SurvivalRatingUpdate } from "@corum-ranked/rules";
import type { SurvivalConfigDocument } from "../config/survival-config.service.js";
import { AchievementService } from "../achievement/achievement.service.js";
import { ID_GENERATOR, SERVER_CLOCK, type IdGenerator, type ServerClock } from "../common/runtime.module.js";
import { SurvivalConfigService } from "../config/survival-config.service.js";
import { DATABASE, type DatabasePort, type SqlExecutor } from "../database/database.port.js";

interface ProfileRow { player_id: string; lp: number; displayed_tier: string; demotion_warning: boolean; lp_reached_at: Date | string; }
interface LadderRow extends ProfileRow { gd_account_id: string; gd_username: string; rank: number; total: number; }

@Injectable()
export class SurvivalService {
  public constructor(@Inject(DATABASE) private readonly database: DatabasePort,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
    @Inject(ID_GENERATOR) private readonly ids: IdGenerator,
    private readonly config: SurvivalConfigService, private readonly achievements: AchievementService) {}

  /** OFF still permits browsing and initializes once, per the confirmed rule. */
  public async profile(playerId: string) {
    const snapshot = this.config.getSnapshot();
    let now = this.clock.now();
    await this.database.transaction(async transaction => {
      await transaction.query("SELECT id FROM ranked_players WHERE id = $1 FOR UPDATE", [playerId]);
      now = this.clock.now();
      await this.ensureProfile(transaction, playerId, snapshot.config, now);
    });
    const row = (await this.database.query<ProfileRow>("SELECT * FROM sur_profiles WHERE player_id = $1", [playerId])).rows[0]!;
    const standing = await this.standing(playerId);
    const record=(await this.database.query<{count:number;best:number|null}>(`SELECT COUNT(*)::int AS count,MIN(p.final_rank)::int AS best
      FROM sur_match_players p JOIN sur_matches m ON m.id=p.match_id JOIN sur_profiles sp ON sp.player_id=p.player_id
      LEFT JOIN sur_debug_matches d ON d.match_id=m.id WHERE p.player_id=$1 AND m.state='FINAL_RESULT'
      AND m.finished_at>sp.history_visible_since AND (d.match_id IS NULL OR d.settled_at IS NOT NULL)`,[playerId])).rows[0];
    return { serverNow: now.toISOString(), enabled: snapshot.config.enabled,
      profile: this.project(row, snapshot.config), standing,recordSummary:{completedMatches:Number(record?.count??0),bestRank:record?.best??null} };
  }

  private async ensureProfile(transaction: SqlExecutor, playerId: string, config: SurvivalConfig, now: Date): Promise<void> {
    const tier = survivalTierForLp(config.initialLp, config.tierBands);
    await transaction.query(`INSERT INTO sur_profiles (player_id,lp,displayed_tier,lp_reached_at,created_at,updated_at)
      VALUES ($1,$2,$3,$4,$4,$4) ON CONFLICT (player_id) DO NOTHING`,
    [playerId, config.initialLp, tier.tier, now.toISOString()]);
  }

  private project(row: ProfileRow, config: SurvivalConfig) {
    const band = config.tierBands.find(tier => tier.tier === row.displayed_tier);
    return { lp: Number(row.lp), tierId: row.displayed_tier, tier: band?.group ?? row.displayed_tier,
      demotionWarning: row.demotion_warning,
      warningRecoveryLp: row.demotion_warning && band ? band.minimumLp + config.demotionWarningRecoveryLp : null,
      lpReachedAt: new Date(row.lp_reached_at).toISOString() };
  }

  public async standing(playerId: string) {
    const result = await this.database.query<{ rank: number; total: number }>(`WITH ladder AS (
      SELECT sp.player_id, RANK() OVER (ORDER BY sp.lp DESC)::int AS rank, COUNT(*) OVER ()::int AS total
      FROM sur_profiles sp JOIN ranked_players p ON p.id=sp.player_id WHERE p.gd_account_id > 0 AND NOT p.is_bot
    ) SELECT rank,total FROM ladder WHERE player_id=$1`, [playerId]);
    const row = result.rows[0];
    return { rank: row ? Number(row.rank) : null, totalPlayers: row ? Number(row.total) : 0,
      topPercent: row ? Math.ceil(Number(row.rank) / Number(row.total) * 1000) / 10 : null };
  }

  public async leaderboard(offset = 0, limit = 20) {
    const result = await this.database.query<LadderRow>(`WITH ladder AS (
      SELECT sp.*,p.gd_account_id::text,p.gd_username,
        RANK() OVER (ORDER BY sp.lp DESC)::int AS rank, COUNT(*) OVER ()::int AS total
      FROM sur_profiles sp JOIN ranked_players p ON p.id=sp.player_id WHERE p.gd_account_id > 0 AND NOT p.is_bot
    ) SELECT * FROM ladder ORDER BY lp DESC,lp_reached_at,player_id LIMIT $1 OFFSET $2`, [limit, offset]);
    const total = Number((await this.database.query<{ total: number }>(`SELECT COUNT(*)::int AS total
      FROM sur_profiles sp JOIN ranked_players p ON p.id=sp.player_id WHERE p.gd_account_id > 0 AND NOT p.is_bot`)).rows[0]?.total ?? 0);
    const status = this.config.getStatus();
    const bands = status.ready ? this.config.getSnapshot().config.tierBands : [];
    return { mode: "SURVIVAL", serverNow: this.clock.now().toISOString(), serverOpen: status.enabled,
      players: result.rows.map(row => ({ gdAccountId: row.gd_account_id, isBot: false, player: row.gd_username,
        rank: Number(row.rank), topPercent: Math.ceil(Number(row.rank) / Number(row.total) * 1000) / 10,
        tier: bands.find(band => band.tier === row.displayed_tier)?.group ?? row.displayed_tier,
        tierId: row.displayed_tier, visibleRankedScore: Number(row.lp), promotion: null,
        placement: null, placementGames: 0, placementGamesRequired: 0, placementResults: [],
        demotionWarning: row.demotion_warning, wins: 0, losses: 0, updatedAt: new Date(row.lp_reached_at).toISOString() })),
      total, totalPlayers: total, offset, hasMore: offset + result.rows.length < total };
  }

  public async history(playerId: string) {
    const matches=await this.database.query<{id:string;finished_at:Date|string;snapshot:unknown;result:unknown}>(
      `SELECT m.id,m.finished_at,m.snapshot,d.result FROM sur_matches m JOIN sur_match_players p ON p.match_id=m.id
       JOIN sur_profiles sp ON sp.player_id=p.player_id LEFT JOIN sur_debug_matches d ON d.match_id=m.id
       WHERE p.player_id=$1 AND m.state='FINAL_RESULT' AND m.finished_at>sp.history_visible_since
       AND (d.match_id IS NULL OR d.settled_at IS NOT NULL) ORDER BY m.finished_at DESC,m.id LIMIT 30`,[playerId]);
    const events = await this.database.query(`SELECT event_key,actual_delta,lp_after,tier_after,awarded_at
      FROM sur_lp_ledger WHERE player_id=$1 ORDER BY awarded_at DESC,id DESC LIMIT 30`, [playerId]);
    return { mode: "SURVIVAL", serverNow: this.clock.now().toISOString(),
      matches:matches.rows.map(row=>({matchId:row.id,mode:"SURVIVAL",finishedAt:new Date(row.finished_at).toISOString(),snapshot:row.snapshot,result:row.result})),lpEvents: events.rows };
  }

  public rejectNewMatch(): never {
    if (!this.config.getStatus().enabled) throw new ServiceUnavailableException({ code: "SURVIVAL_DISABLED", message: "Survival is disabled by operations" });
    throw new NotImplementedException({ code: "SURVIVAL_MATCH_NOT_IMPLEMENTED", message: "Survival matches arrive in M3/M5; no queue or game has been created" });
  }

  /** M5 settlement extension, deliberately absent from HTTP controllers.
   * Use the match's captured config in M5, and keep idempotence keys stable.
   * A transaction updates protected LP and its warning together with the ledger.
   */
  public async settleLp(playerId: string, eventKey: string, delta: number,
    captured:Pick<SurvivalConfigDocument,"generation"|"config"> = this.config.getSnapshot()):Promise<SurvivalRatingUpdate & {eventKey:string;serverNow:string}> {
    if (!eventKey.trim() || eventKey.length > 200 || !Number.isSafeInteger(delta)) throw new BadRequestException("Invalid Survival settlement");
    return this.database.transaction(async transaction => {
      // Shared achievement operations lock the Solo profile before metrics.
      // Maintain that order so a common GOLD unlock cannot deadlock a Claim.
      await transaction.query("SELECT player_id FROM ranked_profiles WHERE player_id=$1 FOR UPDATE", [playerId]);
      const row = (await transaction.query<ProfileRow>("SELECT * FROM sur_profiles WHERE player_id=$1 FOR UPDATE", [playerId])).rows[0];
      if (!row) throw new ConflictException("Enter Survival before settling its LP");
      const existing = (await transaction.query<{ requested_delta: number; result: SurvivalRatingUpdate & {eventKey:string;serverNow:string} }>(
        "SELECT requested_delta,result FROM sur_lp_ledger WHERE player_id=$1 AND event_key=$2", [playerId,eventKey])).rows[0];
      if (existing) {
        if (Number(existing.requested_delta) !== delta) throw new ConflictException("A Survival event cannot change its LP award");
        return existing.result;
      }
      // Stamp arrival when applying LP after acquiring the profile lock, not
      // when a request first arrived and could still be waiting in a transaction.
      const now = this.clock.now();
      const update = updateSurvivalRating({ lp: Number(row.lp), tier: row.displayed_tier, demotionWarning: row.demotion_warning }, delta, captured.config);
      await transaction.query(`UPDATE sur_profiles SET lp=$2,displayed_tier=$3,demotion_warning=$4,updated_at=$5,
        lp_reached_at=CASE WHEN lp<>$2 THEN $5::timestamptz ELSE lp_reached_at END WHERE player_id=$1`,
      [playerId,update.profile.lp,update.profile.tier,update.profile.demotionWarning,now.toISOString()]);
      const result = { ...update, eventKey, serverNow: now.toISOString() };
      await transaction.query(`INSERT INTO sur_lp_ledger
        (id,player_id,event_key,requested_delta,actual_delta,lp_before,lp_after,tier_before,tier_after,
         warning_before,warning_after,config_generation,result,awarded_at)
        VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13::jsonb,$14)`,
      [this.ids.next(),playerId,eventKey,delta,update.actualDelta,Number(row.lp),update.profile.lp,row.displayed_tier,
        update.profile.tier,row.demotion_warning,update.profile.demotionWarning,captured.generation,JSON.stringify(result),now.toISOString()]);
      const group = captured.config.tierBands.find(band => band.tier === update.profile.tier)!.group;
      if (group === "GOLD") await this.achievements.recordSharedTierReached(transaction, playerId, "SURVIVAL", group, eventKey, now);
      return result;
    });
  }
}
