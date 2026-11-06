import type { SqlExecutor } from "../database/database.port.js";

export interface RankedSeasonView {
  readonly preseason: boolean;
  readonly number: number;
  /** Season end while the mode is enabled; mode start while it is disabled. */
  readonly targetAt: string | null;
}
export type RankedSeasons = Readonly<Record<"SOLO_RANKED" | "SURVIVAL", RankedSeasonView>>;

const fallback: RankedSeasonView = { preseason: true, number: 0, targetAt: null };

/** Season settings per mode (Admin). A missing table/row reads as preseason. */
export async function readRankedSeasons(database?: SqlExecutor): Promise<RankedSeasons> {
  const seasons: Record<"SOLO_RANKED" | "SURVIVAL", RankedSeasonView> = { SOLO_RANKED: fallback, SURVIVAL: fallback };
  if (!database) return seasons;
  try {
    const rows = await database.query<{ mode: "SOLO_RANKED" | "SURVIVAL"; preseason: boolean; season_number: number; target_at: Date | string | null }>(
      "SELECT mode, preseason, season_number, target_at FROM ranked_seasons");
    for (const row of rows.rows) {
      seasons[row.mode] = { preseason: !!row.preseason, number: Number(row.season_number),
        targetAt: row.target_at === null ? null : new Date(row.target_at).toISOString() };
    }
  } catch { /* schema not migrated yet: keep the preseason fallback */ }
  return seasons;
}
