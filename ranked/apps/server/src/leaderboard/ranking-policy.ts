/** Rank keys are shared by the list sorter and the SQL used by every standing.
 * Win rate, MMR and name only order members of an existing shared rank. */
const rankKeys = [
  ["score", -1], ["promotion_active", -1], ["promotion_wins", -1], ["demotion_warning", 1],
] as const;

export interface LadderPosition {
  readonly score: number;
  readonly promotion_active: boolean;
  readonly promotion_wins: number;
  readonly demotion_warning: boolean;
}

export const ladderRankOrderSql = (alias = ""): string => rankKeys
  .map(([key, direction]) => `${alias ? alias + "." : ""}${key} ${direction < 0 ? "DESC" : "ASC"}`).join(", ");

/** Compact public tie state for immutable GOLD animations. It contains no MMR. */
export const ladderTiePrioritySql = (alias: string): string => `(CASE WHEN ${alias}.promotion_active
  THEN 2 + ${alias}.promotion_wins WHEN ${alias}.demotion_warning THEN 0 ELSE 1 END)`;

export const compareLadderPosition = (left: LadderPosition, right: LadderPosition): number => {
  for (const [key, direction] of rankKeys) {
    const difference = (Number(left[key]) - Number(right[key])) * direction;
    if (difference) return difference;
  }
  return 0;
};

export const activePromotionSql = (alias: string): string => `(${alias}.promotion_source_tier IS NOT NULL
  AND ${alias}.promotion_target_tier IS NOT NULL AND ${alias}.promotion_games_required IS NOT NULL
  AND ${alias}.promotion_wins_required IS NOT NULL)`;

export const promotionWinsSql = (alias: string): string => `(SELECT COUNT(*)::int FROM
  jsonb_array_elements_text(CASE WHEN jsonb_typeof(${alias}.promotion_results)='array'
    THEN ${alias}.promotion_results ELSE '[]'::jsonb END) AS mark(value) WHERE value='WIN')`;

/** Exact GOLD slots use the same ordering as the public list, including its
 * existing tie-breakers. No random seed is allowed to decide a title. */
export const goldTitleOrderSql = (alias: string): string => `${ladderRankOrderSql(alias)},
  CASE WHEN ${alias}.wins+${alias}.losses>0 THEN ${alias}.wins::double precision/(${alias}.wins+${alias}.losses) ELSE 0 END DESC,
  ${alias}.mmr DESC, lower(${alias}.gd_username) COLLATE "C" ASC, ${alias}.gd_username COLLATE "C" ASC,
  ${alias}.gd_account_id::text COLLATE "C" ASC, ${alias}.id ASC`;
