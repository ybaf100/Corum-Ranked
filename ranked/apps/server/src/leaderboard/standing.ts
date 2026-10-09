import type { DisplayTier, RankedRatingProfile } from "@corum-ranked/rules";
import type { SqlExecutor } from "../database/database.port.js";
import { randomUUID } from "node:crypto";

export interface StandingTarget {
  readonly gdAccountId: string;
  readonly player: string;
  readonly isBot: boolean;
  readonly rank: number;
  readonly visibleRankedScore: number;
}

export interface PlayerStanding {
  readonly rank: number | null;
  readonly total: number;
  readonly topPercent: number | null;
  readonly target: StandingTarget | null;
}

export const unrankedStanding = (): PlayerStanding => ({ rank: null, total: 0, topPercent: null, target: null });

export interface StandingOverride {
  readonly phase: string;
  readonly playerId: string;
  readonly profile: Pick<RankedRatingProfile, "displayedTier" | "visibleRankedScore" | "hiddenMmr">;
}

interface StandingRow {
  phase: string;
  player_id: string;
  rank: number | null;
  total: number;
  target_account_id: string | null;
  target_name: string | null;
  target_is_bot: boolean | null;
  target_rank: number | null;
  target_score: number | null;
}

/** One SQL snapshot for all subjects/phases. Include every historical bot;
 * MMR selects an icon within the nearest higher LP tie, never a rank number.
 * No ladder, MMR or internal player IDs are returned to the client. */
export async function readStandings(
  database: SqlExecutor,
  playerIds: readonly string[],
  phases: readonly string[] = ["current"],
  overrides: readonly StandingOverride[] = [],
  tieSeed: string = randomUUID(),
): Promise<ReadonlyMap<string, ReadonlyMap<string, PlayerStanding>>> {
  const result = await database.query<StandingRow>(
    `WITH phases AS (SELECT unnest($2::text[]) AS phase),
     overrides AS (
       SELECT * FROM jsonb_to_recordset($3::jsonb) AS o(
         phase text, player_id uuid, displayed_tier text,
         visible_ranked_score integer, hidden_mmr integer)
     ), profiles AS (
       SELECT phase.phase, p.id, p.gd_account_id, p.gd_username, p.is_bot,
         CASE WHEN o.player_id IS NOT NULL THEN o.displayed_tier ELSE rp.displayed_tier END AS tier,
         CASE WHEN o.player_id IS NOT NULL THEN o.visible_ranked_score ELSE rp.visible_ranked_score END AS score,
         CASE WHEN o.player_id IS NOT NULL THEN o.hidden_mmr ELSE rp.hidden_mmr END AS mmr
       FROM ranked_profiles rp JOIN ranked_players p ON p.id = rp.player_id
       CROSS JOIN phases phase
       LEFT JOIN overrides o ON o.phase = phase.phase AND o.player_id = p.id
     ), eligible AS (
       SELECT * FROM profiles WHERE tier <> 'UNRANKED' AND score IS NOT NULL
     ), ranked AS (
       SELECT *, RANK() OVER (PARTITION BY phase ORDER BY score DESC)::int AS rank
       FROM eligible
     ), totals AS (
       SELECT phase, COUNT(*)::int AS total FROM eligible GROUP BY phase
     )
     SELECT phase.phase, requested.player_id, own.rank, COALESCE(totals.total, 0) AS total,
       target.gd_account_id::text AS target_account_id,
       target.gd_username AS target_name, target.is_bot AS target_is_bot,
       target.rank AS target_rank, target.score AS target_score
     FROM phases phase CROSS JOIN unnest($1::uuid[]) AS requested(player_id)
     LEFT JOIN ranked own ON own.phase = phase.phase AND own.id = requested.player_id
     LEFT JOIN totals ON totals.phase = phase.phase
     LEFT JOIN LATERAL (
       SELECT candidate.* FROM ranked candidate
       WHERE candidate.phase = phase.phase AND candidate.score > own.score
       ORDER BY candidate.score ASC, candidate.mmr DESC, md5(candidate.id::text || $4::text)
       LIMIT 1
     ) target ON TRUE`,
    [playerIds, phases, JSON.stringify(overrides.map(({ phase, playerId, profile }) => ({
      phase, player_id: playerId, displayed_tier: profile.displayedTier as DisplayTier,
      visible_ranked_score: profile.visibleRankedScore, hidden_mmr: profile.hiddenMmr,
    }))), tieSeed],
  );
  const views = new Map<string, Map<string, PlayerStanding>>(phases.map((phase) => [phase, new Map()]));
  for (const row of result.rows) {
    const rank = row.rank === null ? null : Number(row.rank);
    const total = Number(row.total);
    views.get(row.phase)?.set(row.player_id, {
      rank, total, topPercent: rank !== null && total > 0 ? rank / total * 100 : null,
      target: row.target_account_id !== null && row.target_rank !== null && row.target_score !== null
        ? { gdAccountId: row.target_account_id, player: row.target_name ?? "",
            isBot: Boolean(row.target_is_bot), rank: Number(row.target_rank),
            visibleRankedScore: Number(row.target_score) }
        : null,
    });
  }
  return views;
}
