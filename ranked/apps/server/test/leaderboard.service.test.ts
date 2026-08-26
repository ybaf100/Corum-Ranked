import { describe, expect, it } from "vitest";
import type { RankedConfigService } from "../src/config/ranked-config.service.js";
import type { DatabasePort, SqlResult } from "../src/database/database.port.js";
import { LeaderboardService } from "../src/leaderboard/leaderboard.service.js";
import { configDocumentFixture } from "./fixtures.js";

const row = (overrides: Record<string, unknown>) => ({
  gd_username: "player",
  displayed_tier: "AQUA",
  hidden_mmr: 1_000,
  visible_ranked_score: 1_500,
  placement_games: 5,
  placement_results: ["WIN", "WIN", "LOSS", "WIN", "WIN"],
  wins: 5,
  losses: 1,
  promotion_source_tier: null,
  promotion_target_tier: null,
  promotion_games_required: null,
  promotion_wins_required: null,
  promotion_results: [],
  demotion_warning: false,
  updated_at: "2026-08-26T00:00:00.000Z",
  ...overrides,
});

describe("LeaderboardService", () => {
  it("sorts ranked and Unranked players while keeping MMR and draws private", async () => {
    let capturedSql = "";
    const database: DatabasePort = {
      query: async <Row>(sql: string): Promise<SqlResult<Row>> => {
        capturedSql = sql;
        const rows = [
          row({ gd_username: "aqua-normal", visible_ranked_score: 1_999, hidden_mmr: 9_000 }),
          row({
            gd_username: "promo-one-win",
            visible_ranked_score: 1_999,
            hidden_mmr: 9_500,
            wins: 9,
            losses: 1,
            promotion_source_tier: "AQUA",
            promotion_target_tier: "BRONZE",
            promotion_games_required: 3,
            promotion_wins_required: 2,
            promotion_results: ["WIN", "LOSS"],
          }),
          row({
            gd_username: "promo-two-wins",
            visible_ranked_score: 1_999,
            hidden_mmr: 900,
            wins: 5,
            losses: 5,
            promotion_source_tier: "AQUA",
            promotion_target_tier: "BRONZE",
            promotion_games_required: 3,
            promotion_wins_required: 2,
            promotion_results: ["WIN", "WIN"],
          }),
          row({
            gd_username: "promo-tie-low-mmr",
            visible_ranked_score: 1_999,
            hidden_mmr: 1_500,
            wins: 3,
            losses: 1,
            promotion_source_tier: "AQUA",
            promotion_target_tier: "BRONZE",
            promotion_games_required: 3,
            promotion_wins_required: 2,
            promotion_results: ["WIN"],
          }),
          row({
            gd_username: "z-promo-tie-high-mmr",
            visible_ranked_score: 1_999,
            hidden_mmr: 2_000,
            wins: 3,
            losses: 1,
            promotion_source_tier: "AQUA",
            promotion_target_tier: "BRONZE",
            promotion_games_required: 3,
            promotion_wins_required: 2,
            promotion_results: ["WIN"],
          }),
          row({
            gd_username: "a-promo-tie-high-mmr",
            visible_ranked_score: 1_999,
            hidden_mmr: 2_000,
            wins: 3,
            losses: 1,
            promotion_source_tier: "AQUA",
            promotion_target_tier: "BRONZE",
            promotion_games_required: 3,
            promotion_wins_required: 2,
            promotion_results: ["WIN"],
          }),
          row({
            gd_username: "gold-player",
            displayed_tier: "GOLD",
            visible_ranked_score: 4_100,
          }),
          row({
            gd_username: "unranked-low-rate",
            displayed_tier: "UNRANKED",
            visible_ranked_score: 0,
            hidden_mmr: 5_000,
            placement_games: 4,
            wins: 1,
            losses: 3,
          }),
          row({
            gd_username: "unranked-low-mmr",
            displayed_tier: "UNRANKED",
            visible_ranked_score: 0,
            hidden_mmr: 1_000,
            placement_games: 4,
            wins: 3,
            losses: 1,
          }),
          row({
            gd_username: "z-unranked-high-mmr",
            displayed_tier: "UNRANKED",
            visible_ranked_score: 0,
            hidden_mmr: 2_000,
            placement_games: 4,
            wins: 3,
            losses: 1,
          }),
          row({
            gd_username: "a-unranked-high-mmr",
            displayed_tier: "UNRANKED",
            visible_ranked_score: 0,
            hidden_mmr: 2_000,
            placement_games: 4,
            wins: 3,
            losses: 1,
          }),
        ];
        return {
          rowCount: rows.length,
          rows: rows as unknown as readonly Row[],
        };
      },
      transaction: async (operation) => operation(database),
      ping: async () => true,
    };
    const config = {
      getSnapshot: () => ({ ...configDocumentFixture(), fetchedAt: "2026-08-26T00:00:00.000Z" }),
    } as RankedConfigService;
    const response = await new LeaderboardService(database, config).list();

    expect(capturedSql.toLowerCase()).toContain("hidden_mmr");
    expect(response.serverOpen).toBe(true);
    expect(response.players.map((player) => player.player)).toEqual([
      "gold-player",
      "promo-two-wins",
      "promo-one-win",
      "a-promo-tie-high-mmr",
      "z-promo-tie-high-mmr",
      "promo-tie-low-mmr",
      "aqua-normal",
      "a-unranked-high-mmr",
      "z-unranked-high-mmr",
      "unranked-low-mmr",
      "unranked-low-rate",
    ]);
    expect(response.players.at(-1)).toMatchObject({
      tier: "UNRANKED",
      visibleRankedScore: null,
      placementGamesRequired: 5,
      promotion: null,
      demotionWarning: false,
    });
    expect(JSON.stringify(response)).not.toMatch(/draw|hiddenMmr|hidden_mmr/i);
  });
});
