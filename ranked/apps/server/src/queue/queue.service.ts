import {
  ConflictException,
  Inject,
  Injectable,
  NotFoundException,
  ServiceUnavailableException,
} from "@nestjs/common";
import {
  createMatchSeries,
  displayedTierForProfile,
  effectiveTierForMatch,
  selectCandidateMaps,
  type MatchmakingPolicy,
  type RandomSource,
} from "@corum-ranked/rules";
import {
  ID_GENERATOR,
  RANDOM_SOURCE,
  SERVER_CLOCK,
  type IdGenerator,
  type ServerClock,
} from "../common/runtime.module.js";
import { TokenService } from "../common/token.service.js";
import { RankedConfigService } from "../config/ranked-config.service.js";
import type { RankedConfigSnapshot } from "../config/ranked-config.document.js";
import { DATABASE, type DatabasePort, type SqlExecutor } from "../database/database.port.js";
import type { EnvironmentRecheckDto } from "../session/session.dto.js";
import { SessionService } from "../session/session.service.js";
import type { RankedSessionContext } from "../session/session.types.js";

export type DebugBotDifficulty = "EASY" | "NORMAL" | "HARD";
export type DebugBotScenario =
  | "NORMAL_MATCH"
  | "FORCE_BOT_1_CLEAR"
  | "FORCE_BOT_2_CLEARS"
  | "TRIGGER_LAST_ATTEMPT"
  | "TRIGGER_ROUND_DRAW"
  | "TRIGGER_ROUND_3"
  | "TRIGGER_DEATHMATCH";
export type DebugBotBan = "RANDOM" | "NO_BAN";

export interface DebugBotMatchSettings {
  readonly difficulty: DebugBotDifficulty;
  readonly scenario: DebugBotScenario;
  readonly botBan: DebugBotBan;
  readonly sendDiscordEvents: boolean;
  readonly mmrOffset: number;
}

export interface DebugBotMatchCreation {
  readonly matchId: string;
  readonly playerMatchToken: string;
  readonly botMatchToken: string;
  readonly playerContext: RankedSessionContext;
  readonly botContext: RankedSessionContext;
  readonly settings: DebugBotMatchSettings;
}

interface ProfileRow {
  player_id: string;
  hidden_mmr: number;
}

interface QueueRow {
  player_id: string;
  session_id: string;
  hidden_mmr_snapshot: number;
  joined_at: Date | string;
  last_heartbeat_at: Date | string;
  status: "QUEUED" | "MATCHED" | "LEFT" | "EXPIRED";
  matched_match_id: string | null;
}

interface MatchStatusRow {
  id: string;
  state: string;
  state_version: number;
  deadline_at: Date | string | null;
  player_a_id: string;
  player_b_id: string;
}

const ratingRangeAt = (
  joinedAt: Date | string,
  now: Date,
  policy: MatchmakingPolicy,
): number => {
  const waitedSeconds = Math.max(0, (now.getTime() - new Date(joinedAt).getTime()) / 1_000);
  return Math.min(
    policy.maximumRatingRange,
    policy.initialRatingRange + waitedSeconds * policy.widenPerSecond,
  );
};

@Injectable()
export class QueueService {
  public constructor(
    @Inject(DATABASE) private readonly database: DatabasePort,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
    @Inject(ID_GENERATOR) private readonly ids: IdGenerator,
    @Inject(RANDOM_SOURCE) private readonly random: RandomSource,
    private readonly config: RankedConfigService,
    private readonly sessions: SessionService,
    private readonly tokens: TokenService,
  ) {}

  public async join(session: RankedSessionContext, body: EnvironmentRecheckDto) {
    this.sessions.assertEnvironment(body.installedMods);
    const config = this.config.getSnapshot();
    if (!config.operational.enabled || !config.operational.matchmaking || !config.operational.timeouts) {
      throw new ServiceUnavailableException("Ranked queue is not operationally configured");
    }
    const matchmakingPolicy = config.operational.matchmaking;
    const now = this.clock.now();
    const result = await this.database.transaction(async (transaction) => {
      const staleCutoff = new Date(
        now.getTime() - config.operational.timeouts!.queueHeartbeatSeconds * 1_000,
      );
      await transaction.query(
        `UPDATE ranked_queue_entries
         SET status = 'EXPIRED'
         WHERE status = 'QUEUED' AND last_heartbeat_at <= $1`,
        [staleCutoff.toISOString()],
      );
      const profile = await this.lockProfile(transaction, session.playerId);
      await this.assertNoActiveMatch(transaction, session.playerId);
      await transaction.query(
        `INSERT INTO ranked_queue_entries (
           player_id, session_id, hidden_mmr_snapshot, joined_at, last_heartbeat_at, status
         ) VALUES ($1, $2, $3, $4, $4, 'QUEUED')
         ON CONFLICT (player_id) DO UPDATE SET
           session_id = EXCLUDED.session_id,
           hidden_mmr_snapshot = EXCLUDED.hidden_mmr_snapshot,
           joined_at = CASE
             WHEN ranked_queue_entries.status = 'QUEUED' THEN ranked_queue_entries.joined_at
             ELSE EXCLUDED.joined_at
           END,
           last_heartbeat_at = EXCLUDED.last_heartbeat_at,
           status = 'QUEUED',
           matched_match_id = NULL`,
        [session.playerId, session.sessionId, profile.hidden_mmr, now.toISOString()],
      );
      const ownQueue = await this.lockQueueEntry(transaction, session.playerId);
      const candidate = await this.findCandidate(
        transaction,
        ownQueue,
        now,
        matchmakingPolicy,
      );
      if (!candidate) return { status: "QUEUED" as const, matchId: null };
      const matchId = await this.createMatch(
        transaction,
        candidate,
        ownQueue,
        config,
        now,
      );
      return { status: "MATCHED" as const, matchId };
    });
    return {
      ...result,
      serverNow: now.toISOString(),
    };
  }

  public async createDebugBotMatch(
    session: RankedSessionContext,
    body: EnvironmentRecheckDto,
    settings: DebugBotMatchSettings,
  ): Promise<DebugBotMatchCreation> {
    this.sessions.assertEnvironment(body.installedMods);
    const config = this.config.getSnapshot();
    if (!config.operational.enabled || !config.operational.timeouts || !config.operational.mmrPolicy) {
      throw new ServiceUnavailableException("Ranked is not operationally configured");
    }
    const timeouts = config.operational.timeouts;
    const mmrPolicy = config.operational.mmrPolicy;
    const now = this.clock.now();
    return this.database.transaction(async (transaction) => {
      const profile = await this.lockProfile(transaction, session.playerId);
      await this.assertNoActiveMatch(transaction, session.playerId);
      const queued = await transaction.query<{ status: string }>(
        "SELECT status FROM ranked_queue_entries WHERE player_id = $1 AND status = 'QUEUED' FOR UPDATE",
        [session.playerId],
      );
      if (queued.rows[0]) {
        throw new ConflictException("Leave the public Ranked queue before starting Debug Bot Match");
      }

      const botRating = Math.max(0, Math.round(Number(profile.hidden_mmr) + settings.mmrOffset));
      const botPlayerId = this.ids.next();
      const botSessionId = this.ids.next();
      const botAccount = await transaction.query<{ id: string }>(
        "SELECT nextval('ranked_debug_bot_account_id_seq')::text AS id",
      );
      const botAccountId = botAccount.rows[0]?.id;
      if (!botAccountId) throw new Error("Failed to allocate Debug Bot account ID");
      const placementGames = mmrPolicy.placementGames;
      const displayedTier = displayedTierForProfile(
        botRating,
        placementGames,
        mmrPolicy,
        config.operational.tierBands,
      );
      const botName = `BOT ${settings.difficulty}`;
      await transaction.query(
        `INSERT INTO ranked_players (id, gd_account_id, gd_username, is_bot)
         VALUES ($1, $2, $3, TRUE)`,
        [botPlayerId, botAccountId, botName],
      );
      await transaction.query(
        `INSERT INTO ranked_profiles (
           player_id, displayed_tier, hidden_mmr, visible_ranked_score,
           placement_games, initial_csmp_tier, initial_seed_mmr, seed_applied_at
         ) VALUES ($1, $2, $3, $3, $4, 'NONE', $3, $5)`,
        [botPlayerId, displayedTier, botRating, placementGames, now.toISOString()],
      );
      await transaction.query(
        `INSERT INTO ranked_sessions (
           id, player_id, token_hash, client_version, environment_snapshot,
           expires_at, last_heartbeat_at
         ) VALUES ($1, $2, $3, 'debug-bot-server', '[]'::jsonb, $4, $5)`,
        [
          botSessionId,
          botPlayerId,
          this.tokens.hash(`debug-bot-session:${botSessionId}`),
          new Date(now.getTime() + timeouts.sessionSeconds * 1_000).toISOString(),
          now.toISOString(),
        ],
      );

      const playerA: QueueRow = {
        player_id: session.playerId,
        session_id: session.sessionId,
        hidden_mmr_snapshot: Number(profile.hidden_mmr),
        joined_at: now,
        last_heartbeat_at: now,
        status: "MATCHED",
        matched_match_id: null,
      };
      const playerB: QueueRow = {
        player_id: botPlayerId,
        session_id: botSessionId,
        hidden_mmr_snapshot: botRating,
        joined_at: now,
        last_heartbeat_at: now,
        status: "MATCHED",
        matched_match_id: null,
      };
      const matchId = await this.createMatch(transaction, playerA, playerB, config, now, {
        matchType: "DEBUG_BOT",
        discordEventsEnabled: settings.sendDiscordEvents,
        debugBotConfig: {
          ...settings,
          playerSessionId: session.sessionId,
          botSessionId,
        },
        updateQueue: false,
      });
      const botContext: RankedSessionContext = {
        sessionId: botSessionId,
        playerId: botPlayerId,
        gdAccountId: botAccountId,
        gdUsername: botName,
        displayedTier,
        hiddenMmr: botRating,
        placementGames,
      };
      return {
        matchId,
        playerMatchToken: this.tokens.deriveMatchToken(matchId, session.playerId, session.sessionId),
        botMatchToken: this.tokens.deriveMatchToken(matchId, botPlayerId, botSessionId),
        playerContext: session,
        botContext,
        settings,
      };
    });
  }

  public async leave(session: RankedSessionContext) {
    const now = this.clock.now();
    const result = await this.database.query(
      `UPDATE ranked_queue_entries
       SET status = 'LEFT', last_heartbeat_at = $1
       WHERE player_id = $2 AND status = 'QUEUED'`,
      [now.toISOString(), session.playerId],
    );
    return { left: result.rowCount > 0, serverNow: now.toISOString() };
  }

  public async heartbeat(session: RankedSessionContext) {
    const now = this.clock.now();
    await this.database.query(
      `UPDATE ranked_queue_entries
       SET last_heartbeat_at = $1
       WHERE player_id = $2 AND status = 'QUEUED'`,
      [now.toISOString(), session.playerId],
    );
    return { serverNow: now.toISOString() };
  }

  public async status(session: RankedSessionContext) {
    const now = this.clock.now();
    const queue = await this.database.query<QueueRow>(
      "SELECT * FROM ranked_queue_entries WHERE player_id = $1",
      [session.playerId],
    );
    const entry = queue.rows[0];
    if (!entry) return { status: "NOT_QUEUED", serverNow: now.toISOString() };
    if (entry.status !== "MATCHED" || !entry.matched_match_id) {
      const snapshot = this.config.getSnapshot();
      if (entry.status === "QUEUED") {
        await this.database.query(
          `UPDATE ranked_queue_entries
           SET last_heartbeat_at = $1
           WHERE player_id = $2 AND status = 'QUEUED'`,
          [now.toISOString(), session.playerId],
        );
      }
      const searchRange = snapshot.operational.matchmaking
        ? ratingRangeAt(entry.joined_at, now, snapshot.operational.matchmaking)
        : null;
      return {
        status: entry.status,
        joinedAt: new Date(entry.joined_at).toISOString(),
        searchRange,
        serverNow: now.toISOString(),
      };
    }
    const matchResult = await this.database.query<MatchStatusRow>(
      "SELECT id, state, state_version, deadline_at, player_a_id, player_b_id FROM ranked_matches WHERE id = $1",
      [entry.matched_match_id],
    );
    const match = matchResult.rows[0];
    if (!match) throw new NotFoundException("Matched game no longer exists");
    const matchToken = this.tokens.deriveMatchToken(match.id, session.playerId, entry.session_id);
    return {
      status: "MATCHED",
      matchId: match.id,
      matchToken,
      side: match.player_a_id === session.playerId ? "A" : "B",
      matchState: match.state,
      stateVersion: Number(match.state_version),
      deadlineAt: match.deadline_at ? new Date(match.deadline_at).toISOString() : null,
      serverNow: now.toISOString(),
    };
  }

  private async lockProfile(transaction: SqlExecutor, playerId: string): Promise<ProfileRow> {
    const result = await transaction.query<ProfileRow>(
      "SELECT player_id, hidden_mmr FROM ranked_profiles WHERE player_id = $1 FOR UPDATE",
      [playerId],
    );
    const profile = result.rows[0];
    if (!profile || profile.hidden_mmr === null) {
      throw new ConflictException("Ranked profile is not seeded");
    }
    return profile;
  }

  private async assertNoActiveMatch(transaction: SqlExecutor, playerId: string): Promise<void> {
    const active = await transaction.query<{ id: string }>(
      `SELECT id FROM ranked_matches
       WHERE (player_a_id = $1 OR player_b_id = $1)
         AND state NOT IN ('MATCH_RESULT', 'CANCELLED')
       LIMIT 1 FOR UPDATE`,
      [playerId],
    );
    if (active.rows[0]) throw new ConflictException("Player already has an active Ranked match");
  }

  private async lockQueueEntry(transaction: SqlExecutor, playerId: string): Promise<QueueRow> {
    const result = await transaction.query<QueueRow>(
      "SELECT * FROM ranked_queue_entries WHERE player_id = $1 FOR UPDATE",
      [playerId],
    );
    const row = result.rows[0];
    if (!row) throw new Error("Queue entry disappeared during join");
    return row;
  }

  private async findCandidate(
    transaction: SqlExecutor,
    own: QueueRow,
    now: Date,
    policy: MatchmakingPolicy,
  ): Promise<QueueRow | null> {
    const result = await transaction.query<QueueRow>(
      `SELECT * FROM ranked_queue_entries
       WHERE status = 'QUEUED' AND player_id <> $1
       ORDER BY joined_at ASC
       FOR UPDATE SKIP LOCKED`,
      [own.player_id],
    );
    const ownRange = ratingRangeAt(own.joined_at, now, policy);
    for (const candidate of result.rows) {
      const candidateRange = ratingRangeAt(candidate.joined_at, now, policy);
      const difference = Math.abs(
        Number(own.hidden_mmr_snapshot) - Number(candidate.hidden_mmr_snapshot),
      );
      if (difference <= Math.max(ownRange, candidateRange)) return candidate;
    }
    return null;
  }

  private async createMatch(
    transaction: SqlExecutor,
    playerA: QueueRow,
    playerB: QueueRow,
    config: RankedConfigSnapshot,
    now: Date,
    options: {
      readonly matchType?: "RANKED_PVP" | "DEBUG_BOT";
      readonly debugBotConfig?: Readonly<Record<string, unknown>> | null;
      readonly discordEventsEnabled?: boolean;
      readonly updateQueue?: boolean;
    } = {},
  ): Promise<string> {
    if (!config.operational.timeouts) throw new Error("Missing timeout policy");
    const effective = effectiveTierForMatch(
      Number(playerA.hidden_mmr_snapshot),
      Number(playerB.hidden_mmr_snapshot),
      config.operational.tierBands,
    );
    const candidates = selectCandidateMaps(effective.tier, config.maps, this.random);
    const configSnapshotId = await this.persistConfigSnapshot(transaction, config);
    const matchId = this.ids.next();
    const readyDeadline = new Date(
      now.getTime() + config.operational.timeouts.readySeconds * 1_000,
    );
    await transaction.query(
      `INSERT INTO ranked_matches (
         id, player_a_id, player_b_id, config_snapshot_id,
         mmr_a_before, mmr_b_before, effective_rating_average, effective_tier,
         candidate_maps_snapshot, series_state, state, state_version,
         deadline_at, ready_deadline_at, last_heartbeat_a_at, last_heartbeat_b_at,
         rules_version, match_type, debug_bot_config, discord_events_enabled,
         debug_config, debug_discord_events, created_at
       ) VALUES (
         $1, $2, $3, $4, $5, $6, $7, $8,
         $9::jsonb, $10::jsonb, 'MATCHED', 1,
         $11, $11, $13, $13, $12, $14, $15::jsonb, $16,
         $15::jsonb, $16, $13
       )`,
      [
        matchId,
        playerA.player_id,
        playerB.player_id,
        configSnapshotId,
        playerA.hidden_mmr_snapshot,
        playerB.hidden_mmr_snapshot,
        effective.averageRating,
        effective.tier,
        JSON.stringify(candidates),
        JSON.stringify(createMatchSeries()),
        readyDeadline.toISOString(),
        config.operational.rules.rulesVersion,
        now.toISOString(),
        options.matchType ?? "RANKED_PVP",
        JSON.stringify(options.debugBotConfig ?? null),
        options.discordEventsEnabled ?? false,
      ],
    );
    for (const participant of [playerA, playerB]) {
      const token = this.tokens.deriveMatchToken(matchId, participant.player_id, participant.session_id);
      const expiresAt = new Date(
        now.getTime() + config.operational.timeouts.sessionSeconds * 1_000,
      );
      await transaction.query(
        `INSERT INTO ranked_match_tokens (match_id, player_id, token_hash, expires_at)
         VALUES ($1, $2, $3, $4)`,
        [matchId, participant.player_id, this.tokens.hash(token), expiresAt.toISOString()],
      );
    }
    if (options.updateQueue !== false) {
      await transaction.query(
        `UPDATE ranked_queue_entries
         SET status = 'MATCHED', matched_match_id = $1
         WHERE player_id = $2 OR player_id = $3`,
        [matchId, playerA.player_id, playerB.player_id],
      );
    }
    return matchId;
  }

  private async persistConfigSnapshot(
    transaction: SqlExecutor,
    config: RankedConfigSnapshot,
  ): Promise<string> {
    const id = this.ids.next();
    const inserted = await transaction.query<{ id: string }>(
      `INSERT INTO ranked_config_snapshots (
         id, generation, rules_version, source_payload, fetched_at
       ) VALUES ($1, $2, $3, $4::jsonb, $5)
       ON CONFLICT (generation, rules_version) DO NOTHING
       RETURNING id`,
      [
        id,
        config.generation,
        config.operational.rules.rulesVersion,
        JSON.stringify(config),
        config.fetchedAt,
      ],
    );
    if (inserted.rows[0]) return inserted.rows[0].id;
    const existing = await transaction.query<{ id: string }>(
      "SELECT id FROM ranked_config_snapshots WHERE generation = $1 AND rules_version = $2",
      [config.generation, config.operational.rules.rulesVersion],
    );
    if (!existing.rows[0]) throw new Error("Failed to persist config snapshot");
    return existing.rows[0].id;
  }
}
