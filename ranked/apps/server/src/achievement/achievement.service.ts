import { BadRequestException, ConflictException, Inject, Injectable } from "@nestjs/common";
import {
  ACHIEVEMENT_DEFINITIONS,
  ONE_SHOT_ACHIEVEMENTS,
  PROGRESSIVE_ACHIEVEMENTS,
  achievementDefinition,
  achievementScopeLabel,
  survivalAchievementFacts, summarizeSurvivalAchievements,
  type SurvivalAchievementFacts, type SurvivalAchievementMetrics, type SurvivalMatch, type AchievementScope, type ProgressiveAchievementId,
  evaluateAchievementMatch,
  isReverseSweep,
  isSweepWin,
  progressiveLevelForMetric,
  progressiveProgressPresentation,
  type AchievementDeathmatchFact,
  type AchievementId,
  type AchievementLevel,
  type AchievementMatchFacts,
  type AchievementMetrics,
  type OneShotAchievementId,
  type ProgressiveAchievementDefinition,
} from "@corum-ranked/rules";
import { ID_GENERATOR, SERVER_CLOCK, type IdGenerator, type ServerClock } from "../common/runtime.module.js";
import { DATABASE, type DatabasePort, type SqlExecutor } from "../database/database.port.js";

interface MetricRow extends SurvivalAchievementMetrics {
  solo_gold_reached:boolean; survival_gold_reached:boolean;
  player_id: string;
  ranked_wins: number;
  ranked_matches: number;
  ranked_clears: number;
  perfect_wins: number;
  current_win_streak: number;
  best_win_streak: number;
  deathmatch_wins: number;
  last_attempt_clears: number;
  reverse_sweeps: number;
  sweep_wins: number;
  promotion_wins: number;
  current_perfect_win_streak: number;
  backfilled_at: Date | string | null;
}

interface UnlockRow {
  achievement_id: AchievementId;
  achievement_level: number;
  reward_ap: number;
  unlocked_at: Date | string;
  claimed_at: Date | string | null;
  source_event_key: string;
}

interface ProfileAchievementRow {
  total_ap: number;
  displayed_tier: string;
  placement_games: number;
  equipped_achievement_id: string | null;
  equipped_achievement_level: number | null;
}

interface HistoricalMatchRow {
  id: string;
  player_a_id: string;
  player_b_id: string;
  winner_id: string;
  series_state: unknown;
  rating_presentation: unknown;
  finished_at: Date | string;
}

interface HistoricalCountRow {
  ranked_clears: number;
  last_attempt_clears: number;
}

interface HistoricalDeathmatchRow {
  score_a: number;
  score_b: number;
  winner_id: string | null;
}

export interface AchievementUnlockNotice {
  readonly achievementId: AchievementId;
  readonly type: "PROGRESSIVE" | "ONE_SHOT";
  readonly level: AchievementLevel | null;
  readonly displayLabel: string | null;
  readonly maxed: boolean;
  readonly rewardAp: number;
  readonly nameKo: string;
  readonly nameEn: string;
  readonly rarity: string | null;
}

export interface AchievementMatchPlayerRecord {
  readonly playerId: string;
  readonly won: boolean;
  readonly rankedClears: number;
  readonly perfectWin: boolean;
  readonly lastAttemptClears: number;
  readonly reverseSweep: boolean;
  readonly sweepWin: boolean;
  readonly promotionSucceeded: boolean;
  readonly reachedGold: boolean;
  readonly deathmatches: readonly AchievementDeathmatchFact[];
}

const parseObject = (value: unknown): Record<string, unknown> => {
  if (typeof value === "string") {
    try {
      const parsed = JSON.parse(value) as unknown;
      return parsed && typeof parsed === "object" && !Array.isArray(parsed)
        ? parsed as Record<string, unknown>
        : {};
    } catch {
      return {};
    }
  }
  return value && typeof value === "object" && !Array.isArray(value)
    ? value as Record<string, unknown>
    : {};
};

const iso = (value: Date | string | null): string | null =>
  value === null ? null : new Date(value).toISOString();

const metricsFromRow = (row: MetricRow): AchievementMetrics => ({
  ranked_wins: Number(row.ranked_wins),
  ranked_matches: Number(row.ranked_matches),
  ranked_clears: Number(row.ranked_clears),
  perfect_wins: Number(row.perfect_wins),
  current_win_streak: Number(row.current_win_streak),
  win_streak: Number(row.best_win_streak),
  deathmatch_wins: Number(row.deathmatch_wins),
  last_attempt_clears: Number(row.last_attempt_clears),
  reverse_sweeps: Number(row.reverse_sweeps),
  sweep_wins: Number(row.sweep_wins),
  promotion_wins: Number(row.promotion_wins),
  current_perfect_win_streak: Number(row.current_perfect_win_streak),
});

const metricValue = (metrics: Partial<Record<ProgressiveAchievementId,number>>, definition: ProgressiveAchievementDefinition): number =>
  metrics[definition.metric] ?? 0;

@Injectable()
export class AchievementService {
  public constructor(
    @Inject(DATABASE) private readonly database: DatabasePort,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
    @Inject(ID_GENERATOR) private readonly ids: IdGenerator,
  ) {}

  public async state(playerId: string) {
    const now = this.clock.now();
    return this.database.transaction(async (transaction) => {
      await this.lockProfile(transaction, playerId);
      await this.ensureBackfilled(transaction, playerId, now);
      return this.buildState(transaction, playerId, now);
    });
  }

  public async claim(playerId:string,achievementId:string) {
    if(!achievementDefinition(achievementId))throw new BadRequestException("Unknown Achievement ID");
    return this.claimRewards(playerId,[achievementId],achievementId);
  }

  public async claimAll(playerId:string,scope:AchievementScope|"ALL") {
    if(!["SOLO_RANKED","SURVIVAL","SHARED","ALL"].includes(scope))throw new BadRequestException("Invalid Achievement scope");
    return this.claimRewards(playerId,ACHIEVEMENT_DEFINITIONS.filter(d=>scope==="ALL"||d.scope===scope).map(d=>d.id));
  }

  private async claimRewards(playerId:string,ids:readonly string[],achievementId?:string) {
    const now=this.clock.now();
    return this.database.transaction(async transaction=>{
      await this.lockProfile(transaction,playerId);await this.ensureBackfilled(transaction,playerId,now);
      const pending=await transaction.query<UnlockRow>(`SELECT achievement_id,achievement_level,reward_ap,unlocked_at,claimed_at,source_event_key
        FROM ranked_achievement_unlocks WHERE player_id=$1 AND achievement_id=ANY($2::text[]) AND claimed_at IS NULL
        ORDER BY achievement_id,achievement_level FOR UPDATE`,[playerId,ids]);
      let claimedAmount=0,claimedRewards=0;const groups=new Map<string,(number|null)[]>();
      for(const reward of pending.rows){
        const inserted=await transaction.query<{amount:number}>(`INSERT INTO ranked_ap_ledger(id,player_id,achievement_id,achievement_level,amount,awarded_at)
          VALUES($1,$2,$3,$4,$5,$6) ON CONFLICT(player_id,achievement_id,achievement_level) DO NOTHING RETURNING amount`,
          [this.ids.next(),playerId,reward.achievement_id,Number(reward.achievement_level),Number(reward.reward_ap),now.toISOString()]);
        if(inserted.rows[0]){claimedAmount+=Number(inserted.rows[0].amount);++claimedRewards;
          const levels=groups.get(reward.achievement_id)??[];levels.push(Number(reward.achievement_level)||null);groups.set(reward.achievement_id,levels);}
        await transaction.query(`UPDATE ranked_achievement_unlocks SET claimed_at=COALESCE(claimed_at,$4)
          WHERE player_id=$1 AND achievement_id=$2 AND achievement_level=$3 AND EXISTS(SELECT 1 FROM ranked_ap_ledger
          WHERE player_id=$1 AND achievement_id=$2 AND achievement_level=$3)`,[playerId,reward.achievement_id,Number(reward.achievement_level),now.toISOString()]);
      }
      return {achievementId,claimedAmount,claimedRewards,duplicate:pending.rows.length===0,
        claimedByAchievement:[...groups].map(([achievementId,levels])=>({achievementId,levels})),
        totalAp:await this.reconcileTotalAp(transaction,playerId),unclaimedRewardCount:await this.unclaimedCount(transaction,playerId),serverNow:now.toISOString()};
    });
  }

  public async equipTitle(playerId: string, achievementId: string, requestedLevel?: number) {
    const definition = achievementDefinition(achievementId);
    if (!definition || !definition.titleEnabled) {
      throw new BadRequestException("Achievement title is unavailable");
    }
    const level = definition.type === "PROGRESSIVE" ? requestedLevel ?? null : 0;
    if (definition.type === "PROGRESSIVE" && (!level || level < 1 || level > 5)) {
      throw new BadRequestException("A progressive title requires level 1-5");
    }
    if (definition.type === "ONE_SHOT" && requestedLevel !== undefined) {
      throw new BadRequestException("A one-shot title has no level");
    }
    const now = this.clock.now();
    return this.database.transaction(async (transaction) => {
      await this.lockProfile(transaction, playerId);
      await this.ensureBackfilled(transaction, playerId, now);
      const unlocked = await transaction.query<{ unlocked: boolean }>(
        `SELECT EXISTS (
           SELECT 1 FROM ranked_achievement_unlocks
           WHERE player_id = $1 AND achievement_id = $2 AND achievement_level = $3
         ) AS unlocked`,
        [playerId, definition.id, level],
      );
      if (!unlocked.rows[0]?.unlocked) {
        throw new ConflictException("Achievement title is still locked");
      }
      await transaction.query(
        `UPDATE ranked_profiles
         SET equipped_achievement_id = $2, equipped_achievement_level = $3, updated_at = $4
         WHERE player_id = $1`,
        [playerId, definition.id, level, now.toISOString()],
      );
      return {
        equippedTitle: this.titleProjection(definition.id, level),
        serverNow: now.toISOString(),
      };
    });
  }

  public async clearTitle(playerId: string) {
    const now = this.clock.now();
    await this.database.query(
      `UPDATE ranked_profiles
       SET equipped_achievement_id = NULL, equipped_achievement_level = NULL, updated_at = $2
       WHERE player_id = $1`,
      [playerId, now.toISOString()],
    );
    return { equippedTitle: null, serverNow: now.toISOString() };
  }

  public async recordMatchResult(
    transaction: SqlExecutor,
    matchId: string,
    records: readonly AchievementMatchPlayerRecord[],
    now: Date,
  ): Promise<Readonly<Record<string, readonly AchievementUnlockNotice[]>>> {
    const notices: Record<string, readonly AchievementUnlockNotice[]> = {};
    for (const record of records) {
      await this.lockProfile(transaction,record.playerId);
      await this.ensureBackfilled(transaction, record.playerId, now);
      notices[record.playerId] = await this.applyMatchEvent(
        transaction,
        record.playerId,
        `match:${matchId}:achievement:${record.playerId}`,
        matchId,
        record,
        now,
      );
    }
    return notices;
  }

  public async recordPlacementCompletion(
    transaction: SqlExecutor,
    playerId: string,
    runId: string,
    reachedGold: boolean,
    now: Date,
  ): Promise<readonly AchievementUnlockNotice[]> {
    await this.lockProfile(transaction, playerId);
    await this.ensureBackfilled(transaction, playerId, now);
    if (!await this.eligible(transaction,playerId,now)) return [];
    const eventKey = `placement:${runId}:achievement:${playerId}`;
    const inserted = await this.insertEvent(
      transaction, eventKey, playerId, "PLACEMENT_RESULT", runId, now,
    );
    if (!inserted) return [];
    if(reachedGold)await this.markGold(transaction,playerId,"SOLO_RANKED");
    const notices=await this.unlockOneShots(
      transaction,
      playerId,
      reachedGold ? ["placement_complete", "gold_reached"] : ["placement_complete"],
      eventKey,
      now,
    );
    return [...notices,...await this.refreshV2Unlocks(transaction,playerId,eventKey,now)];
  }

  public titleProjection(achievementId: string | null, level: number | null) {
    if (!achievementId || level === null) return null;
    const definition = achievementDefinition(achievementId);
    if (!definition) return null;
    if (definition.type === "ONE_SHOT") {
      if (level !== 0) return null;
      return {
        achievementId: definition.id,
        scope: definition.scope,
        level: null,
        displayLabel: null,
        nameKo: `[${achievementScopeLabel(definition.scope, "ko")}] ${definition.nameKo}`,
        nameEn: `[${achievementScopeLabel(definition.scope, "en")}] ${definition.nameEn}`,
        displayColor: null,
        rarity: definition.rarity,
      };
    }
    const stage = definition.levels[level - 1];
    if (!stage) return null;
    return {
      achievementId: definition.id,
        scope: definition.scope,
      level: stage.level,
      displayLabel: stage.displayLabel,
      nameKo: `[${achievementScopeLabel(definition.scope, "ko")}] ${definition.nameKo}`,
      nameEn: `[${achievementScopeLabel(definition.scope, "en")}] ${definition.nameEn}`,
      displayColor: stage.displayColor,
      rarity: null,
    };
  }

  public async noticesForSource(
    executor: SqlExecutor,
    playerId: string,
    sourceEventKey: string,
  ): Promise<readonly AchievementUnlockNotice[]> {
    const rows = await executor.query<UnlockRow>(
      `SELECT achievement_id, achievement_level, reward_ap, unlocked_at, claimed_at, source_event_key
       FROM ranked_achievement_unlocks
       WHERE player_id = $1 AND source_event_key = $2
       ORDER BY unlocked_at, achievement_id, achievement_level`,
      [playerId, sourceEventKey],
    );
    const notices: AchievementUnlockNotice[] = [];
    for (const unlock of rows.rows) {
      const definition = achievementDefinition(unlock.achievement_id);
      if (!definition) continue;
      if (definition.type === "ONE_SHOT") {
        notices.push({
          achievementId: definition.id,
          type: "ONE_SHOT",
          level: null,
          displayLabel: null,
          maxed: false,
          rewardAp: Number(unlock.reward_ap),
          nameKo: definition.nameKo,
          nameEn: definition.nameEn,
          rarity: definition.rarity,
        });
        continue;
      }
      const level = definition.levels[Number(unlock.achievement_level) - 1];
      if (!level) continue;
      notices.push({
        achievementId: definition.id,
        type: "PROGRESSIVE",
        level: level.level,
        displayLabel: level.displayLabel,
        maxed: level.level === 5,
        rewardAp: Number(unlock.reward_ap),
        nameKo: definition.nameKo,
        nameEn: definition.nameEn,
        rarity: null,
      });
    }
    return notices;
  }

  private async applyMatchEvent(
    transaction: SqlExecutor,
    playerId: string,
    eventKey: string,
    matchId: string,
    facts: AchievementMatchFacts,
    now: Date,
  ): Promise<readonly AchievementUnlockNotice[]> {
    if(!await this.eligible(transaction,playerId,now))return [];
    const inserted = await this.insertEvent(
      transaction, eventKey, playerId, "MATCH_RESULT", matchId, now,
    );
    if (!inserted) return [];
    const row = await this.lockMetrics(transaction, playerId);
    const evaluation = evaluateAchievementMatch(metricsFromRow(row), facts);
    await this.writeMetrics(transaction, playerId, evaluation.metrics, now);
    const progressive = await this.unlockProgressive(
      transaction, playerId, evaluation.metrics, eventKey, now,
    );
    const oneShots = await this.unlockOneShots(
      transaction, playerId, evaluation.oneShots, eventKey, now,
    );
    if(facts.reachedGold)await this.markGold(transaction,playerId,"SOLO_RANKED");
    return [...progressive, ...oneShots,...await this.refreshV2Unlocks(transaction,playerId,eventKey,now)];
  }

  private async cutover(transaction:SqlExecutor):Promise<Date> {
    const row=(await transaction.query<{activated_at:Date|string}>("SELECT activated_at FROM ranked_achievement_versions WHERE version=2")).rows[0];
    if(!row)throw new ConflictException("Achievement v2 migration is required");return new Date(row.activated_at);
  }
  private async eligible(transaction:SqlExecutor,playerId:string,at:Date):Promise<boolean>{
    if(!Number.isFinite(at.getTime()) || at<await this.cutover(transaction))return false;
    return !!(await transaction.query<{ok:boolean}>("SELECT TRUE AS ok FROM ranked_players WHERE id=$1 AND gd_account_id>0 AND NOT is_bot",[playerId])).rows[0];
  }
  private async ensureBackfilled(transaction:SqlExecutor,playerId:string,now:Date):Promise<void>{
    await transaction.query("INSERT INTO ranked_achievement_metrics(player_id) VALUES($1) ON CONFLICT(player_id) DO NOTHING",[playerId]);
    const row=await this.lockMetrics(transaction,playerId);if(row.backfilled_at)return;
    const since=await this.cutover(transaction);
    // v2 starts at the explicit migration cutover, never resurrecting v1 rewards.
    if(await this.eligible(transaction,playerId,now)){
      const matches=await transaction.query<HistoricalMatchRow>(`SELECT id,player_a_id,player_b_id,winner_id,series_state,rating_presentation,finished_at
        FROM ranked_matches WHERE match_type='RANKED_PVP' AND state='MATCH_RESULT' AND result_applied_at IS NOT NULL
        AND finished_at >= $2 AND (player_a_id=$1 OR player_b_id=$1) ORDER BY finished_at,id`,[playerId,since.toISOString()]);
      for(const match of matches.rows)await this.applyMatchEvent(transaction,playerId,`match:${match.id}:achievement:${playerId}`,match.id,
        await this.historicalMatchFacts(transaction,match,playerId),new Date(match.finished_at));
      const placements=await transaction.query<{id:string;finalized_at:Date|string;final_displayed_tier:string}>(`SELECT id,finalized_at,final_displayed_tier FROM ranked_placement_runs
        WHERE player_id=$1 AND completion_kind='COMPLETED' AND finalized_at >= $2 ORDER BY finalized_at,id`,[playerId,since.toISOString()]);
      for(const run of placements.rows){const key=`placement:${run.id}:achievement:${playerId}`;
        if(await this.insertEvent(transaction,key,playerId,"PLACEMENT_RESULT",run.id,new Date(run.finalized_at))){
          if(run.final_displayed_tier==="GOLD")await this.markGold(transaction,playerId,"SOLO_RANKED");
          await this.unlockOneShots(transaction,playerId,["placement_complete"],key,new Date(run.finalized_at));}}
      const matchesSurvival=await transaction.query<{match_id:string}>(`SELECT o.match_id FROM sur_online_players o JOIN sur_matches m ON m.id=o.match_id
        WHERE o.player_id=$1 AND o.settled_at IS NOT NULL AND m.state='FINAL_RESULT' AND m.finished_at >= $2 ORDER BY m.finished_at,m.id`,[playerId,since.toISOString()]);
      for(const match of matchesSurvival.rows)await this.applySurvivalEvent(transaction,match.match_id,playerId,now);
      await this.refreshV2Unlocks(transaction,playerId,`v2:sync:${playerId}`,now);
    }
    await transaction.query("UPDATE ranked_achievement_metrics SET backfilled_at=$2,updated_at=$2 WHERE player_id=$1",[playerId,now.toISOString()]);
  }
  private allMetrics(row:MetricRow):Partial<Record<ProgressiveAchievementId,number>>{
    const solo=metricsFromRow(row);return {...solo,
      survival_matches:Number(row.survival_matches),survival_wins:Number(row.survival_wins),survival_podiums:Number(row.survival_podiums),
      survival_finals:Number(row.survival_finals),survival_round_survives:Number(row.survival_round_survives),survival_round_wins:Number(row.survival_round_wins),
      survival_podium_streak:Number(row.survival_podium_streak),survival_win_streak:Number(row.survival_win_streak),
      shared_matches:solo.ranked_matches+Number(row.survival_matches),shared_wins:solo.ranked_wins+Number(row.survival_wins),
      shared_mastery:solo.ranked_clears+Number(row.survival_round_survives)};
  }
  private async markGold(tx:SqlExecutor,playerId:string,mode:"SOLO_RANKED"|"SURVIVAL"){
    if(mode==="SOLO_RANKED")await tx.query("UPDATE ranked_achievement_metrics SET solo_gold_reached=TRUE WHERE player_id=$1",[playerId]);
    else await tx.query("UPDATE ranked_achievement_metrics SET survival_gold_reached=TRUE WHERE player_id=$1",[playerId]);
  }
  private async refreshV2Unlocks(tx:SqlExecutor,playerId:string,key:string,now:Date):Promise<readonly AchievementUnlockNotice[]>{
    const row=await this.lockMetrics(tx,playerId),metrics=this.allMetrics(row),shots:OneShotAchievementId[]=[];
    if(row.solo_gold_reached||row.survival_gold_reached)shots.push("gold_reached");
    if(row.solo_gold_reached&&row.survival_gold_reached)shots.push("dual_gold_reached");
    if(row.ranked_matches>0&&row.survival_matches>0)shots.push("dual_mode_debut");
    if(row.ranked_wins>0&&row.survival_wins>0)shots.push("dual_mode_winner");
    const mastered=(scope:AchievementScope)=>PROGRESSIVE_ACHIEVEMENTS.some(d=>d.scope===scope&&metricValue(metrics,d)>=d.levels[4]!.threshold);
    if(mastered("SOLO_RANKED")&&mastered("SURVIVAL"))shots.push("master_of_two_worlds");
    return [...await this.unlockProgressive(tx,playerId,metrics,key,now),...await this.unlockOneShots(tx,playerId,shots,key,now)];
  }
  public async recordSurvivalResult(tx:SqlExecutor,matchId:string,playerId:string,now:Date):Promise<readonly AchievementUnlockNotice[]>{
    await this.lockProfile(tx,playerId);await this.ensureBackfilled(tx,playerId,now);
    return this.applySurvivalEvent(tx,matchId,playerId,now);
  }
  private async applySurvivalEvent(tx:SqlExecutor,matchId:string,playerId:string,now:Date):Promise<readonly AchievementUnlockNotice[]>{
    const row=(await tx.query<{snapshot:unknown;result:unknown}>(`SELECT m.snapshot,o.result FROM sur_matches m
      JOIN sur_online_players o ON o.match_id=m.id JOIN ranked_players p ON p.id=o.player_id
      WHERE m.id=$1 AND o.player_id=$2 AND m.state='FINAL_RESULT' AND o.settled_at IS NOT NULL AND o.result IS NOT NULL
      AND NOT p.is_bot AND p.gd_account_id>0 AND NOT EXISTS(SELECT 1 FROM sur_debug_matches d WHERE d.match_id=m.id)`,[matchId,playerId])).rows[0];
    if(!row)return [];
    const snapshot=parseObject(row.snapshot) as unknown as SurvivalMatch;
    if(snapshot.id!==matchId || !Array.isArray(snapshot.players)||!Array.isArray(snapshot.rounds))return [];
    const facts=survivalAchievementFacts(snapshot,playerId);
    if(!facts||!await this.eligible(tx,playerId,new Date(facts.completedAt)))return [];
    const key=`survival:${matchId}:achievement:${playerId}`;
    if(!await this.insertEvent(tx,key,playerId,"SURVIVAL_RESULT",matchId,new Date(facts.completedAt)))return this.noticesForSource(tx,playerId,key);
    await tx.query(`INSERT INTO ranked_survival_achievement_facts(player_id,match_id,completed_at,facts) VALUES($1,$2,$3,$4::jsonb)
      ON CONFLICT(player_id,match_id) DO NOTHING`,[playerId,matchId,new Date(facts.completedAt).toISOString(),JSON.stringify(facts)]);
    const all=await tx.query<{facts:SurvivalAchievementFacts}>("SELECT facts FROM ranked_survival_achievement_facts WHERE player_id=$1 ORDER BY completed_at,match_id",[playerId]);
    const metrics=summarizeSurvivalAchievements(all.rows.map(r=>r.facts));
    await tx.query(`UPDATE ranked_achievement_metrics SET survival_matches=$2,survival_wins=$3,survival_podiums=$4,survival_finals=$5,
      survival_round_survives=$6,survival_round_wins=$7,current_survival_podium_streak=$8,survival_podium_streak=$9,
      current_survival_win_streak=$10,survival_win_streak=$11,updated_at=$12 WHERE player_id=$1`,
      [playerId,metrics.survival_matches,metrics.survival_wins,metrics.survival_podiums,metrics.survival_finals,metrics.survival_round_survives,
        metrics.survival_round_wins,metrics.current_survival_podium_streak,metrics.survival_podium_streak,metrics.current_survival_win_streak,metrics.survival_win_streak,now.toISOString()]);
    const result=parseObject(row.result),after=parseObject(result.profileAfter);
    if(["GOLD","MASTER","CHAMPION"].includes(String(after.tier)))await this.markGold(tx,playerId,"SURVIVAL");
    await this.unlockOneShots(tx,playerId,facts.oneShots,key,now);await this.refreshV2Unlocks(tx,playerId,key,now);
    return this.noticesForSource(tx,playerId,key);
  }

  private async historicalMatchFacts(
    transaction: SqlExecutor,
    match: HistoricalMatchRow,
    playerId: string,
  ): Promise<AchievementMatchFacts> {
    const ownIsA = match.player_a_id === playerId;
    const side = ownIsA ? "A" : "B";
    const counts = await transaction.query<HistoricalCountRow>(
      `SELECT
         (SELECT COUNT(*)::int FROM ranked_attempts a
            JOIN ranked_rounds r ON r.id = a.round_id
          WHERE r.match_id = $1 AND a.player_id = $2 AND a.valid AND a.cleared AND a.ended_at IS NOT NULL)
         +
         (SELECT COUNT(*)::int FROM ranked_deathmatch_attempts a
            JOIN ranked_deathmatches d ON d.id = a.deathmatch_id
          WHERE d.match_id = $1 AND a.player_id = $2 AND a.valid AND a.cleared AND a.ended_at IS NOT NULL)
           AS ranked_clears,
         (SELECT COUNT(*)::int FROM ranked_attempts a
            JOIN ranked_rounds r ON r.id = a.round_id
          WHERE r.match_id = $1 AND a.player_id = $2 AND a.valid AND a.cleared
            AND a.ended_at IS NOT NULL AND r.last_attempt_window_start IS NOT NULL
            AND a.server_accepted_start_at >= r.last_attempt_window_start)
           AS last_attempt_clears`,
      [match.id, playerId],
    );
    const deathmatches = await transaction.query<HistoricalDeathmatchRow>(
      `SELECT score_a, score_b, winner_id FROM ranked_deathmatches
       WHERE match_id = $1 AND finished_at IS NOT NULL ORDER BY sequence`,
      [match.id],
    );
    const series = parseObject(match.series_state);
    const results = Array.isArray(series.roundResults)
      ? series.roundResults.filter((item): item is string => typeof item === "string")
      : [];
    const won = match.winner_id === playerId;
    const winnerSide = won ? side : side === "A" ? "B" : "A";
    const rating = parseObject(match.rating_presentation);
    const events = parseObject(parseObject(rating.events)[side]);
    const after = parseObject(parseObject(rating.profileAfter)[side]);
    return {
      won,
      rankedClears: Number(counts.rows[0]?.ranked_clears ?? 0),
      perfectWin: rating.perfectWinSide === side,
      lastAttemptClears: Number(counts.rows[0]?.last_attempt_clears ?? 0),
      reverseSweep: won && isReverseSweep(results, winnerSide),
      sweepWin: won && isSweepWin(results, winnerSide),
      promotionSucceeded: events.promotionSucceeded === true,
      reachedGold: after.displayedTier === "GOLD",
      deathmatches: deathmatches.rows.map((deathmatch) => ({
        outcome: deathmatch.winner_id === null
          ? "TIE"
          : deathmatch.winner_id === playerId ? "WIN" : "LOSS",
        ownBest: Number(ownIsA ? deathmatch.score_a : deathmatch.score_b),
        opponentBest: Number(ownIsA ? deathmatch.score_b : deathmatch.score_a),
      })),
    };
  }

  private async buildState(transaction: SqlExecutor, playerId: string, now: Date) {
    const metricRow = await this.lockMetrics(transaction, playerId);
    const metrics = this.allMetrics(metricRow);
    const unlocks = await transaction.query<UnlockRow>(
      `SELECT achievement_id, achievement_level, reward_ap, unlocked_at, claimed_at, source_event_key
       FROM ranked_achievement_unlocks WHERE player_id = $1
       ORDER BY unlocked_at, achievement_id, achievement_level`,
      [playerId],
    );
    const row = await this.lockProfile(transaction, playerId);
    const totalAp = await this.reconcileTotalAp(transaction, playerId);
    const unclaimedRewardCount = unlocks.rows.filter((unlock) => unlock.claimed_at === null).length;
    return {
      totalAp,
      unclaimedRewardCount,
      equippedTitle: this.titleProjection(
        row.equipped_achievement_id,
        row.equipped_achievement_level,
      ),
      achievements: ACHIEVEMENT_DEFINITIONS.map((definition) => {
        const ownUnlocks = unlocks.rows.filter((unlock) => unlock.achievement_id === definition.id);
        const common = {
          id: definition.id,
          type: definition.type,
          scope: definition.scope,
          nameKo: definition.nameKo,
          nameEn: definition.nameEn,
          descriptionKo: definition.descriptionKo,
          descriptionEn: definition.descriptionEn,
          titleEnabled: definition.titleEnabled,
          unlocks: ownUnlocks.map((unlock) => ({
            level: Number(unlock.achievement_level) === 0 ? null : Number(unlock.achievement_level),
            rewardAp: Number(unlock.reward_ap),
            unlockedAt: iso(unlock.unlocked_at),
            claimedAt: iso(unlock.claimed_at),
          })),
          claimableRewards: ownUnlocks.filter((unlock) => unlock.claimed_at === null).length,
          claimableAp: ownUnlocks
            .filter((unlock) => unlock.claimed_at === null)
            .reduce((sum, unlock) => sum + Number(unlock.reward_ap), 0),
        };
        if (definition.type === "ONE_SHOT") {
          return {
            ...common,
            rarity: definition.rarity,
            rewardAp: definition.ap,
            unlocked: ownUnlocks.length > 0,
            claimed: ownUnlocks.some((unlock) => unlock.claimed_at !== null),
          };
        }
        const value = metricValue(metrics, definition);
        // Admin can grant rewards without fabricating played-match metrics.
        // Owned levels still need to be selectable/displayed as unlocked.
        const owned = Math.max(0, ...ownUnlocks.map(unlock => Number(unlock.achievement_level)));
        const threshold = definition.levels.find(level => level.level === owned)?.threshold ?? 0;
        const progress = progressiveProgressPresentation(definition, Math.max(value, threshold));
        return {
          ...common,
          levels: definition.levels,
          progress: { ...progress, metricValue: value },
        };
      }),
      serverNow: now.toISOString(),
    };
  }

  private async unlockProgressive(
    transaction: SqlExecutor,
    playerId: string,
    metrics: Partial<Record<ProgressiveAchievementId,number>>,
    eventKey: string,
    now: Date,
  ): Promise<readonly AchievementUnlockNotice[]> {
    const notices: AchievementUnlockNotice[] = [];
    for (const definition of PROGRESSIVE_ACHIEVEMENTS) {
      const attained = progressiveLevelForMetric(definition, metricValue(metrics, definition));
      for (const level of definition.levels) {
        if (level.level > attained) break;
        const inserted = await transaction.query<{ achievement_id: AchievementId }>(
          `INSERT INTO ranked_achievement_unlocks (
             player_id, achievement_id, achievement_level, reward_ap, unlocked_at, source_event_key
           ) VALUES ($1, $2, $3, $4, $5, $6)
           ON CONFLICT (player_id, achievement_id, achievement_level) DO NOTHING
           RETURNING achievement_id`,
          [playerId, definition.id, level.level, level.ap, now.toISOString(), eventKey],
        );
        if (inserted.rows[0]) {
          notices.push({
            achievementId: definition.id,
            type: "PROGRESSIVE",
            level: level.level,
            displayLabel: level.displayLabel,
            maxed: level.level === 5,
            rewardAp: level.ap,
            nameKo: definition.nameKo,
            nameEn: definition.nameEn,
            rarity: null,
          });
        }
      }
    }
    return notices;
  }

  private async unlockOneShots(
    transaction: SqlExecutor,
    playerId: string,
    achievementIds: readonly OneShotAchievementId[],
    eventKey: string,
    now: Date,
  ): Promise<readonly AchievementUnlockNotice[]> {
    const notices: AchievementUnlockNotice[] = [];
    for (const achievementId of new Set(achievementIds)) {
      const definition = ONE_SHOT_ACHIEVEMENTS.find((item) => item.id === achievementId);
      if (!definition) continue;
      const inserted = await transaction.query<{ achievement_id: AchievementId }>(
        `INSERT INTO ranked_achievement_unlocks (
           player_id, achievement_id, achievement_level, reward_ap, unlocked_at, source_event_key
         ) VALUES ($1, $2, 0, $3, $4, $5)
         ON CONFLICT (player_id, achievement_id, achievement_level) DO NOTHING
         RETURNING achievement_id`,
        [playerId, definition.id, definition.ap, now.toISOString(), eventKey],
      );
      if (inserted.rows[0]) {
        notices.push({
          achievementId: definition.id,
          type: "ONE_SHOT",
          level: null,
          displayLabel: null,
          maxed: false,
          rewardAp: definition.ap,
          nameKo: definition.nameKo,
          nameEn: definition.nameEn,
          rarity: definition.rarity,
        });
      }
    }
    return notices;
  }

  private async insertEvent(
    transaction: SqlExecutor,
    eventKey: string,
    playerId: string,
    sourceType: "MATCH_RESULT" | "PLACEMENT_RESULT" | "BACKFILL" | "SHARED_TIER_REACHED" | "SURVIVAL_RESULT",
    sourceId: string | null,
    now: Date,
  ): Promise<boolean> {
    const result = await transaction.query<{ event_key: string }>(
      `INSERT INTO ranked_achievement_events (
         event_key, player_id, source_type, source_id, occurred_at
       ) VALUES ($1, $2, $3, $4, $5)
       ON CONFLICT (event_key) DO NOTHING RETURNING event_key`,
      [eventKey, playerId, sourceType, sourceId, now.toISOString()],
    );
    return Boolean(result.rows[0]);
  }

  private async lockMetrics(transaction: SqlExecutor, playerId: string): Promise<MetricRow> {
    const result = await transaction.query<MetricRow>(
      "SELECT * FROM ranked_achievement_metrics WHERE player_id = $1 FOR UPDATE",
      [playerId],
    );
    const row = result.rows[0];
    if (!row) throw new Error("Achievement metric row is missing");
    return row;
  }

  private async lockProfile(
    transaction: SqlExecutor,
    playerId: string,
  ): Promise<ProfileAchievementRow> {
    const result = await transaction.query<ProfileAchievementRow>(
      `SELECT total_ap, displayed_tier, placement_games,
              equipped_achievement_id, equipped_achievement_level
       FROM ranked_profiles WHERE player_id = $1 FOR UPDATE`,
      [playerId],
    );
    const row = result.rows[0];
    if (!row) throw new ConflictException("Ranked profile is missing");
    return row;
  }

  private async writeMetrics(
    transaction: SqlExecutor,
    playerId: string,
    metrics: AchievementMetrics,
    now: Date,
  ): Promise<void> {
    await transaction.query(
      `UPDATE ranked_achievement_metrics SET
         ranked_wins = $2, ranked_matches = $3, ranked_clears = $4,
         perfect_wins = $5, current_win_streak = $6, best_win_streak = $7,
         deathmatch_wins = $8, last_attempt_clears = $9,
         reverse_sweeps = $10, sweep_wins = $11, promotion_wins = $12,
         current_perfect_win_streak = $13, updated_at = $14
       WHERE player_id = $1`,
      [
        playerId, metrics.ranked_wins, metrics.ranked_matches, metrics.ranked_clears,
        metrics.perfect_wins, metrics.current_win_streak, metrics.win_streak,
        metrics.deathmatch_wins, metrics.last_attempt_clears, metrics.reverse_sweeps,
        metrics.sweep_wins, metrics.promotion_wins, metrics.current_perfect_win_streak,
        now.toISOString(),
      ],
    );
  }

  private async unclaimedCount(transaction: SqlExecutor, playerId: string): Promise<number> {
    const result = await transaction.query<{ count: number }>(
      `SELECT COUNT(*)::int AS count FROM ranked_achievement_unlocks
       WHERE player_id = $1 AND claimed_at IS NULL`,
      [playerId],
    );
    return Number(result.rows[0]?.count ?? 0);
  }

  private async reconcileTotalAp(transaction: SqlExecutor, playerId: string): Promise<number> {
    const sum = await transaction.query<{ total: number }>(
      "SELECT COALESCE(SUM(amount), 0)::int AS total FROM ranked_ap_ledger WHERE player_id = $1",
      [playerId],
    );
    const total = Number(sum.rows[0]?.total ?? 0);
    await transaction.query(
      "UPDATE ranked_profiles SET total_ap = $2 WHERE player_id = $1 AND total_ap IS DISTINCT FROM $2",
      [playerId, total],
    );
    return total;
  }
}
