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

interface BotDriver {
  readonly creation: DebugBotMatchCreation;
  attemptId: string | null;
  attemptProgress: number;
  attemptTarget: number;
  attemptLastTickMs: number;
  nextAttemptAtMs: number;
  eventSequence: number;
  banSubmitted: boolean;
  seededRounds: Set<number>;
  busy: boolean;
}

interface ActiveDebugMatchRow {
  id: string;
  debug_bot_config: unknown;
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

  public onModuleInit(): void {
    if (!this.configuration) return;
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
    this.drivers.set(creation.matchId, this.newDriver(creation));
    await this.drive(creation.matchId);
    return {
      debug: true,
      matchType: "DEBUG_BOT",
      matchId: creation.matchId,
      matchToken: creation.playerMatchToken,
      side: "A",
      opponent: {
        name: creation.botContext.gdUsername,
        rating: creation.botContext.hiddenMmr,
        difficulty: settings.difficulty,
      },
      scenario: settings.scenario,
      serverNow: this.clock.now().toISOString(),
    };
  }

  public async tickOnce(): Promise<void> {
    if (!this.configuration) return;
    await this.hydrateDrivers();
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
      attemptProgress: 0,
      attemptTarget: 0,
      attemptLastTickMs: 0,
      nextAttemptAtMs: 0,
      eventSequence: 0,
      banSubmitted: false,
      seededRounds: new Set<number>(),
      busy: false,
    };
  }

  private async hydrateDrivers(): Promise<void> {
    const result = await this.database.query<ActiveDebugMatchRow>(
      `SELECT m.id, m.debug_bot_config, m.player_a_id, m.player_b_id,
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
    for (const row of result.rows) {
      if (this.drivers.has(row.id)) continue;
      const stored = parseObject(row.debug_bot_config);
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
      const state = await this.matches.state(
        matchId,
        driver.creation.botMatchToken,
        driver.creation.botContext,
      ) as Record<string, any>;
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
      if (["ROUND_PLAYING", "FINAL_ATTEMPT_WINDOW", "LAST_ATTEMPT_WINDOW"].includes(state.state)) {
        const round = state.currentRound as Record<string, any> | null;
        if (!round) return;
        await this.seedScenarioRound(driver, state, round);
        const refreshed = await this.matches.state(
          matchId,
          driver.creation.botMatchToken,
          driver.creation.botContext,
        ) as Record<string, any>;
        if (["ROUND_PLAYING", "FINAL_ATTEMPT_WINDOW", "LAST_ATTEMPT_WINDOW"].includes(refreshed.state)) {
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
    return scenario === "TRIGGER_ROUND_3" || scenario === "TRIGGER_DEATHMATCH";
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
    const bot = () => this.completeAttempt(driver, false, levelId, 100, true, "scenario-bot");
    const player = () => this.completeAttempt(driver, true, levelId, 100, true, "scenario-player");

    if (scenario === "FORCE_BOT_1_CLEAR" && roundNumber === 1) {
      await bot();
    } else if (scenario === "FORCE_BOT_2_CLEARS" && roundNumber === 1) {
      await bot();
      await bot();
    } else if (scenario === "TRIGGER_LAST_ATTEMPT" && roundNumber === 1) {
      await bot();
      await player();
      await player();
    } else if (scenario === "TRIGGER_ROUND_DRAW") {
      await bot();
      await player();
      await player();
      await bot();
    } else if (scenario === "TRIGGER_ROUND_3") {
      if (roundNumber === 1) {
        await player();
        await player();
      } else if (roundNumber === 2) {
        await bot();
        await bot();
      }
    } else if (scenario === "TRIGGER_DEATHMATCH") {
      await bot();
      await player();
      await player();
      await bot();
    }
    driver.seededRounds.add(roundNumber);
    if (state.state !== "ROUND_PLAYING") driver.attemptId = null;
  }

  private async completeAttempt(
    driver: BotDriver,
    player: boolean,
    levelId: string,
    progressPercent: number,
    cleared: boolean,
    kind: string,
  ): Promise<void> {
    const creation = driver.creation;
    const context = player ? creation.playerContext : creation.botContext;
    const token = player ? creation.playerMatchToken : creation.botMatchToken;
    const sequence = ++driver.eventSequence;
    const started = await this.matches.startAttempt(creation.matchId, token, context, {
      levelId,
      clientEventId: `debug-bot-${kind}-${sequence}-start`,
    });
    if (!started.accepted || !started.attemptId) return;
    await this.matches.endAttempt(creation.matchId, token, context, {
      levelId,
      attemptId: started.attemptId,
      clientEventId: `debug-bot-${kind}-${sequence}-end`,
      progressPercent,
      cleared,
    });
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
      if (nowMs < driver.nextAttemptAtMs) return;
      const started = await this.matches.startAttempt(
        driver.creation.matchId,
        driver.creation.botMatchToken,
        driver.creation.botContext,
        { levelId, clientEventId: `debug-bot-auto-${++driver.eventSequence}-start` },
      );
      if (!started.accepted || !started.attemptId) return;
      driver.attemptId = started.attemptId;
      driver.attemptProgress = 0;
      driver.attemptTarget = this.chooseAttemptTarget(
        Number(map.qualifyingPercent),
        driver.creation.settings.difficulty,
      );
      driver.attemptLastTickMs = nowMs;
      return;
    }

    const difficulty = configuration.difficulties[driver.creation.settings.difficulty];
    const elapsedSeconds = Math.max(configuration.tickMs, nowMs - driver.attemptLastTickMs) / 1_000;
    const nextProgress = Math.min(
      driver.attemptTarget,
      Math.max(driver.attemptProgress + 1, Math.floor(driver.attemptProgress + difficulty.progressPerSecond * elapsedSeconds)),
    );
    driver.attemptLastTickMs = nowMs;
    if (!deathmatch && nextProgress !== driver.attemptProgress) {
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
      },
    );
    driver.attemptId = null;
    driver.attemptProgress = 0;
    driver.nextAttemptAtMs = nowMs + configuration.attemptDelayMs;
  }

  private chooseAttemptTarget(
    qualifyingPercent: number,
    difficultyName: DebugBotMatchSettings["difficulty"],
  ): number {
    const configuration = this.configuration!;
    const difficulty = configuration.difficulties[difficultyName];
    const outcome = this.random.next();
    if (outcome < difficulty.clearChance) return 100;
    if (outcome < difficulty.qualifyingChance) {
      return Math.max(
        Math.ceil(qualifyingPercent),
        Math.min(99, Math.floor(qualifyingPercent + this.random.next() * (100 - qualifyingPercent))),
      );
    }
    return Math.max(1, Math.min(99, Math.floor(this.random.next() * qualifyingPercent)));
  }
}
