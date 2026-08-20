import { timingSafeEqual } from "node:crypto";
import {
  ConflictException,
  ForbiddenException,
  Inject,
  Injectable,
  Logger,
  NotFoundException,
  ServiceUnavailableException,
  type OnApplicationBootstrap,
  type OnApplicationShutdown,
} from "@nestjs/common";
import {
  createMatchSeries,
  displayedTierForProfile,
  effectiveTierForMatch,
  selectCandidateMaps,
  type PlayerSide,
  type RandomSource,
  type RankedMapSnapshot,
} from "@corum-ranked/rules";
import {
  ID_GENERATOR,
  RANDOM_SOURCE,
  SERVER_CLOCK,
  type IdGenerator,
  type ServerClock,
} from "../common/runtime.module.js";
import { TokenService } from "../common/token.service.js";
import type { RankedConfigSnapshot } from "../config/ranked-config.document.js";
import { RankedConfigService } from "../config/ranked-config.service.js";
import {
  SERVER_ENVIRONMENT,
  type ServerEnvironment,
} from "../config/server-environment.js";
import { DATABASE, type DatabasePort, type SqlExecutor } from "../database/database.port.js";
import { MatchService } from "../match/match.service.js";
import type { InstalledModDto } from "../session/session.dto.js";
import { SessionService } from "../session/session.service.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { DEBUG_BOT_DIFFICULTY_PROFILES } from "./debug-bot.config.js";
import type { CreateDebugBotMatchDto } from "./debug-bot.dto.js";
import type {
  DebugBotDifficultyProfile,
  DebugBotMatchConfig,
} from "./debug-bot.types.js";

interface SessionEnvironmentRow {
  readonly environment_snapshot: unknown;
}

interface BotPlayerRow {
  readonly id: string;
}

interface ActiveMatchRow {
  readonly id: string;
  readonly match_type: "RANKED_PVP" | "DEBUG_BOT";
}

interface DebugRoundState {
  readonly roundNumber: number;
  readonly map: RankedMapSnapshot;
  readonly scores: Readonly<Record<PlayerSide, number>>;
  readonly clears: Readonly<Record<PlayerSide, number>>;
}

interface DebugDeathmatchState {
  readonly sequence: number;
  readonly map: RankedMapSnapshot;
}

interface DebugMatchState {
  readonly state: string;
  readonly candidateMaps: readonly RankedMapSnapshot[] | null;
  readonly currentRound: DebugRoundState | null;
  readonly deathmatch: DebugDeathmatchState | null;
}

interface AttemptPlan {
  readonly targetProgress: number;
  readonly cleared: boolean;
  readonly progressPerSecond: number;
}

interface ActiveBotAttempt extends AttemptPlan {
  readonly attemptId: string;
  readonly playableLevelId: string;
  readonly deathmatch: boolean;
  readonly startedAtMs: number;
  lastTelemetryAtMs: number;
  currentProgress: number;
}

interface DebugBotRuntime {
  readonly matchId: string;
  readonly config: DebugBotMatchConfig;
  readonly human: RankedSessionContext;
  readonly bot: RankedSessionContext;
  readonly humanMatchToken: string;
  readonly botMatchToken: string;
  readonly installedMods: readonly InstalledModDto[];
  activeAttempt: ActiveBotAttempt | null;
  nextAttemptAtMs: number;
  eventSequence: number;
  lastAttemptStarts: number;
  lastAttemptRound: number;
  deathmatchSequence: number;
  deathmatchAttemptsCompleted: number;
}

const parseJson = <T>(value: unknown): T =>
  typeof value === "string" ? (JSON.parse(value) as T) : structuredClone(value) as T;

const copyInstalledMods = (mods: readonly InstalledModDto[]): InstalledModDto[] =>
  mods.map((mod) => structuredClone(mod));

const sameSecret = (submitted: string, expected: string): boolean => {
  const submittedBytes = Buffer.from(submitted, "utf8");
  const expectedBytes = Buffer.from(expected, "utf8");
  return submittedBytes.length === expectedBytes.length &&
    timingSafeEqual(submittedBytes, expectedBytes);
};

@Injectable()
export class DebugBotMatchService implements OnApplicationBootstrap, OnApplicationShutdown {
  private readonly logger = new Logger(DebugBotMatchService.name);
  private readonly runtimes = new Map<string, DebugBotRuntime>();
  private readonly ticking = new Set<string>();
  private timer: ReturnType<typeof setInterval> | null = null;

  public constructor(
    @Inject(DATABASE) private readonly database: DatabasePort,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
    @Inject(ID_GENERATOR) private readonly ids: IdGenerator,
    @Inject(RANDOM_SOURCE) private readonly random: RandomSource,
    @Inject(SERVER_ENVIRONMENT) private readonly environment: ServerEnvironment,
    private readonly config: RankedConfigService,
    private readonly sessions: SessionService,
    private readonly tokens: TokenService,
    private readonly matches: MatchService,
  ) {}

  public async onApplicationBootstrap(): Promise<void> {
    if (!this.environment.debugBotMatch) return;
    const now = this.clock.now();
    await this.database.query(
      `UPDATE ranked_matches
       SET state = 'CANCELLED', cancellation_reason = 'DEBUG_SERVER_RESTART',
           finished_at = $1, deadline_at = NULL, state_version = state_version + 1
       WHERE match_type = 'DEBUG_BOT'
         AND state NOT IN ('MATCH_RESULT', 'CANCELLED')`,
      [now.toISOString()],
    );
    this.timer = setInterval(() => void this.tickAll(), 100);
    this.timer.unref();
  }

  public onApplicationShutdown(): void {
    if (this.timer) clearInterval(this.timer);
    this.timer = null;
    this.runtimes.clear();
    this.ticking.clear();
  }

  public async create(
    session: RankedSessionContext,
    body: CreateDebugBotMatchDto,
  ) {
    const debugEnvironment = this.environment.debugBotMatch;
    if (!debugEnvironment) throw new NotFoundException("Debug Bot Match is disabled");
    if (!sameSecret(body.password, debugEnvironment.password)) {
      throw new ForbiddenException("Incorrect debug password");
    }
    if (body.sendDiscordEvents && !this.environment.discordRelay) {
      throw new ServiceUnavailableException(
        "Debug Discord events require a configured Discord relay",
      );
    }
    const snapshot = this.config.getSnapshot();
    if (
      !snapshot.operational.enabled ||
      !snapshot.operational.mmrPolicy ||
      !snapshot.operational.timeouts ||
      !snapshot.operational.matchmaking
    ) {
      throw new ServiceUnavailableException("Ranked is not operationally configured");
    }
    const timeouts = snapshot.operational.timeouts;
    const mmrPolicy = snapshot.operational.mmrPolicy;

    const now = this.clock.now();
    const difficultyProfile = DEBUG_BOT_DIFFICULTY_PROFILES[body.difficulty];
    const minimumRating = Math.min(
      ...snapshot.operational.tierBands.map((band) => band.minInclusive),
    );
    const botRating = Math.max(
      minimumRating,
      session.hiddenMmr + difficultyProfile.ratingOffset,
    );
    const storedConfig: DebugBotMatchConfig = {
      difficulty: body.difficulty,
      scenario: body.scenario,
      botBan: body.botBan,
      sendDiscordEvents: body.sendDiscordEvents,
      botRating,
      ratingOffset: difficultyProfile.ratingOffset,
      botPlacementGames: session.placementGames,
    };
    const created = await this.database.transaction(async (transaction) => {
      const active = await transaction.query<ActiveMatchRow>(
        `SELECT id, match_type
         FROM ranked_matches
         WHERE (player_a_id = $1 OR player_b_id = $1)
           AND state NOT IN ('MATCH_RESULT', 'CANCELLED')
         ORDER BY created_at DESC
         FOR UPDATE`,
        [session.playerId],
      );
      if (active.rows.some((match) => match.match_type === "RANKED_PVP")) {
        throw new ConflictException("Finish the active Ranked match before starting debug");
      }
      if (active.rows.some((match) => match.match_type === "DEBUG_BOT")) {
        throw new ConflictException("Player already has an active Debug Bot Match");
      }

      const sessionEnvironment = await transaction.query<SessionEnvironmentRow>(
        "SELECT environment_snapshot FROM ranked_sessions WHERE id = $1",
        [session.sessionId],
      );
      const installedMods = parseJson<InstalledModDto[]>(
        sessionEnvironment.rows[0]?.environment_snapshot ?? [],
      );
      this.sessions.assertEnvironment(installedMods);

      const botPlayer = await transaction.query<BotPlayerRow>(
        `INSERT INTO ranked_players (id, gd_account_id, gd_username)
         VALUES ($1, -2008, 'BOT')
         ON CONFLICT (gd_account_id) DO UPDATE
           SET gd_username = 'BOT', updated_at = $2
         RETURNING id`,
        [this.ids.next(), now.toISOString()],
      );
      const botPlayerId = botPlayer.rows[0]?.id;
      if (!botPlayerId) throw new Error("Failed to create the debug bot participant");

      const effective = effectiveTierForMatch(
        session.hiddenMmr,
        botRating,
        snapshot.operational.tierBands,
      );
      const candidates = selectCandidateMaps(effective.tier, snapshot.maps, this.random);
      const configSnapshotId = await this.persistConfigSnapshot(transaction, snapshot);
      const matchId = this.ids.next();
      const botSessionId = this.ids.next();
      const readyDeadline = new Date(
        now.getTime() + timeouts.readySeconds * 1_000,
      );
      await transaction.query(
        `INSERT INTO ranked_matches (
           id, match_type, debug_config, debug_discord_events,
           player_a_id, player_b_id, config_snapshot_id,
           mmr_a_before, mmr_b_before, effective_rating_average, effective_tier,
           candidate_maps_snapshot, series_state, state, state_version,
           deadline_at, ready_deadline_at, last_heartbeat_a_at, last_heartbeat_b_at,
           rules_version, created_at
         ) VALUES (
           $1, 'DEBUG_BOT', $2::jsonb, $3,
           $4, $5, $6,
           $7, $8, $9, $10,
           $11::jsonb, $12::jsonb, 'MATCHED', 1,
           $13, $13, $14, $14,
           $15, $14
         )`,
        [
          matchId,
          JSON.stringify({ schemaVersion: 2, ...storedConfig }),
          storedConfig.sendDiscordEvents,
          session.playerId,
          botPlayerId,
          configSnapshotId,
          session.hiddenMmr,
          botRating,
          effective.averageRating,
          effective.tier,
          JSON.stringify(candidates),
          JSON.stringify(createMatchSeries()),
          readyDeadline.toISOString(),
          now.toISOString(),
          snapshot.operational.rules.rulesVersion,
        ],
      );

      const expiresAt = new Date(
        now.getTime() + timeouts.sessionSeconds * 1_000,
      );
      const humanMatchToken = this.tokens.deriveMatchToken(
        matchId,
        session.playerId,
        session.sessionId,
      );
      const botMatchToken = this.tokens.deriveMatchToken(matchId, botPlayerId, botSessionId);
      await transaction.query(
        `INSERT INTO ranked_match_tokens (match_id, player_id, token_hash, expires_at)
         VALUES ($1, $2, $3, $4), ($1, $5, $6, $4)`,
        [
          matchId,
          session.playerId,
          this.tokens.hash(humanMatchToken),
          expiresAt.toISOString(),
          botPlayerId,
          this.tokens.hash(botMatchToken),
        ],
      );
      const bot: RankedSessionContext = {
        sessionId: botSessionId,
        playerId: botPlayerId,
        gdAccountId: "-2008",
        gdUsername: "BOT",
        displayedTier: displayedTierForProfile(
          botRating,
          session.placementGames,
          mmrPolicy,
          snapshot.operational.tierBands,
        ),
        hiddenMmr: botRating,
        placementGames: session.placementGames,
      };
      return {
        matchId,
        humanMatchToken,
        botMatchToken,
        bot,
        installedMods,
      };
    });

    this.runtimes.set(created.matchId, {
      matchId: created.matchId,
      config: storedConfig,
      human: session,
      bot: created.bot,
      humanMatchToken: created.humanMatchToken,
      botMatchToken: created.botMatchToken,
      installedMods: created.installedMods,
      activeAttempt: null,
      nextAttemptAtMs: now.getTime(),
      eventSequence: 0,
      lastAttemptStarts: 0,
      lastAttemptRound: 0,
      deathmatchSequence: 0,
      deathmatchAttemptsCompleted: 0,
    });
    return {
      matchId: created.matchId,
      matchToken: created.humanMatchToken,
      side: "A" as const,
      matchType: "DEBUG_BOT" as const,
      debug: storedConfig,
      serverNow: now.toISOString(),
    };
  }

  public async tickMatchNow(matchId: string): Promise<boolean> {
    const runtime = this.runtimes.get(matchId);
    if (!runtime || this.ticking.has(matchId)) return false;
    this.ticking.add(matchId);
    try {
      await this.tickRuntime(runtime);
      return true;
    } finally {
      this.ticking.delete(matchId);
    }
  }

  private async tickAll(): Promise<void> {
    for (const matchId of this.runtimes.keys()) {
      try {
        await this.tickMatchNow(matchId);
      } catch (error) {
        const message = error instanceof Error ? error.message : "unknown simulator failure";
        this.logger.warn(`Debug Bot Match ${matchId} tick failed: ${message}`);
      }
    }
  }

  private async tickRuntime(runtime: DebugBotRuntime): Promise<void> {
    const state = await this.matches.state(
      runtime.matchId,
      runtime.botMatchToken,
      runtime.bot,
    ) as DebugMatchState;
    switch (state.state) {
      case "MATCHED":
        await this.matches.ready(runtime.matchId, runtime.botMatchToken, runtime.bot, {
          installedMods: copyInstalledMods(runtime.installedMods),
        });
        return;
      case "BAN_PHASE":
        await this.submitBotBan(runtime, state);
        return;
      case "ROUND_PREPARE":
      case "DEATHMATCH_PREPARE":
        await this.matches.ready(runtime.matchId, runtime.botMatchToken, runtime.bot, {
          installedMods: copyInstalledMods(runtime.installedMods),
        });
        return;
      case "ROUND_PLAYING":
      case "FINAL_ATTEMPT_WINDOW":
      case "LAST_ATTEMPT_WINDOW":
      case "ROUND_SETTLING":
        await this.driveRound(runtime, state);
        return;
      case "DEATHMATCH_PLAYING":
        await this.driveDeathmatch(runtime, state);
        return;
      case "MATCH_RESULT":
      case "CANCELLED":
        this.runtimes.delete(runtime.matchId);
        return;
      default:
        return;
    }
  }

  private async submitBotBan(
    runtime: DebugBotRuntime,
    state: DebugMatchState,
  ): Promise<void> {
    let canonicalLevelId: string | null = null;
    const candidates = state.candidateMaps ?? [];
    if (
      runtime.config.botBan === "RANDOM" &&
      candidates.length > 0 &&
      this.random.next() >= 0.5
    ) {
      const index = Math.min(
        candidates.length - 1,
        Math.floor(this.random.next() * candidates.length),
      );
      canonicalLevelId = candidates[index]?.canonicalLevelId ?? null;
    }
    await this.matches.submitBan(
      runtime.matchId,
      runtime.botMatchToken,
      runtime.bot,
      canonicalLevelId ? { canonicalLevelId } : {},
    );
  }

  private async driveRound(
    runtime: DebugBotRuntime,
    state: DebugMatchState,
  ): Promise<void> {
    const round = state.currentRound;
    if (!round) return;
    if (runtime.lastAttemptRound !== round.roundNumber) {
      runtime.lastAttemptRound = round.roundNumber;
      runtime.lastAttemptStarts = 0;
      runtime.activeAttempt = null;
      runtime.nextAttemptAtMs = this.clock.now().getTime();
    }
    if (state.state === "ROUND_PLAYING" && await this.driveScenarioPrelude(runtime, round)) {
      return;
    }
    await this.driveBotAttempt(runtime, state, false);
  }

  private async driveScenarioPrelude(
    runtime: DebugBotRuntime,
    round: DebugRoundState,
  ): Promise<boolean> {
    const clearsA = Number(round.clears.A);
    const clearsB = Number(round.clears.B);
    switch (runtime.config.scenario) {
      case "NORMAL_MATCH":
        return false;
      case "FORCE_BOT_ONE_CLEAR":
        if (clearsB < 1) {
          await this.emitInstantAttempt(runtime, "B", round.map.playableLevelId, 100, true);
          return true;
        }
        return false;
      case "FORCE_BOT_TWO_CLEARS":
        if (clearsB < 2) {
          await this.emitInstantAttempt(runtime, "B", round.map.playableLevelId, 100, true);
        }
        return true;
      case "TRIGGER_LAST_ATTEMPT":
      case "TRIGGER_ROUND_DRAW":
        if (round.roundNumber !== 1) return false;
        return this.seedLastAttempt(runtime, round.map.playableLevelId, clearsA, clearsB);
      case "TRIGGER_ROUND_THREE":
        if (round.roundNumber === 1) {
          if (clearsA < 2) {
            await this.emitInstantAttempt(runtime, "A", round.map.playableLevelId, 100, true);
          }
          return true;
        }
        if (round.roundNumber === 2) {
          if (clearsB < 2) {
            await this.emitInstantAttempt(runtime, "B", round.map.playableLevelId, 100, true);
          }
          return true;
        }
        return false;
      case "TRIGGER_DEATHMATCH":
        if (round.roundNumber === 1) {
          if (clearsA < 2) {
            await this.emitInstantAttempt(runtime, "A", round.map.playableLevelId, 100, true);
          }
          return true;
        }
        if (round.roundNumber === 2) {
          if (clearsB < 2) {
            await this.emitInstantAttempt(runtime, "B", round.map.playableLevelId, 100, true);
          }
          return true;
        }
        return this.seedLastAttempt(runtime, round.map.playableLevelId, clearsA, clearsB);
    }
  }

  private async seedLastAttempt(
    runtime: DebugBotRuntime,
    playableLevelId: string,
    clearsA: number,
    clearsB: number,
  ): Promise<boolean> {
    if (clearsB < 1) {
      await this.emitInstantAttempt(runtime, "B", playableLevelId, 100, true);
      return true;
    }
    if (clearsA < 2) {
      await this.emitInstantAttempt(runtime, "A", playableLevelId, 100, true);
      return true;
    }
    return false;
  }

  private async driveDeathmatch(
    runtime: DebugBotRuntime,
    state: DebugMatchState,
  ): Promise<void> {
    const deathmatch = state.deathmatch;
    if (!deathmatch) return;
    if (runtime.deathmatchSequence !== deathmatch.sequence) {
      runtime.deathmatchSequence = deathmatch.sequence;
      runtime.deathmatchAttemptsCompleted = 0;
      runtime.activeAttempt = null;
      runtime.nextAttemptAtMs = this.clock.now().getTime();
    }
    if (runtime.deathmatchAttemptsCompleted >= 3) return;
    await this.driveBotAttempt(runtime, state, true);
  }

  private async driveBotAttempt(
    runtime: DebugBotRuntime,
    state: DebugMatchState,
    deathmatch: boolean,
  ): Promise<void> {
    const nowMs = this.clock.now().getTime();
    if (runtime.activeAttempt) {
      await this.advanceActiveAttempt(runtime, state, nowMs);
      return;
    }
    if (nowMs < runtime.nextAttemptAtMs) return;
    if (deathmatch && runtime.deathmatchAttemptsCompleted >= 3) return;

    const map = deathmatch ? state.deathmatch?.map : state.currentRound?.map;
    if (!map) return;
    const plan = this.createAttemptPlan(runtime, state, map, deathmatch);
    const response = await this.matches.startAttempt(
      runtime.matchId,
      runtime.botMatchToken,
      runtime.bot,
      {
        levelId: map.playableLevelId,
        clientEventId: this.eventId(runtime, deathmatch ? "dm-start" : "start"),
      },
    );
    if (!response.accepted || !response.attemptId) {
      runtime.nextAttemptAtMs = nowMs + 250;
      return;
    }
    if (!deathmatch && state.state === "LAST_ATTEMPT_WINDOW") {
      runtime.lastAttemptStarts += 1;
    }
    runtime.activeAttempt = {
      attemptId: response.attemptId,
      playableLevelId: map.playableLevelId,
      deathmatch,
      startedAtMs: nowMs,
      lastTelemetryAtMs: nowMs,
      currentProgress: 0,
      ...plan,
    };
  }

  private async advanceActiveAttempt(
    runtime: DebugBotRuntime,
    state: DebugMatchState,
    nowMs: number,
  ): Promise<void> {
    const active = runtime.activeAttempt;
    if (!active) return;
    const elapsedSeconds = Math.max(0, nowMs - active.startedAtMs) / 1_000;
    const progress = Math.min(
      active.targetProgress,
      Math.floor(elapsedSeconds * active.progressPerSecond),
    );
    if (
      !active.deathmatch &&
      progress !== active.currentProgress &&
      nowMs - active.lastTelemetryAtMs >= 100
    ) {
      await this.matches.updateAttemptProgress(
        runtime.matchId,
        runtime.botMatchToken,
        runtime.bot,
        {
          levelId: active.playableLevelId,
          attemptId: active.attemptId,
          progressPercent: progress,
        },
      );
      active.currentProgress = progress;
      active.lastTelemetryAtMs = nowMs;
    } else if (active.deathmatch) {
      active.currentProgress = progress;
    }
    if (progress < active.targetProgress) return;

    const response = await this.matches.endAttempt(
      runtime.matchId,
      runtime.botMatchToken,
      runtime.bot,
      {
        levelId: active.playableLevelId,
        attemptId: active.attemptId,
        clientEventId: this.eventId(runtime, active.deathmatch ? "dm-end" : "end"),
        progressPercent: active.targetProgress,
        cleared: active.cleared,
      },
    );
    if (response.accepted && active.deathmatch) runtime.deathmatchAttemptsCompleted += 1;
    runtime.activeAttempt = null;
    const profile = DEBUG_BOT_DIFFICULTY_PROFILES[runtime.config.difficulty];
    runtime.nextAttemptAtMs = nowMs + profile.restartDelayMs;
    if (state.state === "LAST_ATTEMPT_WINDOW" && !active.cleared) {
      runtime.nextAttemptAtMs = nowMs + Math.min(150, profile.restartDelayMs);
    }
  }

  private createAttemptPlan(
    runtime: DebugBotRuntime,
    state: DebugMatchState,
    map: RankedMapSnapshot,
    deathmatch: boolean,
  ): AttemptPlan {
    const profile = DEBUG_BOT_DIFFICULTY_PROFILES[runtime.config.difficulty];
    if (deathmatch && runtime.config.scenario === "TRIGGER_DEATHMATCH") {
      const targets = [58, 74, 91] as const;
      return {
        targetProgress: targets[Math.min(runtime.deathmatchAttemptsCompleted, 2)]!,
        cleared: false,
        progressPerSecond: 120,
      };
    }
    if (!deathmatch && state.state === "LAST_ATTEMPT_WINDOW") {
      return this.lastAttemptPlan(runtime, profile);
    }
    const forceFailure = runtime.config.scenario === "FORCE_BOT_ONE_CLEAR" &&
      Number(state.currentRound?.clears.B ?? 0) >= 1;
    const cleared = !forceFailure && this.random.next() < profile.clearProbability;
    if (cleared) {
      return { targetProgress: 100, cleared: true, progressPerSecond: profile.progressPerSecond };
    }
    const qualifying = Math.max(0, Math.min(100, map.qualifyingPercent));
    const reachesQualifying = this.random.next() < profile.qualifyingReachProbability;
    const noise = (this.random.next() - 0.5) * 30;
    const desired = Math.round(profile.averageProgress + noise);
    const minimum = reachesQualifying ? Math.min(99, Math.ceil(qualifying)) : 1;
    const maximum = reachesQualifying ? 99 : Math.max(1, Math.ceil(qualifying) - 1);
    return {
      targetProgress: Math.max(minimum, Math.min(maximum, desired)),
      cleared: false,
      progressPerSecond: profile.progressPerSecond,
    };
  }

  private lastAttemptPlan(
    runtime: DebugBotRuntime,
    profile: DebugBotDifficultyProfile,
  ): AttemptPlan {
    const forceClear = runtime.config.scenario === "TRIGGER_ROUND_DRAW" ||
      runtime.config.scenario === "TRIGGER_DEATHMATCH" ||
      (runtime.config.scenario === "TRIGGER_LAST_ATTEMPT" &&
        runtime.config.difficulty === "HARD");
    if (forceClear && runtime.lastAttemptStarts >= 1) {
      return { targetProgress: 100, cleared: true, progressPerSecond: 120 };
    }
    if (
      runtime.config.scenario === "TRIGGER_LAST_ATTEMPT" &&
      runtime.config.difficulty === "NORMAL" &&
      runtime.lastAttemptStarts >= 1 &&
      this.random.next() < profile.clearProbability
    ) {
      return { targetProgress: 100, cleared: true, progressPerSecond: 110 };
    }
    const failures = [36, 63, 88] as const;
    return {
      targetProgress: failures[Math.min(runtime.lastAttemptStarts, failures.length - 1)]!,
      cleared: false,
      progressPerSecond: 105,
    };
  }

  private async emitInstantAttempt(
    runtime: DebugBotRuntime,
    side: PlayerSide,
    playableLevelId: string,
    progressPercent: number,
    cleared: boolean,
  ): Promise<boolean> {
    const session = side === "A" ? runtime.human : runtime.bot;
    const matchToken = side === "A" ? runtime.humanMatchToken : runtime.botMatchToken;
    const started = await this.matches.startAttempt(runtime.matchId, matchToken, session, {
      levelId: playableLevelId,
      clientEventId: this.eventId(runtime, `scenario-${side.toLowerCase()}-start`),
    });
    if (!started.accepted || !started.attemptId) return false;
    const ended = await this.matches.endAttempt(runtime.matchId, matchToken, session, {
      levelId: playableLevelId,
      attemptId: started.attemptId,
      clientEventId: this.eventId(runtime, `scenario-${side.toLowerCase()}-end`),
      progressPercent,
      cleared,
    });
    return ended.accepted;
  }

  private eventId(runtime: DebugBotRuntime, kind: string): string {
    runtime.eventSequence += 1;
    return `debug-${kind}-${runtime.eventSequence}`;
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
    if (!existing.rows[0]) throw new Error("Failed to persist debug config snapshot");
    return existing.rows[0].id;
  }
}
