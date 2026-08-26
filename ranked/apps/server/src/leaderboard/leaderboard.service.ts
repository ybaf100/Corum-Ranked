import { Inject, Injectable } from "@nestjs/common";
import type { DisplayTier } from "@corum-ranked/rules";
import { RankedConfigService } from "../config/ranked-config.service.js";
import { DATABASE, type DatabasePort } from "../database/database.port.js";

type RatingMark = "WIN" | "LOSS";

interface LeaderboardRow {
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
}

export interface PublicLeaderboardPlayer {
  readonly player: string;
  readonly tier: DisplayTier;
  readonly visibleRankedScore: number | null;
  readonly placementGames: number;
  readonly placementGamesRequired: number | null;
  readonly placementResults: readonly RatingMark[];
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

const TIER_RANK: Readonly<Record<DisplayTier, number>> = {
  UNRANKED: 0,
  RED: 1,
  AQUA: 2,
  BRONZE: 3,
  SILVER: 4,
  GOLD: 5,
};

@Injectable()
export class LeaderboardService {
  public constructor(
    @Inject(DATABASE) private readonly database: DatabasePort,
    private readonly config: RankedConfigService,
  ) {}

  public async list(): Promise<{
    serverNow: string;
    serverOpen: boolean;
    players: readonly PublicLeaderboardPlayer[];
  }> {
    const snapshot = this.config.getSnapshotOrNull();
    const placementGamesRequired = snapshot?.operational.mmrPolicy?.placementGames ?? null;
    const result = await this.database.query<LeaderboardRow>(
      `SELECT
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
         rp.updated_at
       FROM ranked_profiles rp
       JOIN ranked_players p ON p.id = rp.player_id
       WHERE p.is_bot = FALSE
         AND p.gd_account_id > 0`,
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
      const placementInProgress = row.displayed_tier === "UNRANKED" ||
        row.visible_ranked_score === null ||
        (placementGamesRequired !== null && Number(row.placement_games) < placementGamesRequired);

      return {
        hiddenMmr: Number(row.hidden_mmr),
        player: {
          player: row.gd_username,
          tier: placementInProgress ? "UNRANKED" : row.displayed_tier,
          visibleRankedScore: placementInProgress ? null : Number(row.visible_ranked_score ?? 0),
          placementGames: Number(row.placement_games),
          placementGamesRequired,
          placementResults: this.ratingMarks(row.placement_results),
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

    return {
      serverNow: new Date().toISOString(),
      serverOpen: snapshot?.operational.enabled ?? false,
      players: [...ranked, ...unranked].map(({ player }) => player),
    };
  }

  private compareRanked(left: SortableLeaderboardPlayer, right: SortableLeaderboardPlayer): number {
    const tierDifference = TIER_RANK[right.player.tier] - TIER_RANK[left.player.tier];
    if (tierDifference !== 0) return tierDifference;

    if (left.player.promotion && right.player.promotion) {
      const promotionWinDifference = this.promotionWins(right.player) - this.promotionWins(left.player);
      if (promotionWinDifference !== 0) return promotionWinDifference;
      return this.compareRateMmrName(left, right);
    }

    if (left.player.promotion || right.player.promotion) {
      return left.player.promotion ? -1 : 1;
    }

    const scoreDifference = Number(right.player.visibleRankedScore ?? 0) -
      Number(left.player.visibleRankedScore ?? 0);
    if (scoreDifference !== 0) return scoreDifference;
    return this.compareRateMmrName(left, right);
  }

  private compareUnranked(left: SortableLeaderboardPlayer, right: SortableLeaderboardPlayer): number {
    return this.compareRateMmrName(left, right);
  }

  private compareRateMmrName(left: SortableLeaderboardPlayer, right: SortableLeaderboardPlayer): number {
    const rateDifference = this.winRate(right.player) - this.winRate(left.player);
    if (rateDifference !== 0) return rateDifference;
    const mmrDifference = right.hiddenMmr - left.hiddenMmr;
    if (mmrDifference !== 0) return mmrDifference;
    return left.player.player.localeCompare(right.player.player, "en", { sensitivity: "base" });
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
