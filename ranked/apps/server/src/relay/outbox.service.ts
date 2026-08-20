import { Inject, Injectable } from "@nestjs/common";
import { ID_GENERATOR, type IdGenerator } from "../common/runtime.module.js";
import type { SqlExecutor } from "../database/database.port.js";

export const RANKED_RELAY_EVENT_TYPES = [
  "ROUND_START",
  "CLEAR_EVENT",
  "LAST_ATTEMPT",
  "ROUND_RESULT",
  "MATCH_RESULT",
  "DEATHMATCH_START",
  "DEATHMATCH_RESULT",
] as const;

export type RankedRelayEventType = (typeof RANKED_RELAY_EVENT_TYPES)[number];

export interface MatchRelayIdentity {
  readonly id: string;
  readonly player_a_id: string;
  readonly player_b_id: string;
  readonly match_type?: "RANKED_PVP" | "DEBUG_BOT";
  readonly debug_discord_events?: boolean;
}

interface PlayerNameRow {
  readonly id: string;
  readonly gd_username: string;
}

@Injectable()
export class OutboxService {
  public constructor(@Inject(ID_GENERATOR) private readonly ids: IdGenerator) {}

  public async enqueueMatchEvent(
    transaction: SqlExecutor,
    match: MatchRelayIdentity,
    eventType: RankedRelayEventType,
    deduplicationKey: string,
    payload: Readonly<Record<string, unknown>>,
    occurredAt: Date,
  ): Promise<void> {
    const debugBotMatch = match.match_type === "DEBUG_BOT";
    if (debugBotMatch && !match.debug_discord_events) return;
    const players = await transaction.query<PlayerNameRow>(
      "SELECT id, gd_username FROM ranked_players WHERE id = $1 OR id = $2",
      [match.player_a_id, match.player_b_id],
    );
    const playerA = players.rows.find((player) => player.id === match.player_a_id);
    const playerB = players.rows.find((player) => player.id === match.player_b_id);
    await transaction.query(
      `INSERT INTO ranked_outbox_events (
         id, aggregate_type, aggregate_id, event_type,
         deduplication_key, payload, available_at
       ) VALUES ($1, 'RANKED_MATCH', $2, $3, $4, $5::jsonb, $6)
       ON CONFLICT (deduplication_key) DO NOTHING`,
      [
        this.ids.next(),
        match.id,
        eventType,
        deduplicationKey,
        JSON.stringify({
          matchId: match.id,
          debugBotMatch,
          occurredAt: occurredAt.toISOString(),
          players: {
            A: playerA?.gd_username ?? "Player A",
            B: playerB?.gd_username ?? "Player B",
          },
          ...payload,
        }),
        occurredAt.toISOString(),
      ],
    );
  }
}
