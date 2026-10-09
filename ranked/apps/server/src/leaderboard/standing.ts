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
  readonly goldTimeline?: GoldScoreTimeline;
}

export interface GoldScoreTimeline {
  readonly fixedTotal: number;
  readonly higherFixed: number;
  readonly groups: readonly { score: number; count: number; target: StandingTarget; preferMoving: boolean }[];
  readonly moving: { beforeScore: number | null; afterScore: number | null; target: StandingTarget } | null;
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
  gold_timeline: GoldScoreTimeline | null;
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
  includeGoldTimeline = false,
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
     ), fixed AS (
       SELECT * FROM ranked WHERE phase='before' AND NOT (id=ANY($1::uuid[]))
     ), fixed_groups AS (
       SELECT DISTINCT ON (score) *, COUNT(*) OVER (PARTITION BY score)::int AS amount
       FROM fixed ORDER BY score, mmr DESC, md5(id::text || $4::text)
     )
     SELECT phase.phase, requested.player_id, own.rank, COALESCE(totals.total, 0) AS total,
       target.gd_account_id::text AS target_account_id,
       target.gd_username AS target_name, target.is_bot AS target_is_bot,
       target.rank AS target_rank, target.score AS target_score,
       CASE WHEN $5::boolean AND phase.phase='before' AND (old_own.tier='GOLD' OR new_own.tier='GOLD')
       THEN jsonb_build_object(
         'fixedTotal',(SELECT COUNT(*)::int FROM fixed),
         'higherFixed',(SELECT COUNT(*)::int FROM fixed WHERE score>COALESCE(above_range.score,GREATEST(old_own.score,new_own.score,moving_before.score,moving_after.score))),
         'groups',COALESCE((SELECT jsonb_agg(jsonb_build_object('score',g.score,'count',g.amount,
           'preferMoving', COALESCE(moving_after.mmr>g.mmr OR (moving_after.mmr=g.mmr AND md5(moving_after.id::text || $4::text)<md5(g.id::text || $4::text)),FALSE),
           'target',jsonb_build_object('gdAccountId',g.gd_account_id::text,'player',g.gd_username,'isBot',g.is_bot,'rank',g.rank,'visibleRankedScore',g.score)) ORDER BY g.score DESC)
           FROM fixed_groups g WHERE g.score BETWEEN LEAST(old_own.score,new_own.score,moving_before.score,moving_after.score) AND GREATEST(old_own.score,new_own.score,moving_before.score,moving_after.score)
             OR g.score=above_range.score),'[]'::jsonb),
         'moving',CASE WHEN moving_before.id IS NOT NULL OR moving_after.id IS NOT NULL THEN jsonb_build_object(
           'beforeScore',moving_before.score,'afterScore',moving_after.score,
           'target',jsonb_build_object('gdAccountId',COALESCE(moving_after.gd_account_id,moving_before.gd_account_id)::text,
             'player',COALESCE(moving_after.gd_username,moving_before.gd_username),'isBot',COALESCE(moving_after.is_bot,moving_before.is_bot),
             'rank',COALESCE(moving_before.rank,moving_after.rank),'visibleRankedScore',COALESCE(moving_before.score,moving_after.score))) ELSE NULL END)
       ELSE NULL END AS gold_timeline
     FROM phases phase CROSS JOIN unnest($1::uuid[]) AS requested(player_id)
     LEFT JOIN ranked own ON own.phase = phase.phase AND own.id = requested.player_id
     LEFT JOIN totals ON totals.phase = phase.phase
     LEFT JOIN profiles old_own ON old_own.phase='before' AND old_own.id=requested.player_id
     LEFT JOIN profiles new_own ON new_own.phase='after' AND new_own.id=requested.player_id
     LEFT JOIN ranked moving_before ON $5::boolean AND moving_before.phase='before' AND moving_before.id<>requested.player_id AND moving_before.id=ANY($1::uuid[])
     LEFT JOIN ranked moving_after ON $5::boolean AND moving_after.phase='after' AND moving_after.id<>requested.player_id AND moving_after.id=ANY($1::uuid[])
     LEFT JOIN LATERAL (SELECT * FROM fixed_groups WHERE score>GREATEST(old_own.score,new_own.score,moving_before.score,moving_after.score) ORDER BY score LIMIT 1) above_range ON TRUE
     LEFT JOIN LATERAL (
       SELECT candidate.* FROM ranked candidate
       WHERE candidate.phase = phase.phase AND candidate.score > own.score
       ORDER BY candidate.score ASC, candidate.mmr DESC, md5(candidate.id::text || $4::text)
       LIMIT 1
     ) target ON TRUE`,
    [playerIds, phases, JSON.stringify(overrides.map(({ phase, playerId, profile }) => ({
      phase, player_id: playerId, displayed_tier: profile.displayedTier as DisplayTier,
      visible_ranked_score: profile.visibleRankedScore, hidden_mmr: profile.hiddenMmr,
    }))), tieSeed, includeGoldTimeline],
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
      ...(row.gold_timeline ? {goldTimeline:row.gold_timeline} : {}),
    });
  }
  return views;
}
