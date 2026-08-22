import {
  CanActivate,
  ExecutionContext,
  Inject,
  Injectable,
  HttpException,
  HttpStatus,
  UnauthorizedException,
} from "@nestjs/common";
import type { DisplayTier } from "@corum-ranked/rules";
import { TokenService } from "../common/token.service.js";
import { DATABASE, type DatabasePort } from "../database/database.port.js";
import {
  REQUIRED_RANKED_CLIENT_VERSION,
  normalizeRankedClientVersion,
} from "./client-version.js";
import type { RankedRequest, RankedSessionContext } from "./session.types.js";

interface SessionRow {
  session_id: string;
  player_id: string;
  gd_account_id: string;
  gd_username: string;
  displayed_tier: DisplayTier;
  hidden_mmr: number;
  placement_games: number;
  client_version: string;
}

@Injectable()
export class SessionGuard implements CanActivate {
  public constructor(
    @Inject(DATABASE) private readonly database: DatabasePort,
    private readonly tokens: TokenService,
  ) {}

  public async canActivate(context: ExecutionContext): Promise<boolean> {
    const request = context.switchToHttp().getRequest<RankedRequest>();
    const authorization = request.headers.authorization || "";
    const match = authorization.match(/^Bearer\s+(.+)$/i);
    if (!match?.[1]) throw new UnauthorizedException("Missing Ranked session token");
    const tokenHash = this.tokens.hash(match[1]);
    const result = await this.database.query<SessionRow>(
      `SELECT
         s.id AS session_id,
         s.client_version,
         p.id AS player_id,
         p.gd_account_id::text AS gd_account_id,
         p.gd_username,
         rp.displayed_tier,
         rp.hidden_mmr,
         rp.placement_games
       FROM ranked_sessions s
       JOIN ranked_players p ON p.id = s.player_id
       JOIN ranked_profiles rp ON rp.player_id = p.id
       WHERE s.token_hash = $1
         AND s.revoked_at IS NULL
         AND s.expires_at > CURRENT_TIMESTAMP`,
      [tokenHash],
    );
    const row = result.rows[0];
    if (!row || row.hidden_mmr === null) throw new UnauthorizedException("Invalid or expired session");
    if (
      normalizeRankedClientVersion(row.client_version) !==
      normalizeRankedClientVersion(REQUIRED_RANKED_CLIENT_VERSION)
    ) {
      throw new HttpException({
        code: "RANKED_CLIENT_UPDATE_REQUIRED",
        message: `Corum Ranked ${REQUIRED_RANKED_CLIENT_VERSION} is required. Update the mod before entering Ranked.`,
        requiredVersion: REQUIRED_RANKED_CLIENT_VERSION,
        clientVersion: row.client_version,
      }, HttpStatus.UPGRADE_REQUIRED);
    }
    const session: RankedSessionContext = {
      sessionId: row.session_id,
      playerId: row.player_id,
      gdAccountId: row.gd_account_id,
      gdUsername: row.gd_username,
      displayedTier: row.displayed_tier,
      hiddenMmr: Number(row.hidden_mmr),
      placementGames: Number(row.placement_games),
    };
    request.rankedSession = session;
    return true;
  }
}
