import { Inject, Injectable } from "@nestjs/common";
import { baseRankedTier, goldTierForPosition, goldTierSlots, rankedTierLabel, type DisplayTier } from "@corum-ranked/rules";
import { RankedConfigService } from "../config/ranked-config.service.js";
import { DATABASE, type DatabasePort } from "../database/database.port.js";
import { compareLadderPosition, type LadderPosition } from "./ranking-policy.js";

type RatingMark = "WIN" | "LOSS";

interface LeaderboardRow {
  gd_account_id: string;
  is_bot: boolean;
  gd_username: string;
  displayed_tier: DisplayTier;
  hidden_mmr: number;
  visible_ranked_score: number | null;
  placement_games: number;
  placement_results: unknown;
  wins: number;
  losses: number;
  promotion_source_tier: string | null;
  promotion_target_tier: string | null;
  promotion_games_required: number | null;
  promotion_wins_required: number | null;
  promotion_results: unknown;
  demotion_warning: boolean;
  updated_at: Date | string;
  placement_state: string | null;
  placement_total_stages: number | null;
  placement_current_stage: number | null;
  placement_current_map: number | null;
  placement_completed_stages: number | null;
  placement_completed_maps: number | null;
}

export type PublicPlacementProgress =
  | { readonly state: "NOT_STARTED" | "CANCELLED" }
  | {
      readonly state: string;
      readonly totalStages: 3 | 5;
      readonly currentStageIndex: number;
      readonly currentMapIndex: number;
      readonly completedStages: number;
      readonly completedMaps: number;
    };

export interface PublicLeaderboardPlayer {
  readonly gdAccountId: string;
  readonly isBot: boolean;
  readonly rank: number | null;
  readonly topPercent: number | null;
  readonly player: string;
  readonly tier: DisplayTier;
  readonly visibleRankedScore: number | null;
  readonly placementGames: number;
  readonly placementGamesRequired: number | null;
  readonly placementResults: readonly RatingMark[];
  /** Solo Stage progress. Legacy placementGames/results are not Run progress. */
  readonly placement: PublicPlacementProgress | null;
  readonly promotion: {
    readonly sourceTier: string;
    readonly targetTier: string;
    readonly gamesRequired: number;
    readonly winsRequired: number;
    readonly results: readonly RatingMark[];
  } | null;
  readonly demotionWarning: boolean;
  readonly wins: number;
  readonly losses: number;
  readonly updatedAt: string;
}

interface SortableLeaderboardPlayer {
  readonly player: PublicLeaderboardPlayer;
  readonly hiddenMmr: number;
}

@Injectable()
export class LeaderboardService {
  public constructor(
    @Inject(DATABASE) private readonly database: DatabasePort,
    private readonly config: RankedConfigService,
  ) {}

  public async list(offset = 0, limit?: number, search?:string): Promise<{
    serverNow: string;
    serverOpen: boolean;
    players: readonly PublicLeaderboardPlayer[];
    total: number;
    totalPlayers: number;
    offset: number;
    hasMore: boolean;
    search?:{query:string;found:boolean;gdAccountId:string|null};
  }> {
    const snapshot = this.config.getSnapshotOrNull();
    const placementGamesRequired = snapshot?.operational.mmrPolicy?.placementGames ?? null;
    const result = await this.database.query<LeaderboardRow>(
      `SELECT
         p.gd_account_id::text,
         p.is_bot,
         p.gd_username,
         rp.displayed_tier,
         rp.hidden_mmr,
         rp.visible_ranked_score,
         rp.placement_games,
         rp.placement_results,
         rp.wins,
         rp.losses,
         rp.promotion_source_tier,
         rp.promotion_target_tier,
         rp.promotion_games_required,
         rp.promotion_wins_required,
         rp.promotion_results,
         rp.demotion_warning,
         rp.updated_at,
         placement.state AS placement_state,
         placement.stage_count AS placement_total_stages,
         placement.current_stage_index AS placement_current_stage,
         placement.current_map_index AS placement_current_map,
         placement.completed_stages AS placement_completed_stages,
         placement.completed_maps AS placement_completed_maps
       FROM ranked_profiles rp
       JOIN ranked_players p ON p.id = rp.player_id
       LEFT JOIN LATERAL (
         SELECT pr.state, pr.stage_count, pr.current_stage_index, pr.current_map_index,
                jsonb_array_length(pr.stage_results) AS completed_stages,
                (SELECT COUNT(*) FROM ranked_placement_maps pm
                 WHERE pm.run_id = pr.id AND pm.state = 'MAP_RESULT') AS completed_maps
         FROM ranked_placement_runs pr
         WHERE pr.player_id = rp.player_id
           AND (rp.displayed_tier = 'UNRANKED' OR rp.visible_ranked_score IS NULL)
         ORDER BY pr.created_at DESC, pr.id DESC
         LIMIT 1
       ) placement ON TRUE
       WHERE p.is_bot = TRUE OR p.gd_account_id > 0`,
    );

    const sortable = result.rows.map((row): SortableLeaderboardPlayer => {
      const promotionResults = this.ratingMarks(row.promotion_results);
      const promotion = row.promotion_source_tier && row.promotion_target_tier &&
        row.promotion_games_required !== null && row.promotion_wins_required !== null
        ? {
            sourceTier: row.promotion_source_tier,
            targetTier: row.promotion_target_tier,
            gamesRequired: Number(row.promotion_games_required),
            winsRequired: Number(row.promotion_wins_required),
            results: promotionResults,
          }
        : null;
      // Finalization/skip writes the tier and LP atomically. A later change to
      // the legacy PvP placementGames setting must not unrank a finished Run.
      const placementInProgress = row.displayed_tier === "UNRANKED" ||
        row.visible_ranked_score === null;

      return {
        hiddenMmr: Number(row.hidden_mmr),
        player: {
          gdAccountId: String(row.gd_account_id),
          isBot: Boolean(row.is_bot),
          rank: null,
          topPercent: null,
          player: row.gd_username,
          tier: placementInProgress ? "UNRANKED" : row.displayed_tier,
          visibleRankedScore: placementInProgress ? null : Number(row.visible_ranked_score ?? 0),
          placementGames: Number(row.placement_games),
          placementGamesRequired,
          placementResults: this.ratingMarks(row.placement_results),
          placement: placementInProgress ? this.placementProgress(row) : null,
          promotion: placementInProgress ? null : promotion,
          demotionWarning: placementInProgress ? false : Boolean(row.demotion_warning),
          wins: Number(row.wins),
          losses: Number(row.losses),
          updatedAt: new Date(row.updated_at).toISOString(),
        },
      };
    });

    const ranked = sortable
      .filter(({ player }) => player.tier !== "UNRANKED" && player.visibleRankedScore !== null)
      .sort((left, right) => this.compareRanked(left, right));
    const unranked = sortable
      .filter(({ player }) => player.tier === "UNRANKED" || player.visibleRankedScore === null)
      .sort((left, right) => this.compareUnranked(left, right));

    let rank = 0;
    const goldTotal=ranked.filter(entry=>baseRankedTier(entry.player.tier)==="GOLD").length;
    let goldPosition=0;
    const placed = ranked.map((entry, index) => {
      if (!index || compareLadderPosition(this.position(entry), this.position(ranked[index - 1]!))) rank = index + 1;
      const tier=baseRankedTier(entry.player.tier)==="GOLD"?goldTierForPosition(++goldPosition,goldTotal):entry.player.tier;
      const displayRank=baseRankedTier(tier)==="GOLD"?goldPosition:rank;
      return { ...entry.player, tier, rank:displayRank, topPercent: displayRank / ranked.length * 100 };
    });
    const players = [...placed, ...unranked.map(({ player }) => player)];
    const query=search?.trim().toLowerCase();
    let located=-1;
    if(query){located=players.findIndex(p=>p.player.toLowerCase()===query);if(located<0)located=players.findIndex(p=>p.player.toLowerCase().includes(query));}
    if(located>=0)offset=Math.floor(located/(limit??20))*(limit??20);
    const page = limit === undefined ? players.slice(offset) : players.slice(offset, offset + limit);

    return {
      serverNow: new Date().toISOString(),
      serverOpen: snapshot?.operational.enabled ?? false,
      players: page,
      total: ranked.length,
      totalPlayers: players.length,
      offset,
      hasMore: offset + page.length < players.length,
      ...(query?{search:{query:search!.trim(),found:located>=0,gdAccountId:located>=0?players[located]!.gdAccountId:null}}:{}),
    };
  }

  public async tierGuide() {
    const snapshot=this.config.getSnapshot();
    const ladder=await this.list();
    const gold=ladder.players.filter(p=>baseRankedTier(p.tier)==="GOLD");
    const slots=goldTierSlots(gold.length);
    const cutoff=(tier:DisplayTier)=>{
      const players=gold.filter(p=>p.tier===tier);
      return players.length?Math.min(...players.map(p=>p.visibleRankedScore!)):null;
    };
    return {serverNow:ladder.serverNow,mode:"SOLO_RANKED",generation:snapshot.generation,
      tiers:[...snapshot.operational.tierBands].sort((a,b)=>a.minInclusive-b.minInclusive).map(b=>({
        ...b,label:rankedTierLabel(b.tier),promotionRequired:b.promotionGames>0,
        demotionProtection:!/[12]$/.test(b.tier)})),
      gold:{total:gold.length,championSlots:slots.champion,masterSlots:slots.master,goldSlots:slots.gold,
        championCutoffLp:cutoff("CHAMPION"),masterCutoffLp:cutoff("MASTER"),
        nextTierFromGold:slots.master>0?"MASTER":"CHAMPION",includesBots:true},
      tieBreak:["LP","PROMOTION_STATUS","PROMOTION_WINS","DEMOTION_WARNING","WIN_RATE","HIDDEN_MMR","USERNAME","GD_ACCOUNT_ID"]};
  }

  private placementProgress(row: LeaderboardRow): PublicPlacementProgress | null {
    if (!row.placement_state) return { state: "NOT_STARTED" };
    if (row.placement_state === "CANCELLED") return { state: "CANCELLED" };
    const totalStages = Number(row.placement_total_stages);
    const currentStageIndex = Number(row.placement_current_stage);
    const currentMapIndex = Number(row.placement_current_map);
    const completedStages = Number(row.placement_completed_stages);
    const completedMaps = Number(row.placement_completed_maps);
    if (totalStages !== 3 && totalStages !== 5) return null;
    if (!Number.isInteger(currentStageIndex) || currentStageIndex < 1 || currentStageIndex > totalStages) return null;
    if (currentMapIndex !== 1 && currentMapIndex !== 2) return null;
    if (!Number.isInteger(completedStages) || completedStages < 0 || completedStages > totalStages) return null;
    if (!Number.isInteger(completedMaps) || completedMaps < 0 || completedMaps > totalStages * 2) return null;
    return { state: row.placement_state, totalStages, currentStageIndex, currentMapIndex, completedStages, completedMaps };
  }

  private compareRanked(left: SortableLeaderboardPlayer, right: SortableLeaderboardPlayer): number {
    const difference = compareLadderPosition(this.position(left), this.position(right));
    if (difference) return difference;
    return this.compareRateMmrName(left, right);
  }

  private position(entry: SortableLeaderboardPlayer): LadderPosition {
    const promotion = entry.player.promotion;
    return { score: entry.player.visibleRankedScore ?? 0, promotion_active: promotion !== null,
      promotion_wins: this.promotionWins(entry.player), demotion_warning: !promotion && entry.player.demotionWarning };
  }

  private compareUnranked(left: SortableLeaderboardPlayer, right: SortableLeaderboardPlayer): number {
    return this.compareRateMmrName(left, right);
  }

  private compareRateMmrName(left: SortableLeaderboardPlayer, right: SortableLeaderboardPlayer): number {
    const rateDifference = this.winRate(right.player) - this.winRate(left.player);
    if (rateDifference !== 0) return rateDifference;
    const mmrDifference = right.hiddenMmr - left.hiddenMmr;
    if (mmrDifference !== 0) return mmrDifference;
    const name = left.player.player.toLowerCase(), other = right.player.player.toLowerCase();
    if (name !== other) return name < other ? -1 : 1;
    if (left.player.player !== right.player.player) return left.player.player < right.player.player ? -1 : 1;
    return left.player.gdAccountId < right.player.gdAccountId ? -1 : left.player.gdAccountId > right.player.gdAccountId ? 1 : 0;
  }

  private promotionWins(player: PublicLeaderboardPlayer): number {
    return player.promotion?.results.filter((result) => result === "WIN").length ?? 0;
  }

  private winRate(player: PublicLeaderboardPlayer): number {
    const games = player.wins + player.losses;
    return games > 0 ? player.wins / games : 0;
  }

  private ratingMarks(value: unknown): readonly RatingMark[] {
    let parsed = value;
    if (typeof parsed === "string") {
      try { parsed = JSON.parse(parsed) as unknown; } catch { return []; }
    }
    if (!Array.isArray(parsed)) return [];
    return parsed.filter((mark): mark is RatingMark => mark === "WIN" || mark === "LOSS");
  }
}
