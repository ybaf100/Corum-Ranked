import { ForbiddenException, Inject, Injectable, ServiceUnavailableException } from "@nestjs/common";
import {
  evaluateClientEnvironment,
  seedProfileOnce,
  type CsmpTier,
  type DisplayTier,
  type RankedProfileSeedState,
} from "@corum-ranked/rules";
import { ID_GENERATOR, SERVER_CLOCK, type IdGenerator, type ServerClock } from "../common/runtime.module.js";
import { TokenService } from "../common/token.service.js";
import {
  CSMP_TIER_SOURCE,
  type CsmpTierSource,
} from "../config/csmp-tier.source.js";
import { RankedConfigService } from "../config/ranked-config.service.js";
import { DATABASE, type DatabasePort, type SqlExecutor } from "../database/database.port.js";
import type { CreateSessionDto, InstalledModDto } from "./session.dto.js";
import type { RankedSessionContext } from "./session.types.js";

interface ExistingProfileRow {
  player_id: string;
  hidden_mmr: number | null;
  placement_games: number;
  initial_csmp_tier: CsmpTier | null;
  initial_seed_mmr: number | null;
  seed_applied_at: Date | string | null;
  displayed_tier: DisplayTier;
  visible_ranked_score: number | null;
}

interface PlayerRow {
  id: string;
}

export interface SessionCreationResult {
  readonly sessionToken: string;
  readonly expiresAt: string;
  readonly player: {
    readonly gdAccountId: string;
    readonly gdUsername: string;
    readonly displayedTier: DisplayTier;
    readonly placementGames: number;
    readonly placementGamesRequired: number;
    readonly visibleRankedScore: number;
  };
  readonly authenticationLevel: "SELF_ASSERTED_GD_ACCOUNT_WITH_SERVER_SESSION";
  readonly serverNow: string;
}

@Injectable()
export class SessionService {
  public constructor(
    @Inject(DATABASE) private readonly database: DatabasePort,
    @Inject(CSMP_TIER_SOURCE) private readonly csmpTierSource: CsmpTierSource,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
    @Inject(ID_GENERATOR) private readonly ids: IdGenerator,
    private readonly config: RankedConfigService,
    private readonly tokens: TokenService,
  ) {}

  public assertEnvironment(installedMods: readonly InstalledModDto[]): void {
    const snapshot = this.config.getSnapshot();
    const decision = evaluateClientEnvironment(installedMods, {
      allowedMods: snapshot.allowedMods,
      cbf: snapshot.operational.cbf,
    });
    if (!decision.allowed) {
      throw new ForbiddenException({
        code: "RANKED_ENVIRONMENT_BLOCKED",
        unauthorizedModIds: decision.unauthorizedModIds,
        allowedModIds: decision.allowedModIds,
        missingRequiredModIds: decision.missingRequiredModIds,
        versionViolations: decision.versionViolations,
        cbfIssues: decision.cbfIssues,
      });
    }
  }

  public async create(dto: CreateSessionDto): Promise<SessionCreationResult> {
    const config = this.config.getSnapshot();
    if (!config.operational.enabled || !config.operational.mmrPolicy || !config.operational.timeouts) {
      throw new ServiceUnavailableException("Ranked queue is not operationally configured");
    }
    this.assertEnvironment(dto.installedMods);

    const existing = await this.findExistingProfile(dto.gdAccountId);
    const needsSeed = !existing?.seed_applied_at;
    const csmpTier = needsSeed
      ? await this.csmpTierSource.fetchCurrentTier(dto.gdAccountId)
      : null;
    const now = this.clock.now();
    const sessionToken = this.tokens.createSessionToken();
    const expiresAt = new Date(now.getTime() + config.operational.timeouts.sessionSeconds * 1_000);

    const profile = await this.database.transaction(async (transaction) => {
      const player = await this.upsertPlayer(transaction, dto);
      const seededProfile = await this.ensureSeededProfile(
        transaction,
        player.id,
        csmpTier,
        config.operational.csmpSeeds,
        now,
      );
      await transaction.query(
        `INSERT INTO ranked_sessions (
           id, player_id, token_hash, client_version, environment_snapshot,
           expires_at, last_heartbeat_at
         ) VALUES ($1, $2, $3, $4, $5::jsonb, $6, $7)`,
        [
          this.ids.next(),
          player.id,
          this.tokens.hash(sessionToken),
          dto.clientVersion,
          JSON.stringify(dto.installedMods),
          expiresAt.toISOString(),
          now.toISOString(),
        ],
      );
      return seededProfile;
    });

    return {
      sessionToken,
      expiresAt: expiresAt.toISOString(),
      player: {
        gdAccountId: dto.gdAccountId,
        gdUsername: dto.gdUsername,
        displayedTier: profile.displayed_tier,
        placementGames: Number(profile.placement_games),
        placementGamesRequired: config.operational.mmrPolicy.placementGames,
        visibleRankedScore: Number(profile.visible_ranked_score ?? profile.hidden_mmr ?? 0),
      },
      authenticationLevel: "SELF_ASSERTED_GD_ACCOUNT_WITH_SERVER_SESSION",
      serverNow: now.toISOString(),
    };
  }

  public async heartbeat(session: RankedSessionContext): Promise<{ serverNow: string }> {
    const now = this.clock.now();
    await this.database.query(
      "UPDATE ranked_sessions SET last_heartbeat_at = $1 WHERE id = $2 AND revoked_at IS NULL",
      [now.toISOString(), session.sessionId],
    );
    return { serverNow: now.toISOString() };
  }

  private async findExistingProfile(gdAccountId: string): Promise<ExistingProfileRow | null> {
    const result = await this.database.query<ExistingProfileRow>(
      `SELECT rp.*, p.id AS player_id
       FROM ranked_players p
       JOIN ranked_profiles rp ON rp.player_id = p.id
       WHERE p.gd_account_id = $1`,
      [gdAccountId],
    );
    return result.rows[0] ?? null;
  }

  private async upsertPlayer(transaction: SqlExecutor, dto: CreateSessionDto): Promise<PlayerRow> {
    const result = await transaction.query<PlayerRow>(
      `INSERT INTO ranked_players (id, gd_account_id, gd_username)
       VALUES ($1, $2, $3)
       ON CONFLICT (gd_account_id) DO UPDATE
         SET gd_username = EXCLUDED.gd_username, updated_at = CURRENT_TIMESTAMP
       RETURNING id`,
      [this.ids.next(), dto.gdAccountId, dto.gdUsername],
    );
    const player = result.rows[0];
    if (!player) throw new Error("Failed to upsert Ranked player");
    return player;
  }

  private async ensureSeededProfile(
    transaction: SqlExecutor,
    playerId: string,
    fetchedCsmpTier: CsmpTier | null,
    seeds: Readonly<Partial<Record<CsmpTier, number>>>,
    now: Date,
  ): Promise<ExistingProfileRow> {
    const locked = await transaction.query<ExistingProfileRow>(
      "SELECT *, player_id FROM ranked_profiles WHERE player_id = $1 FOR UPDATE",
      [playerId],
    );
    const existing = locked.rows[0];
    if (existing?.seed_applied_at) return existing;
    if (!fetchedCsmpTier) {
      throw new ServiceUnavailableException("CSMP seed source was unavailable during first profile creation");
    }

    const base: RankedProfileSeedState = {
      hiddenMmr: existing?.hidden_mmr ?? null,
      placementGamesPlayed: Number(existing?.placement_games ?? 0),
      initialCsmpTier: existing?.initial_csmp_tier ?? null,
      initialSeedMmr: existing?.initial_seed_mmr ?? null,
      seedAppliedAt: existing?.seed_applied_at ? String(existing.seed_applied_at) : null,
    };
    const seeded = seedProfileOnce(base, fetchedCsmpTier, seeds, now.toISOString()).profile;
    const result = await transaction.query<ExistingProfileRow>(
      `INSERT INTO ranked_profiles (
         player_id, displayed_tier, hidden_mmr, visible_ranked_score, initial_csmp_tier,
         initial_seed_mmr, seed_applied_at
       ) VALUES ($1, 'UNRANKED', $2, $2, $3, $4, $5)
       ON CONFLICT (player_id) DO UPDATE SET
         hidden_mmr = EXCLUDED.hidden_mmr,
         visible_ranked_score = EXCLUDED.visible_ranked_score,
         initial_csmp_tier = EXCLUDED.initial_csmp_tier,
         initial_seed_mmr = EXCLUDED.initial_seed_mmr,
         seed_applied_at = EXCLUDED.seed_applied_at,
         updated_at = CURRENT_TIMESTAMP
       WHERE ranked_profiles.seed_applied_at IS NULL
       RETURNING *, player_id`,
      [
        playerId,
        seeded.hiddenMmr,
        seeded.initialCsmpTier,
        seeded.initialSeedMmr,
        seeded.seedAppliedAt,
      ],
    );
    const profile = result.rows[0];
    if (profile) return profile;
    const raced = await transaction.query<ExistingProfileRow>(
      "SELECT *, player_id FROM ranked_profiles WHERE player_id = $1",
      [playerId],
    );
    if (!raced.rows[0]) throw new Error("Failed to persist Ranked profile seed");
    return raced.rows[0];
  }
}
