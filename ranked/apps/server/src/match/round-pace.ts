import type { PlayerSide, RoundPace } from "@corum-ranked/rules";
import type { SqlExecutor } from "../database/database.port.js";

interface RoundPaceRow {
  round_number: number;
  result: PlayerSide | "DRAW" | null;
  result_reason: string | null;
  last_attempt_target: PlayerSide | null;
  round_seconds: number | string | null;
  first_clear_a: number | string | null;
  first_clear_b: number | string | null;
}

export interface MatchRoundPace {
  /** Each settled round's length and each side's first valid Clear (seconds from its start). */
  readonly rounds: readonly RoundPace[];
  readonly firstRoundWinner: PlayerSide | "DRAW" | null;
  /** Sides that saved a round with a last-attempt Clear (a round ending LAST_ATTEMPT_CLEAR). */
  readonly savedRoundSides: readonly PlayerSide[];
}

const seconds = (value: number | string | null): number | null => {
  if (value === null || value === undefined) return null;
  const parsed = Number(value);
  return Number.isFinite(parsed) && parsed >= 0 ? parsed : null;
};

/** Rating V2 inputs read from the settled rounds and their attempts (no writes). */
export async function readRoundPace(
  database: SqlExecutor,
  matchId: string,
  playerA: string,
  playerB: string,
  fallbackRoundSeconds: number,
): Promise<MatchRoundPace> {
  const { rows } = await database.query<RoundPaceRow>(
    `SELECT r.round_number, r.result, r.result_reason, r.last_attempt_target,
            EXTRACT(EPOCH FROM (r.normal_end_at - r.started_at)) AS round_seconds,
            EXTRACT(EPOCH FROM ((SELECT MIN(a.ended_at) FROM ranked_attempts a
              WHERE a.round_id = r.id AND a.player_id = $2 AND a.valid AND a.cleared) - r.started_at)) AS first_clear_a,
            EXTRACT(EPOCH FROM ((SELECT MIN(a.ended_at) FROM ranked_attempts a
              WHERE a.round_id = r.id AND a.player_id = $3 AND a.valid AND a.cleared) - r.started_at)) AS first_clear_b
     FROM ranked_rounds r
     WHERE r.match_id = $1 AND r.result IS NOT NULL
     ORDER BY r.round_number`,
    [matchId, playerA, playerB],
  );
  return {
    rounds: rows.map((round) => ({
      roundSeconds: seconds(round.round_seconds) || fallbackRoundSeconds,
      firstClearSecondsA: seconds(round.first_clear_a),
      firstClearSecondsB: seconds(round.first_clear_b),
    })),
    firstRoundWinner: rows[0]?.result ?? null,
    savedRoundSides: rows
      .filter((round) => round.result_reason === "LAST_ATTEMPT_CLEAR" && (round.last_attempt_target === "A" || round.last_attempt_target === "B"))
      .map((round) => round.last_attempt_target as PlayerSide),
  };
}
