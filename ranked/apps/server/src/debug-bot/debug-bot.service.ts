import { timingSafeEqual } from "node:crypto";
import {
  Inject,
  Injectable,
  NotFoundException,
  type OnApplicationShutdown,
  type OnModuleInit,
} from "@nestjs/common";
import type { DisplayTier, RandomSource } from "@corum-ranked/rules";
import {
  RANDOM_SOURCE,
  SERVER_CLOCK,
  type ServerClock,
} from "../common/runtime.module.js";
import { TokenService } from "../common/token.service.js";
import {
  SERVER_ENVIRONMENT,
  type DebugBotEnvironment,
  type ServerEnvironment,
} from "../config/server-environment.js";
import { RankedConfigService } from "../config/ranked-config.service.js";
import { DATABASE, type DatabasePort } from "../database/database.port.js";
import { MatchService } from "../match/match.service.js";
import {
  QueueService,
  type DebugBotMatchCreation,
  type DebugBotMatchSettings,
  type DebugBotScenario,
} from "../queue/queue.service.js";
import type { InstalledModDto } from "../session/session.dto.js";
import type { RankedSessionContext } from "../session/session.types.js";
import type { CreateDebugBotMatchDto } from "./debug-bot.dto.js";
import { createDebugBotAttemptPlan } from "./debug-bot.behavior.js";

interface BotDriver {
  readonly creation: DebugBotMatchCreation;
  attemptId: string | null;
  attemptContextKey: string;
  attemptProgress: number;
  attemptTarget: number;
  attemptStartedAtMs: number;
  attemptDurationMs: number;
  attemptRestartDelayMs: number;
  nextAttemptAtMs: number;
  eventSequence: number;
  banSubmitted: boolean;
  seededRounds: Set<number>;
  busy: boolean;
}

interface ActiveDebugMatchRow {
  id: string;
  debug_config: unknown;
  player_a_id: string;
  player_b_id: string;
  player_a_account_id: string;
  player_b_account_id: string;
  player_a_name: string;
  player_b_name: string;
  player_a_tier: DisplayTier;
  player_b_tier: DisplayTier;
  player_a_mmr: number;
  player_b_mmr: number;
  player_a_placements: number;
  player_b_placements: number;
}

const parseObject = (value: unknown): Record<string, unknown> => {
  if (typeof value === "string") return JSON.parse(value) as Record<string, unknown>;
  return value && typeof value === "object" ? value as Record<string, unknown> : {};
};

@Injectable()
export class DebugBotService implements OnModuleInit, OnApplicationShutdown {
  private readonly configuration: DebugBotEnvironment | null;
  private readonly drivers = new Map<string, BotDriver>();
  private interval: ReturnType<typeof setInterval> | null = null;
  private discovery:Promise<void>|null=null;
  private nextDiscoveryAt=-Infinity;

  public constructor(
    @Inject(SERVER_ENVIRONMENT) environment: ServerEnvironment,
    @Inject(DATABASE) private readonly database: DatabasePort,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
    @Inject(RANDOM_SOURCE) private readonly random: RandomSource,
    private readonly queue: QueueService,
    private readonly matches: MatchService,
    private readonly config: RankedConfigService,
    private readonly tokens: TokenService,
  ) {
    this.configuration = environment.debugBot;
  }

  public async onModuleInit(): Promise<void> {
    if (!this.configuration) return;
    await this.hydrateDrivers(true).catch(()=>{});
    this.interval = setInterval(() => void this.tickOnce(), this.configuration.tickMs);
    this.interval.unref?.();
  }

  public onApplicationShutdown(): void {
    if (this.interval) clearInterval(this.interval);
    this.interval = null;
  }

  public async create(session: RankedSessionContext, body: CreateDebugBotMatchDto) {
    const configuration = this.configuration;
    if (!configuration) throw new NotFoundException("Debug Bot Match is disabled");
    if (!this.passwordMatches(body.password, configuration.password)) {
      throw new NotFoundException("Incorrect password.");
    }
    const settings: DebugBotMatchSettings = {
      difficulty: body.difficulty,
      scenario: body.scenario,
      botBan: body.botBan,
      sendDiscordEvents: body.sendDiscordEvents,
      mmrOffset: configuration.difficulties[body.difficulty].mmrOffset,
    };
    const creation = await this.queue.createDebugBotMatch(session, body, settings);
    if (settings.scenario === "FORCE_WIN" || settings.scenario === "FORCE_LOSE") {
      await this.matches.forceDebugResult(
        creation.matchId,
        settings.scenario === "FORCE_WIN" ? "A" : "B",
      );
    } else {
      this.drivers.set(creation.matchId, this.newDriver(creation));
      await this.drive(creation.matchId);
    }
    return {
      debug: true,
      matchType: "DEBUG_BOT",
      matchId: creation.matchId,
      matchToken: creation.playerMatchToken,
      side: "A",
      opponent: {
        name: creation.botContext.gdUsername,
        difficulty: settings.difficulty,
      },
      scenario: settings.scenario,
      serverNow: this.clock.now().toISOString(),
    };
  }

  public async tickOnce(): Promise<void> {
    if (!this.configuration) return;
    await this.hydrateDrivers().catch(()=>{});
    await Promise.all([...this.drivers.keys()].map(async (matchId) => {
      try {
        await this.drive(matchId);
      } catch {
        // Debug simulation failures are retried on the next tick. Request bodies/passwords are never logged.
      }
    }));
  }

  private passwordMatches(actualText: string, expectedText: string): boolean {
    const actual = Buffer.from(actualText, "utf8");
    const expected = Buffer.from(expectedText, "utf8");
    return actual.length === expected.length && timingSafeEqual(actual, expected);
  }

  private newDriver(creation: DebugBotMatchCreation): BotDriver {
    return {
      creation,
      attemptId: null,
      attemptContextKey: "",
      attemptProgress: 0,
      attemptTarget: 0,
      attemptStartedAtMs: 0,
      attemptDurationMs: 0,
      attemptRestartDelayMs: 0,
      nextAttemptAtMs: 0,
      eventSequence: 0,
      banSubmitted: false,
      seededRounds: new Set<number>(),
      busy: false,
    };
  }

  private hydrateDrivers(force=false):Promise<void> {
    if(this.discovery)return this.discovery;
    const now=this.clock.now().getTime();
    if(!force && now<this.nextDiscoveryAt)return Promise.resolve();
    this.nextDiscoveryAt=now+2000;
    this.discovery=this.loadDrivers().finally(()=>{this.discovery=null;});
    return this.discovery;
  }
  private async loadDrivers(): Promise<void> {
    const known=[...this.drivers.keys()];
    const result = await this.database.query<ActiveDebugMatchRow>(
      `SELECT m.id, m.debug_config, m.player_a_id, m.player_b_id,
              pa.gd_account_id::text AS player_a_account_id,
              pb.gd_account_id::text AS player_b_account_id,
              pa.gd_username AS player_a_name, pb.gd_username AS player_b_name,
              ra.displayed_tier AS player_a_tier, rb.displayed_tier AS player_b_tier,
              ra.hidden_mmr AS player_a_mmr, rb.hidden_mmr AS player_b_mmr,
              ra.placement_games AS player_a_placements,
              rb.placement_games AS player_b_placements
       FROM ranked_matches m
       JOIN ranked_players pa ON pa.id = m.player_a_id
       JOIN ranked_players pb ON pb.id = m.player_b_id
       JOIN ranked_profiles ra ON ra.player_id = m.player_a_id
       JOIN ranked_profiles rb ON rb.player_id = m.player_b_id
       WHERE m.match_type = 'DEBUG_BOT'
         AND m.state NOT IN ('MATCH_RESULT', 'CANCELLED')`,
    );
    const live=new Set(result.rows.map(row=>row.id));
    for(const id of known)if(!live.has(id) && !this.drivers.get(id)?.busy)this.drivers.delete(id);
    for (const row of result.rows) {
      if (this.drivers.has(row.id)) continue;
      const stored = parseObject(row.debug_config);
      const playerSessionId = String(stored.playerSessionId ?? "");
      const botSessionId = String(stored.botSessionId ?? "");
      if (!playerSessionId || !botSessionId) continue;
      const settings: DebugBotMatchSettings = {
        difficulty: stored.difficulty as DebugBotMatchSettings["difficulty"],
        scenario: stored.scenario as DebugBotMatchSettings["scenario"],
        botBan: stored.botBan as DebugBotMatchSettings["botBan"],
        sendDiscordEvents: Boolean(stored.sendDiscordEvents),
        mmrOffset: Number(stored.mmrOffset ?? 0),
      };
      const playerContext: RankedSessionContext = {
        sessionId: playerSessionId,
        playerId: row.player_a_id,
        gdAccountId: row.player_a_account_id,
        gdUsername: row.player_a_name,
        displayedTier: row.player_a_tier,
        hiddenMmr: Number(row.player_a_mmr),
        placementGames: Number(row.player_a_placements),
      };
      const botContext: RankedSessionContext = {
        sessionId: botSessionId,
        playerId: row.player_b_id,
        gdAccountId: row.player_b_account_id,
        gdUsername: row.player_b_name,
        displayedTier: row.player_b_tier,
        hiddenMmr: Number(row.player_b_mmr),
        placementGames: Number(row.player_b_placements),
      };
      this.drivers.set(row.id, this.newDriver({
        matchId: row.id,
        playerMatchToken: this.tokens.deriveMatchToken(row.id, row.player_a_id, playerSessionId),
        botMatchToken: this.tokens.deriveMatchToken(row.id, row.player_b_id, botSessionId),
        playerContext,
        botContext,
        settings,
      }));
    }
  }

  private async drive(matchId: string): Promise<void> {
    const driver = this.drivers.get(matchId);
    if (!driver || driver.busy) return;
    driver.busy = true;
    try {
      let state = await this.matches.state(
        matchId,
        driver.creation.botMatchToken,
        driver.creation.botContext,
      ) as Record<string, any>;
      if (state.state === "MATCHED" && driver.creation.settings.scenario === "TRIGGER_DEATHMATCH") {
        await this.matches.forceDebugDeathmatch(matchId);
        state = await this.matches.state(
          matchId,
          driver.creation.botMatchToken,
          driver.creation.botContext,
        ) as Record<string, any>;
      }
      this.synchronizeAttemptContext(driver, state);
      if (state.state === "MATCH_RESULT" || state.state === "CANCELLED") {
        this.drivers.delete(matchId);
        return;
      }
      if (state.state === "MATCHED") {
        await this.ready(driver, false);
        return;
      }
      if (state.state === "BAN_PHASE") {
        await this.submitBotBan(driver, state);
        return;
      }
      if (state.state === "ROUND_PREPARE" || state.state === "DEATHMATCH_PREPARE") {
        await this.ready(driver, false);
        if (this.autoDrivesPlayer(driver.creation.settings.scenario)) {
          await this.ready(driver, true);
        }
        return;
      }
      if (state.state === "ROUND_RESULT") {
        if (this.autoDrivesPlayer(driver.creation.settings.scenario)) {
          await this.database.query(
            "UPDATE ranked_matches SET deadline_at = $2 WHERE id = $1 AND state = 'ROUND_RESULT'",
            [matchId, this.clock.now().toISOString()],
          );
        }
        return;
      }
      if (["ROUND_PLAYING", "FINAL_ATTEMPT_WINDOW", "LAST_ATTEMPT_WINDOW", "ROUND_SETTLING"].includes(state.state)) {
        const round = state.currentRound as Record<string, any> | null;
        if (!round) return;

        // ROUND_SETTLING means the start window has closed but an already
        // accepted attempt is still alive. Scenario seeding must never create
        // new attempts here, but a Bot attempt that started before the deadline
        // still needs to keep receiving progress/end ticks until it naturally
        // finishes. alpha.30 stopped driving the Bot as soon as the match entered
        // ROUND_SETTLING, permanently trapping LAST ATTEMPT Bot Matches.
        if (state.state !== "ROUND_SETTLING") {
          await this.seedScenarioRound(driver, state, round);
          if (!driver.seededRounds.has(Number(round.roundNumber))) return;
        }
        const refreshed = await this.matches.state(
          matchId,
          driver.creation.botMatchToken,
          driver.creation.botContext,
        ) as Record<string, any>;
        this.synchronizeAttemptContext(driver, refreshed);
        if (["ROUND_PLAYING", "FINAL_ATTEMPT_WINDOW", "LAST_ATTEMPT_WINDOW", "ROUND_SETTLING"].includes(refreshed.state)) {
          if (refreshed.state === "ROUND_SETTLING" && !driver.attemptId) {
            await this.recoverSettlingBotAttempt(driver, refreshed);
          }
          await this.simulateBotAttempt(driver, refreshed, false);
        }
        return;
      }
      if (state.state === "DEATHMATCH_PLAYING") {
        await this.simulateBotAttempt(driver, state, true);
      }
    } finally {
      driver.busy = false;
    }
  }

  private installedMods(): InstalledModDto[] {
    const snapshot = this.config.getSnapshot();
    return snapshot.allowedMods
      .filter((rule) => rule.enabled && rule.required)
      .map((rule) => {
        const mod: InstalledModDto = {
          id: rule.id,
          version: rule.minVersion ?? rule.maxVersion ?? "v0.0.0",
          enabled: true,
          loaded: true,
          internal: false,
          system: false,
        };
        if (rule.id === snapshot.operational.cbf.modId) {
          mod.settings = { ...snapshot.operational.cbf.requiredSettings };
        }
        return mod;
      });
  }

  private synchronizeAttemptContext(driver: BotDriver, state: Record<string, any>): void {
    const deathmatch = String(state.state).startsWith("DEATHMATCH_");
    const key = deathmatch
      ? `D${Number(state.deathmatch?.sequence ?? 0)}`
      : `R${Number(state.currentRound?.roundNumber ?? 0)}`;
    if (driver.attemptContextKey === key && state.state !== "ROUND_RESULT" &&
        state.state !== "DEATHMATCH_RESULT") return;
    // Surrender closes both authoritative attempts without the driver's End
    // callback. Never send the old attempt/progress against the next round/map.
    // LAST_ATTEMPT_WINDOW / ROUND_SETTLING keep the same context and attempt.
    driver.attemptContextKey = key;
    driver.attemptId = null;
    driver.attemptProgress = driver.attemptTarget = 0;
    driver.attemptStartedAtMs = driver.attemptDurationMs = 0;
    driver.attemptRestartDelayMs = driver.nextAttemptAtMs = 0;
  }

  private async ready(driver: BotDriver, player: boolean): Promise<void> {
    const creation = driver.creation;
    await this.matches.ready(
      creation.matchId,
      player ? creation.playerMatchToken : creation.botMatchToken,
      player ? creation.playerContext : creation.botContext,
      { installedMods: this.installedMods() },
    );
  }

  private async submitBotBan(driver: BotDriver, state: Record<string, any>): Promise<void> {
    if (driver.banSubmitted) return;
    const candidates = Array.isArray(state.candidateMaps) ? state.candidateMaps : [];
    const index = candidates.length > 0 ? Math.floor(this.random.next() * candidates.length) : -1;
    const canonicalLevelId = driver.creation.settings.botBan === "RANDOM" && index >= 0
      ? String(candidates[index].canonicalLevelId)
      : null;
    await this.matches.submitBan(
      driver.creation.matchId,
      driver.creation.botMatchToken,
      driver.creation.botContext,
      { canonicalLevelId },
    );
    driver.banSubmitted = true;
  }

  private autoDrivesPlayer(scenario: DebugBotScenario): boolean {
    return scenario === "TRIGGER_ROUND_3";
  }

  private async seedScenarioRound(
    driver: BotDriver,
    state: Record<string, any>,
    round: Record<string, any>,
  ): Promise<void> {
    const roundNumber = Number(round.roundNumber);
    if (driver.seededRounds.has(roundNumber)) return;
    const scenario = driver.creation.settings.scenario;
    const levelId = String(round.map.playableLevelId);
    let steps: boolean[] = [];
    if (scenario === "FORCE_BOT_1_CLEAR" && roundNumber === 1) {
      steps = [false];
    } else if (scenario === "FORCE_BOT_3_CLEARS" && roundNumber === 1) {
      steps = [false, false, false];
    } else if (scenario === "TRIGGER_LAST_ATTEMPT" && roundNumber === 1) {
      steps = [false, false, true, true, true];
    } else if (scenario === "TRIGGER_ROUND_DRAW") {
      steps = [false, true, true, true, false, false];
    } else if (scenario === "TRIGGER_ROUND_3") {
      if (roundNumber === 1) steps = [true, true, true];
      else if (roundNumber === 2) steps = [false, false, false];
    }
    const targets = { A: 0, B: 0 };
    let current = state;
    for (const [index, player] of steps.entries()) {
      const side = player ? "A" : "B";
      targets[side] += 1;
      // Durable clear counts are the checkpoint, including after a lost ACK
      // or driver restart. Never repeat the already completed Bot prefix.
      const clears = Number(current.currentRound?.clears?.[side] ?? 0);
      if (clears >= targets[side]) continue;
      if (!this.isSameSeedRound(current, roundNumber, levelId)) return;
      await this.completeAttempt(driver, player, levelId, 100, true,
        `scenario-r${roundNumber}-step${index}`, roundNumber);
      current = await this.matches.state(driver.creation.matchId,
        driver.creation.botMatchToken, driver.creation.botContext) as Record<string, any>;
      const refreshedClears = Number(current.currentRound?.clears?.[side] ?? 0);
      if (refreshedClears < targets[side]) return;
    }
    driver.seededRounds.add(roundNumber);
    if (state.state !== "ROUND_PLAYING") driver.attemptId = null;
  }

  private isSameSeedRound(state: Record<string, any>, round: number, levelId: string): boolean {
    return ["ROUND_PLAYING", "FINAL_ATTEMPT_WINDOW", "LAST_ATTEMPT_WINDOW"].includes(state.state) &&
      Number(state.currentRound?.roundNumber) === round &&
      String(state.currentRound?.map?.playableLevelId) === levelId;
  }

  private async completeAttempt(
    driver: BotDriver,
    player: boolean,
    levelId: string,
    progressPercent: number,
    cleared: boolean,
    kind: string,
    roundNumber: number,
  ): Promise<boolean> {
    const creation = driver.creation;
    const context = player ? creation.playerContext : creation.botContext;
    const token = player ? creation.playerMatchToken : creation.botMatchToken;
    const event = `debug-bot-${kind}`;
    const started = await this.matches.startAttempt(creation.matchId, token, context, {
      levelId,
      clientEventId: `${event}-start`,
    });
    let attemptId = started.accepted ? started.attemptId : null;
    if (!attemptId && started.reason === "ATTEMPT_ALREADY_ACTIVE") {
      // These development scenarios explicitly simulate both players. GO can
      // already have admitted the real client's first Attempt. Complete that
      // exact accepted Attempt through the normal End authority, rather than
      // silently skipping it or changing scores/entry state in the database.
      const active = await this.database.query<{ domain_attempt_id: string }>(
        `SELECT a.domain_attempt_id FROM ranked_attempts a
         JOIN ranked_rounds r ON r.id = a.round_id
         WHERE r.match_id = $1 AND r.round_number = $2 AND a.player_id = $3
           AND a.played_level_id = $4 AND a.ended_at IS NULL AND a.valid = TRUE
         ORDER BY a.attempt_sequence DESC LIMIT 1`,
        [creation.matchId, roundNumber, context.playerId, levelId],
      );
      attemptId = active.rows[0]?.domain_attempt_id ?? null;
    }
    if (!attemptId) return false;
    const ended = await this.matches.endAttempt(creation.matchId, token, context, {
      levelId,
      attemptId,
      clientEventId: `${event}-end`,
      progressPercent,
      cleared,
    });
    if (ended.accepted && !player && driver.attemptId === attemptId) driver.attemptId = null;
    return ended.accepted;
  }

  private async recoverSettlingBotAttempt(
    driver: BotDriver,
    state: Record<string, any>,
  ): Promise<void> {
    if (driver.attemptId || state.state !== "ROUND_SETTLING") return;
    const roundNumber = Number(state.currentRound?.roundNumber ?? 0);
    if (!Number.isSafeInteger(roundNumber) || roundNumber <= 0) return;

    // Debug Bot state is intentionally in-memory, but Render/dev restarts can
    // recreate the driver while an authoritative Bot attempt is still open in
    // PostgreSQL. Recover that attempt instead of trying to start a new one after
    // the start window has already closed. Live progress is ephemeral, so resume
    // from 0 and let the normal progress endpoint recreate its runtime snapshot.
    const active = await this.database.query<{ domain_attempt_id: string }>(
      `SELECT a.domain_attempt_id
       FROM ranked_attempts a
       JOIN ranked_rounds r ON r.id = a.round_id
       WHERE r.match_id = $1
         AND r.round_number = $2
         AND a.player_id = $3
         AND a.ended_at IS NULL
         AND a.valid = TRUE
       ORDER BY a.attempt_sequence DESC
       LIMIT 1`,
      [driver.creation.matchId, roundNumber, driver.creation.botContext.playerId],
    );
    const attemptId = active.rows[0]?.domain_attempt_id;
    if (!attemptId) return;

    const map = state.currentRound?.map as Record<string, any> | undefined;
    const nowMs = this.clock.now().getTime();
    const plan = this.planAttempt(driver, map ?? {}, Number(map?.qualifyingPercent ?? 50));
    driver.attemptId = attemptId;
    driver.attemptProgress = 0;
    driver.attemptTarget = plan.targetPercent;
    driver.attemptStartedAtMs = nowMs;
    driver.attemptDurationMs = plan.durationMs;
    driver.attemptRestartDelayMs = plan.restartDelayMs;
    driver.nextAttemptAtMs = 0;
  }

  private async simulateBotAttempt(
    driver: BotDriver,
    state: Record<string, any>,
    deathmatch: boolean,
  ): Promise<void> {
    const configuration = this.configuration;
    if (!configuration) return;
    if (
      state.state === "LAST_ATTEMPT_WINDOW" &&
      state.currentRound?.lastAttemptWindow?.targetSide !== "B"
    ) return;
    const map = deathmatch ? state.deathmatch?.map : state.currentRound?.map;
    const levelId = String(map?.playableLevelId ?? "");
    if (!levelId) return;
    const nowMs = this.clock.now().getTime();
    if (!driver.attemptId) {
      // ROUND_SETTLING never grants a new start. It exists only to let an
      // attempt accepted before the deadline finish naturally.
      if (state.state === "ROUND_SETTLING") return;
      if (nowMs < driver.nextAttemptAtMs) return;
      const started = await this.matches.startAttempt(
        driver.creation.matchId,
        driver.creation.botMatchToken,
        driver.creation.botContext,
        { levelId, clientEventId: `debug-bot-auto-${++driver.eventSequence}-start` },
      );
      if (!started.accepted || !started.attemptId) return;
      const plan = this.planAttempt(driver, map, Number(map.qualifyingPercent));
      driver.attemptId = started.attemptId;
      driver.attemptProgress = 0;
      driver.attemptTarget = plan.targetPercent;
      driver.attemptStartedAtMs = nowMs;
      driver.attemptDurationMs = plan.durationMs;
      driver.attemptRestartDelayMs = plan.restartDelayMs;
      return;
    }

    const elapsedMs = Math.max(0, nowMs - driver.attemptStartedAtMs);
    const progressRatio = driver.attemptDurationMs <= 0
      ? 1
      : Math.min(1, elapsedMs / driver.attemptDurationMs);
    const projectedProgress = Math.floor(driver.attemptTarget * progressRatio);
    const nextProgress = Math.min(
      driver.attemptTarget,
      Math.max(driver.attemptProgress, elapsedMs > 0 ? Math.max(1, projectedProgress) : 0),
    );
    if (nextProgress !== driver.attemptProgress) {
      await this.matches.updateAttemptProgress(
        driver.creation.matchId,
        driver.creation.botMatchToken,
        driver.creation.botContext,
        { levelId, attemptId: driver.attemptId, progressPercent: nextProgress },
      );
    }
    driver.attemptProgress = nextProgress;
    if (driver.attemptProgress < driver.attemptTarget) return;
    await this.matches.endAttempt(
      driver.creation.matchId,
      driver.creation.botMatchToken,
      driver.creation.botContext,
      {
        levelId,
        attemptId: driver.attemptId,
        clientEventId: `debug-bot-auto-${++driver.eventSequence}-end`,
        progressPercent: driver.attemptTarget,
        cleared: driver.attemptTarget === 100,
        nativeDeath: driver.attemptTarget !== 100,
      },
    );
    driver.attemptId = null;
    driver.attemptProgress = 0;
    driver.attemptStartedAtMs = 0;
    driver.attemptDurationMs = 0;
    driver.nextAttemptAtMs = nowMs + Math.max(
      configuration.attemptDelayMs,
      driver.attemptRestartDelayMs,
    );
    driver.attemptRestartDelayMs = 0;
  }

  private planAttempt(
    driver: BotDriver,
    stateMap: Record<string, any>,
    qualifyingPercent: number,
  ) {
    const configuration = this.configuration!;
    const difficulty = configuration.difficulties[driver.creation.settings.difficulty];
    const snapshot = this.config.getSnapshot();
    const canonicalLevelId = String(stateMap.canonicalLevelId ?? "");
    const configuredMap = snapshot.maps.find(
      (candidate) => candidate.canonicalLevelId === canonicalLevelId,
    );
    return createDebugBotAttemptPlan({
      qualifyingPercent,
      botHiddenMmr: driver.creation.botContext.hiddenMmr,
      map: {
        difficulty: configuredMap?.difficulty ?? stateMap.difficulty,
        length: configuredMap?.length,
      },
      profile: difficulty,
      syntheticScenario: driver.creation.settings.scenario !== "NORMAL_MATCH",
    }, this.random);
  }
}
