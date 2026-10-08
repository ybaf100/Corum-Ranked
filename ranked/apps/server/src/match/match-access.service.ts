import { Inject, Injectable, UnauthorizedException } from "@nestjs/common";
import { SERVER_CLOCK, type ServerClock } from "../common/runtime.module.js";
import { TokenService } from "../common/token.service.js";
import type { SqlExecutor } from "../database/database.port.js";
import type { RankedSessionContext } from "../session/session.types.js";

export interface MatchAccess {
  readonly side: "A" | "B";
}

interface AccessRow {
  player_a_id: string;
  player_b_id: string;
  token_hash: string;
}

@Injectable()
export class MatchAccessService {
  public constructor(
    private readonly tokens: TokenService,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
  ) {}

  public async authorize(
    executor: SqlExecutor,
    matchId: string,
    session: RankedSessionContext,
    matchToken: string,
  ): Promise<MatchAccess> {
    if (!matchToken) throw new UnauthorizedException("Missing match-scoped token");
    const result = await executor.query<AccessRow>(
      `SELECT m.player_a_id, m.player_b_id, mt.token_hash
       FROM ranked_matches m
       JOIN ranked_match_tokens mt ON mt.match_id = m.id AND mt.player_id = $2
       WHERE m.id = $1
         AND mt.revoked_at IS NULL
         AND mt.expires_at > $3`,
      [matchId, session.playerId, this.clock.now().toISOString()],
    );
    const row = result.rows[0];
    if (!row || !this.tokens.matches(matchToken, row.token_hash)) {
      throw new UnauthorizedException("Invalid or expired match-scoped token");
    }
    return { side: row.player_a_id === session.playerId ? "A" : "B" };
  }
}
