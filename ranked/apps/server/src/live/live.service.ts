import { Inject, Injectable } from "@nestjs/common";
import { DATABASE, type DatabasePort } from "../database/database.port.js";

/** Matches older than this, or silent for longer than the heartbeat window, are not shown. */
const SOLO_MAX_AGE = "3 hours";
const SOLO_SILENT = "2 minutes";
const SURVIVAL_MAX_AGE = "3 hours";
const SURVIVAL_SILENT = "15 minutes";
const QUEUE_FRESH = "60 seconds";
const LIMIT = 20;

interface SoloRow {
  state: string; match_type: string; effective_tier: string; format: string | null;
  current_round_number: number | null; round_wins_a: number; round_wins_b: number;
  deadline_at: Date | string | null; started_at: Date | string | null; created_at: Date | string;
  a_name: string; a_bot: boolean; a_tier: string | null; b_name: string; b_bot: boolean; b_tier: string | null;
  round_title: string | null; round_difficulty: string | null; round_pool: number | null;
  score_a: string | number | null; score_b: string | number | null; clears_a: number | null; clears_b: number | null;
  dm_sequence: number | null; dm_title: string | null;
}
interface SurvivalRow {
  state: string; created_at: Date | string; online: boolean; debug: boolean;
  entrants: number; alive: number; humans: number; alive_humans: string[] | null;
  phase: string | null; target_qualifiers: number | null; map_name: string | null; map_creator: string | null;
  starts_at: Date | string | null; ends_at: Date | string | null;
}

const iso = (value: Date | string | null): string | null => value === null ? null : new Date(value).toISOString();
const num = (value: string | number | null): number | null => value === null ? null : Number(value);

/**
 * Public, read-only view of matches in progress for the website. It carries only
 * what both players already see on screen: names, displayed tiers, round and map,
 * round wins and the current round's authoritative score and clears. Match IDs,
 * session data, Hidden MMR and unsettled LP or ranks are never included.
 */
@Injectable()
export class LiveService {
  public constructor(@Inject(DATABASE) private readonly database: DatabasePort) {}

  public async snapshot() {
    const [solo, survival, queues] = await Promise.all([this.solo(), this.survival(), this.queues()]);
    return {
      serverNow: new Date().toISOString(),
      solo: { queued: queues.solo, matches: solo },
      survival: { queued: queues.survival, matches: survival },
    };
  }

  private async solo() {
    const result = await this.database.query<SoloRow>(
      `SELECT m.state, m.match_type, m.effective_tier, m.series_state->>'format' AS format,
              m.current_round_number, m.round_wins_a, m.round_wins_b, m.deadline_at, m.started_at, m.created_at,
              pa.gd_username AS a_name, pa.is_bot AS a_bot, ra.displayed_tier AS a_tier,
              pb.gd_username AS b_name, pb.is_bot AS b_bot, rb.displayed_tier AS b_tier,
              r.title AS round_title, r.difficulty AS round_difficulty, r.pool AS round_pool,
              r.score_a, r.score_b, r.clears_a, r.clears_b,
              dm.sequence AS dm_sequence, dm.map_snapshot->>'title' AS dm_title
         FROM ranked_matches m
         JOIN ranked_players pa ON pa.id = m.player_a_id
         JOIN ranked_players pb ON pb.id = m.player_b_id
         LEFT JOIN ranked_profiles ra ON ra.player_id = pa.id
         LEFT JOIN ranked_profiles rb ON rb.player_id = pb.id
         LEFT JOIN LATERAL (
           SELECT title, difficulty, pool, score_a, score_b, clears_a, clears_b
             FROM ranked_rounds WHERE match_id = m.id AND round_number = m.current_round_number
             LIMIT 1
         ) r ON TRUE
         LEFT JOIN LATERAL (
           SELECT sequence, map_snapshot FROM ranked_deathmatches
            WHERE match_id = m.id ORDER BY sequence DESC LIMIT 1
         ) dm ON m.state LIKE 'DEATHMATCH%'
        WHERE m.finished_at IS NULL
          AND m.state NOT IN ('MATCH_RESULT', 'CANCELLED')
          AND m.created_at > CURRENT_TIMESTAMP - INTERVAL '${SOLO_MAX_AGE}'
          AND (m.created_at > CURRENT_TIMESTAMP - INTERVAL '${SOLO_SILENT}'
               OR GREATEST(m.last_heartbeat_a_at, m.last_heartbeat_b_at) > CURRENT_TIMESTAMP - INTERVAL '${SOLO_SILENT}')
        ORDER BY m.created_at DESC
        LIMIT ${LIMIT}`,
    );
    return result.rows.map((row) => {
      const deathmatch = row.state.startsWith("DEATHMATCH");
      return {
        state: row.state,
        botMatch: row.match_type === "DEBUG_BOT",
        effectiveTier: row.effective_tier,
        format: row.format === "SINGLE" ? "SINGLE" : "BO3",
        round: deathmatch ? null : row.current_round_number,
        deathmatch: deathmatch ? { sequence: row.dm_sequence, map: row.dm_title } : null,
        roundWins: [Number(row.round_wins_a), Number(row.round_wins_b)],
        players: [
          { name: row.a_name, isBot: Boolean(row.a_bot), tier: row.a_tier },
          { name: row.b_name, isBot: Boolean(row.b_bot), tier: row.b_tier },
        ],
        map: !deathmatch && row.round_title ? { title: row.round_title, difficulty: row.round_difficulty, pool: row.round_pool } : null,
        roundScore: !deathmatch && row.round_title ? { scores: [num(row.score_a), num(row.score_b)], clears: [row.clears_a ?? 0, row.clears_b ?? 0] } : null,
        deadlineAt: iso(row.deadline_at),
        startedAt: iso(row.started_at ?? row.created_at),
      };
    });
  }

  private async survival() {
    const result = await this.database.query<SurvivalRow>(
      `SELECT s.state, s.created_at,
              (o.match_id IS NOT NULL) AS online, (d.match_id IS NOT NULL) AS debug,
              (SELECT COUNT(*)::int FROM sur_match_players p WHERE p.match_id = s.id) AS entrants,
              (SELECT COUNT(*)::int FROM sur_match_players p WHERE p.match_id = s.id AND p.status = 'ACTIVE') AS alive,
              (SELECT COUNT(*)::int FROM sur_match_players p WHERE p.match_id = s.id AND p.player_kind = 'HUMAN') AS humans,
              (SELECT array_agg(p.player_name ORDER BY p.player_name) FROM sur_match_players p
                WHERE p.match_id = s.id AND p.player_kind = 'HUMAN' AND p.status = 'ACTIVE') AS alive_humans,
              r.phase, r.target_qualifiers, r.map_snapshot->>'name' AS map_name, r.map_snapshot->>'creator' AS map_creator,
              r.starts_at, r.ends_at
         FROM sur_matches s
         LEFT JOIN sur_online_matches o ON o.match_id = s.id
         LEFT JOIN sur_debug_matches d ON d.match_id = s.id
         LEFT JOIN sur_rounds r ON r.match_id = s.id AND r.round_key = s.current_round_key
        WHERE s.finished_at IS NULL
          AND s.state NOT IN ('FINAL_RESULT', 'CANCELLED')
          AND s.created_at > CURRENT_TIMESTAMP - INTERVAL '${SURVIVAL_MAX_AGE}'
          AND s.updated_at > CURRENT_TIMESTAMP - INTERVAL '${SURVIVAL_SILENT}'
        ORDER BY s.created_at DESC
        LIMIT ${LIMIT}`,
    );
    return result.rows.map((row) => ({
      state: row.state,
      kind: row.online ? "ONLINE" : row.debug ? "DEBUG" : "OTHER",
      phase: row.phase,
      targetQualifiers: row.target_qualifiers,
      entrants: Number(row.entrants),
      alive: Number(row.alive),
      humans: Number(row.humans),
      aliveHumans: row.alive_humans ?? [],
      map: row.map_name ? { title: row.map_name, creator: row.map_creator } : null,
      startsAt: iso(row.starts_at),
      endsAt: iso(row.ends_at),
      startedAt: iso(row.created_at),
    }));
  }

  private async queues() {
    const result = await this.database.query<{ solo: number; survival: number }>(
      `SELECT
         (SELECT COUNT(*)::int FROM ranked_queue_entries
           WHERE status = 'QUEUED' AND last_heartbeat_at > CURRENT_TIMESTAMP - INTERVAL '${QUEUE_FRESH}') AS solo,
         (SELECT COUNT(*)::int FROM sur_queue_entries
           WHERE status = 'QUEUED' AND last_heartbeat_at > CURRENT_TIMESTAMP - INTERVAL '${QUEUE_FRESH}') AS survival`,
    );
    const row = result.rows[0];
    return { solo: Number(row?.solo ?? 0), survival: Number(row?.survival ?? 0) };
  }
}
