import { ConflictException, ForbiddenException, Inject, Injectable, Optional, ServiceUnavailableException, UnauthorizedException } from "@nestjs/common";
import { ForumRecordsService } from "../forum/forum-records.service.js";
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
import { RANKED_SESSION_HEARTBEAT_SECONDS, RANKED_SESSION_LEASE_SECONDS, sessionAliveSince } from "./session-presence.policy.js";

interface ExistingProfileRow {
  player_id: string;
  hidden_mmr: number | null;
  placement_games: number;
  initial_csmp_tier: CsmpTier | null;
  initial_seed_mmr: number | null;
  seed_applied_at: Date | string | null;
  displayed_tier: DisplayTier;
  visible_ranked_score: number | null;
  placement_results?: unknown;
  result_streak?: number;
  wins?: number;
  losses?: number;
  promotion_source_tier?: string | null;
  promotion_target_tier?: string | null;
  promotion_cutoff?: number | null;
  promotion_games_required?: number | null;
  promotion_wins_required?: number | null;
  promotion_results?: unknown;
  demotion_warning?: boolean;
}

interface PlayerRow {
  id: string;
}

export interface SessionCreationResult {
  readonly sessionToken: string;
  readonly expiresAt: string;
  readonly sessionLeaseSeconds: number;
  readonly sessionHeartbeatSeconds: number;
  readonly activeMatch: boolean;
  readonly player: {
    readonly gdAccountId: string;
    readonly gdUsername: string;
    readonly displayedTier: DisplayTier;
    readonly placementGames: number;
    readonly placementGamesRequired: number;
    readonly visibleRankedScore: number | null;
    readonly placementResults: readonly ("WIN" | "LOSS")[];
    readonly resultStreak: number;
    readonly wins: number;
    readonly losses: number;
    readonly promotion: {
      readonly sourceTier: string;
      readonly targetTier: string;
      readonly cutoff: number;
      readonly gamesRequired: number;
      readonly winsRequired: number;
      readonly results: readonly ("WIN" | "LOSS")[];
      readonly winsRemaining: number;
    } | null;
    readonly demotionWarning: boolean;
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
    @Optional() private readonly forumRecords?: ForumRecordsService,
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
    let now = this.clock.now();
    const sessionToken = this.tokens.createSessionToken();
    let expiresAt = new Date(now.getTime() + config.operational.timeouts.sessionSeconds * 1_000);
    let activeMatch = false;

    const profile = await this.database.transaction(async (transaction) => {
      const player = await this.upsertPlayer(transaction, dto);
      now = this.clock.now();
      expiresAt = new Date(now.getTime() + config.operational.timeouts!.sessionSeconds * 1_000);
      // The upsert locks the unique player row until commit. Concurrent
      // admissions for the same account serialize even when no session exists.
      const active = await transaction.query<{ id: string; instance_id: string | null }>(
        `SELECT id, environment_snapshot->>'clientInstanceId' AS instance_id
         FROM ranked_sessions WHERE player_id = $1 AND revoked_at IS NULL
           AND expires_at > $2 AND last_heartbeat_at > $3`,
        [player.id, now.toISOString(), sessionAliveSince(now)],
      );
      if (!dto.takeOver && active.rows.some((row) => !dto.clientInstanceId || row.instance_id !== dto.clientInstanceId)) {
        throw new ConflictException({
          code: "RANKED_ACCOUNT_ACTIVE",
          message: "This account is already connected to Ranked on another device. Leave Ranked there or wait for the connection to expire.",
          retryAfterSeconds: RANKED_SESSION_LEASE_SECONDS,
        });
      }
      // A retry from the same process rotates its token. Delayed heartbeats or
      // Close from an older token must never revive/revoke the replacement.
      // Matchmaking locks queue rows before renewing session rows. Use that
      // same order so admission/Close cannot deadlock a concurrent matchmaker.
      await transaction.query("SELECT player_id FROM ranked_queue_entries WHERE player_id = $1 FOR UPDATE", [player.id]);
      const ongoing = await transaction.query<{ id: string }>(
        `SELECT id FROM ranked_matches WHERE (player_a_id=$1 OR player_b_id=$1)
         AND state NOT IN ('MATCH_RESULT','CANCELLED') ORDER BY created_at DESC LIMIT 1`, [player.id]);
      const matchId = ongoing.rows[0]?.id;
      activeMatch = !!matchId;
      if (!matchId) await transaction.query("DELETE FROM ranked_queue_entries WHERE player_id = $1", [player.id]);
      await transaction.query("UPDATE ranked_sessions SET revoked_at = $2 WHERE player_id = $1 AND revoked_at IS NULL",
        [player.id, now.toISOString()]);
      const seededProfile = await this.ensureSeededProfile(
        transaction,
        player.id,
        csmpTier,
        config.operational.csmpSeeds,
        now,
      );
      const sessionId = this.ids.next();
      await transaction.query(
        `INSERT INTO ranked_sessions (
           id, player_id, token_hash, client_version, environment_snapshot,
           expires_at, last_heartbeat_at
         ) VALUES ($1, $2, $3, $4, $5::jsonb, $6, $7)`,
        [
          sessionId,
          player.id,
          this.tokens.hash(sessionToken),
          dto.clientVersion,
          JSON.stringify({ installedMods: dto.installedMods, clientInstanceId: dto.clientInstanceId ?? null }),
          expiresAt.toISOString(),
          now.toISOString(),
        ],
      );
      if (matchId) {
        // Move the existing assignment, not the match itself. Both explicit
        // takeover and a retry from the same process get a new scoped token;
        // the old device cannot keep playing with its previous credentials.
        await transaction.query(`INSERT INTO ranked_queue_entries
          (player_id,session_id,hidden_mmr_snapshot,joined_at,last_heartbeat_at,status,matched_match_id)
          VALUES ($1,$2,$3,$4,$4,'MATCHED',$5) ON CONFLICT (player_id) DO UPDATE SET
          session_id=EXCLUDED.session_id, status='MATCHED',matched_match_id=EXCLUDED.matched_match_id,
          last_heartbeat_at=EXCLUDED.last_heartbeat_at`,
          [player.id,sessionId,seededProfile.hidden_mmr,now.toISOString(),matchId]);
        const matchToken = this.tokens.deriveMatchToken(matchId,player.id,sessionId);
        await transaction.query(`INSERT INTO ranked_match_tokens (match_id,player_id,token_hash,expires_at)
          VALUES ($1,$2,$3,$4) ON CONFLICT (match_id,player_id) DO UPDATE SET
          token_hash=EXCLUDED.token_hash,expires_at=EXCLUDED.expires_at,revoked_at=NULL`,
          [matchId,player.id,this.tokens.hash(matchToken),expiresAt.toISOString()]);
      }
      return seededProfile;
    });

    // Queue the first fetch only after the transaction commits. Returning users
    // hit the durable cache; external APIs never hold the profile/session lock.
    this.forumRecords?.enqueue(dto.gdAccountId, dto.gdUsername);
    return {
      sessionToken,
      expiresAt: expiresAt.toISOString(),
      sessionLeaseSeconds: RANKED_SESSION_LEASE_SECONDS,
      sessionHeartbeatSeconds: RANKED_SESSION_HEARTBEAT_SECONDS,
      activeMatch,
      player: {
        gdAccountId: dto.gdAccountId,
        gdUsername: dto.gdUsername,
        displayedTier: profile.displayed_tier,
        placementGames: Number(profile.placement_games),
        placementGamesRequired: config.operational.mmrPolicy.placementGames,
        visibleRankedScore: Number(profile.placement_games) < config.operational.mmrPolicy.placementGames
          ? null
          : Number(profile.visible_ranked_score ?? 0),
        placementResults: this.ratingMarks(profile.placement_results),
        resultStreak: Number(profile.result_streak ?? 0),
        wins: Number(profile.wins ?? 0),
        losses: Number(profile.losses ?? 0),
        promotion: profile.promotion_source_tier && profile.promotion_target_tier &&
          profile.promotion_cutoff !== null && profile.promotion_cutoff !== undefined &&
          profile.promotion_games_required !== null && profile.promotion_games_required !== undefined &&
          profile.promotion_wins_required !== null && profile.promotion_wins_required !== undefined
          ? {
              sourceTier: profile.promotion_source_tier,
              targetTier: profile.promotion_target_tier,
              cutoff: Number(profile.promotion_cutoff),
              gamesRequired: Number(profile.promotion_games_required),
              winsRequired: Number(profile.promotion_wins_required),
              results: this.ratingMarks(profile.promotion_results),
              winsRemaining: Math.max(
                0,
                Number(profile.promotion_wins_required) -
                  this.ratingMarks(profile.promotion_results).filter((mark) => mark === "WIN").length,
              ),
            }
          : null,
        demotionWarning: Boolean(profile.demotion_warning),
      },
      authenticationLevel: "SELF_ASSERTED_GD_ACCOUNT_WITH_SERVER_SESSION",
      serverNow: now.toISOString(),
    };
  }

  private ratingMarks(value: unknown): readonly ("WIN" | "LOSS")[] {
    let parsed = value;
    if (typeof value === "string") {
      try { parsed = JSON.parse(value) as unknown; } catch { return []; }
    }
    if (!Array.isArray(parsed)) return [];
    return parsed.filter((mark): mark is "WIN" | "LOSS" => mark === "WIN" || mark === "LOSS");
  }

  public async heartbeat(session: RankedSessionContext) {
    return this.database.transaction(async (transaction) => {
      // Serialize against admission/Close, then sample the clock. A heartbeat
      // waiting on a row lock cannot use its earlier time to revive a dead lease.
      await transaction.query("SELECT id FROM ranked_players WHERE id = $1 FOR UPDATE", [session.playerId]);
      const now = this.clock.now();
      const result = await transaction.query(
        `UPDATE ranked_sessions SET last_heartbeat_at = $1 WHERE id = $2 AND player_id = $4 AND revoked_at IS NULL
         AND expires_at > $1 AND last_heartbeat_at > $3`,
        [now.toISOString(), session.sessionId, sessionAliveSince(now), session.playerId],
      );
      if (!result.rowCount) throw new UnauthorizedException("Ranked connection expired; enter Ranked again.");
      return { serverNow: now.toISOString(), sessionLeaseSeconds: RANKED_SESSION_LEASE_SECONDS };
    });
  }

  public async close(session: RankedSessionContext) {
    const now = this.clock.now();
    await this.database.transaction(async (transaction) => {
      await transaction.query("SELECT id FROM ranked_players WHERE id = $1 FOR UPDATE", [session.playerId]);
      await transaction.query("DELETE FROM ranked_queue_entries WHERE session_id = $1", [session.sessionId]);
      await transaction.query("UPDATE ranked_sessions SET revoked_at = $2 WHERE id = $1 AND revoked_at IS NULL",
        [session.sessionId, now.toISOString()]);
    });
    return { closed: true, serverNow: now.toISOString() };
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
    seeds: Readonly<Record<CsmpTier, number>>,
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
       ) VALUES ($1, 'UNRANKED', $2, 0, $3, $4, $5)
       ON CONFLICT (player_id) DO UPDATE SET
         hidden_mmr = EXCLUDED.hidden_mmr,
         visible_ranked_score = 0,
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
