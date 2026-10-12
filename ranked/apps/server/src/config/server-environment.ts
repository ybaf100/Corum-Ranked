import { RankedDomainError } from "@corum-ranked/rules";
import { DEBUG_BOT_DIFFICULTY_PROFILES } from "../debug-bot/debug-bot.config.js";
import { normalizeRankedDownloadBaseUrl,parseRankedModUpdates, type RankedModUpdates } from "./ranked-mod-updates.js";

export const SERVER_ENVIRONMENT = Symbol("SERVER_ENVIRONMENT");

export interface ServerEnvironment {
  readonly rankedModUpdates?: RankedModUpdates;
  readonly rankedDownloadBaseUrl?: string;
  readonly nodeEnv: "development" | "test" | "production";
  readonly port: number;
  readonly databaseUrl: string;
  readonly rankedConfigUrl: string;
  readonly rankedConfigRefreshMs: number;
  readonly rankedConfigFetchTimeoutMs: number;
  readonly rankedCsmpFetchTimeoutMs: number;
  readonly sessionTokenSecret: string;
  readonly corsOrigins: readonly string[];
  readonly discordRelay: DiscordRelayEnvironment | null;
  readonly debugBot: DebugBotEnvironment | null;
  readonly debugSpectate: { readonly password: string } | null;
}

export interface DebugBotDifficultyEnvironment {
  readonly mmrOffset: number;
  /** Base chance to reach qualifying or better before map/MMR/length adjustment. */
  readonly qualifyingChance: number;
  /** Base chance to clear before map/MMR/length adjustment. */
  readonly clearChance: number;
  /** Real-time pace multiplier. 1.0 roughly follows the map's estimated native duration. */
  readonly paceMultiplier: number;
}

export interface DebugBotEnvironment {
  readonly password: string;
  readonly tickMs: number;
  readonly attemptDelayMs: number;
  readonly difficulties: Readonly<Record<"EASY" | "NORMAL" | "HARD", DebugBotDifficultyEnvironment>>;
}

export interface DiscordRelayEnvironment {
  readonly webhookUrl: string;
  readonly pollMs: number;
  readonly batchSize: number;
  readonly requestTimeoutMs: number;
  readonly leaseMs: number;
  readonly retryBaseMs: number;
  readonly retryMaximumMs: number;
  readonly maximumAttempts: number;
}

const requireValue = (environment: NodeJS.ProcessEnv, key: string): string => {
  const value = environment[key]?.trim();
  if (!value || /^<.+>$/.test(value)) {
    throw new RankedDomainError("INVALID_CONFIG", `${key} must be explicitly configured`);
  }
  return value;
};

const positiveInteger = (value: string, key: string): number => {
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed) || parsed <= 0) {
    throw new RankedDomainError("INVALID_CONFIG", `${key} must be a positive integer`);
  }
  return parsed;
};

const finiteNumber = (value: string | undefined, fallback: number, key: string): number => {
  const parsed = value?.trim() ? Number(value) : fallback;
  if (!Number.isFinite(parsed)) {
    throw new RankedDomainError("INVALID_CONFIG", `${key} must be a finite number`);
  }
  return parsed;
};

const probability = (value: string | undefined, fallback: number, key: string): number => {
  const parsed = finiteNumber(value, fallback, key);
  if (parsed < 0 || parsed > 1) {
    throw new RankedDomainError("INVALID_CONFIG", `${key} must be from 0 through 1`);
  }
  return parsed;
};

export const loadServerEnvironment = (
  environment: NodeJS.ProcessEnv = process.env,
): ServerEnvironment => {
  const nodeEnvText = environment.NODE_ENV?.trim() || "development";
  if (!(["development", "test", "production"] as const).includes(nodeEnvText as never)) {
    throw new RankedDomainError("INVALID_CONFIG", "NODE_ENV must be development, test, or production");
  }
  const nodeEnv = nodeEnvText as ServerEnvironment["nodeEnv"];
  const databaseUrl = requireValue(environment, "DATABASE_URL");
  const rankedConfigUrl = requireValue(environment, "RANKED_CONFIG_URL");
  const sessionTokenSecret = requireValue(environment, "RANKED_SESSION_TOKEN_SECRET");
  const port = positiveInteger(environment.PORT?.trim() || "3000", "PORT");
  const rankedConfigRefreshMs = positiveInteger(
    requireValue(environment, "RANKED_CONFIG_REFRESH_MS"),
    "RANKED_CONFIG_REFRESH_MS",
  );
  const rankedConfigFetchTimeoutMs = positiveInteger(
    requireValue(environment, "RANKED_CONFIG_FETCH_TIMEOUT_MS"),
    "RANKED_CONFIG_FETCH_TIMEOUT_MS",
  );
  const rankedCsmpFetchTimeoutMs = positiveInteger(
    environment.RANKED_CSMP_FETCH_TIMEOUT_MS?.trim() || "30000",
    "RANKED_CSMP_FETCH_TIMEOUT_MS",
  );

  const databaseProtocol = new URL(databaseUrl).protocol;
  if (databaseProtocol !== "postgres:" && databaseProtocol !== "postgresql:") {
    throw new RankedDomainError("INVALID_CONFIG", "DATABASE_URL must use PostgreSQL");
  }
  const configProtocol = new URL(rankedConfigUrl).protocol;
  if (configProtocol !== "https:" && !(nodeEnv !== "production" && configProtocol === "http:")) {
    throw new RankedDomainError(
      "INVALID_CONFIG",
      "RANKED_CONFIG_URL must use HTTPS outside local development/test",
    );
  }
  if (nodeEnv === "production" && sessionTokenSecret.length < 32) {
    throw new RankedDomainError(
      "INVALID_CONFIG",
      "RANKED_SESSION_TOKEN_SECRET must be at least 32 characters in production",
    );
  }

  const corsOrigins = (environment.CORS_ORIGINS || "")
    .split(",")
    .map((origin) => origin.trim())
    .filter(Boolean);

  let debugBot: DebugBotEnvironment | null = null;
  if (environment.ENABLE_DEBUG_BOT_MATCH?.trim().toLowerCase() === "true") {
    const password = requireValue(environment, "DEBUG_BOT_PASSWORD");
    const difficulty = (
      name: "EASY" | "NORMAL" | "HARD",
      offset: number,
      qualifyingChance: number,
      clearChance: number,
      paceMultiplier: number,
    ): DebugBotDifficultyEnvironment => {
      const resolvedQualifyingChance = probability(
        environment[`DEBUG_BOT_${name}_QUALIFYING_CHANCE`],
        qualifyingChance,
        `DEBUG_BOT_${name}_QUALIFYING_CHANCE`,
      );
      const resolvedClearChance = probability(
        environment[`DEBUG_BOT_${name}_CLEAR_CHANCE`],
        clearChance,
        `DEBUG_BOT_${name}_CLEAR_CHANCE`,
      );
      if (resolvedClearChance > resolvedQualifyingChance) {
        throw new RankedDomainError(
          "INVALID_CONFIG",
          `DEBUG_BOT_${name}_CLEAR_CHANCE cannot exceed DEBUG_BOT_${name}_QUALIFYING_CHANCE`,
        );
      }
      const resolvedPaceMultiplier = finiteNumber(
        environment[`DEBUG_BOT_${name}_PACE_MULTIPLIER`],
        paceMultiplier,
        `DEBUG_BOT_${name}_PACE_MULTIPLIER`,
      );
      if (resolvedPaceMultiplier <= 0 || resolvedPaceMultiplier > 3) {
        throw new RankedDomainError(
          "INVALID_CONFIG",
          `DEBUG_BOT_${name}_PACE_MULTIPLIER must be greater than 0 and at most 3`,
        );
      }
      return {
        mmrOffset: finiteNumber(environment[`DEBUG_BOT_${name}_MMR_OFFSET`], offset, `DEBUG_BOT_${name}_MMR_OFFSET`),
        qualifyingChance: resolvedQualifyingChance,
        clearChance: resolvedClearChance,
        paceMultiplier: resolvedPaceMultiplier,
      };
    };
    debugBot = {
      password,
      tickMs: positiveInteger(environment.DEBUG_BOT_TICK_MS?.trim() || "125", "DEBUG_BOT_TICK_MS"),
      attemptDelayMs: positiveInteger(
        environment.DEBUG_BOT_ATTEMPT_DELAY_MS?.trim() || "1200",
        "DEBUG_BOT_ATTEMPT_DELAY_MS",
      ),
      difficulties: {
        EASY: difficulty(
          "EASY",
          DEBUG_BOT_DIFFICULTY_PROFILES.EASY.ratingOffset,
          DEBUG_BOT_DIFFICULTY_PROFILES.EASY.qualifyingReachProbability,
          DEBUG_BOT_DIFFICULTY_PROFILES.EASY.clearProbability,
          DEBUG_BOT_DIFFICULTY_PROFILES.EASY.paceMultiplier,
        ),
        NORMAL: difficulty(
          "NORMAL",
          DEBUG_BOT_DIFFICULTY_PROFILES.NORMAL.ratingOffset,
          DEBUG_BOT_DIFFICULTY_PROFILES.NORMAL.qualifyingReachProbability,
          DEBUG_BOT_DIFFICULTY_PROFILES.NORMAL.clearProbability,
          DEBUG_BOT_DIFFICULTY_PROFILES.NORMAL.paceMultiplier,
        ),
        HARD: difficulty(
          "HARD",
          DEBUG_BOT_DIFFICULTY_PROFILES.HARD.ratingOffset,
          DEBUG_BOT_DIFFICULTY_PROFILES.HARD.qualifyingReachProbability,
          DEBUG_BOT_DIFFICULTY_PROFILES.HARD.clearProbability,
          DEBUG_BOT_DIFFICULTY_PROFILES.HARD.paceMultiplier,
        ),
      },
    };
  }

  const webhookText = environment.DISCORD_WEBHOOK_URL?.trim() || "";
  let discordRelay: DiscordRelayEnvironment | null = null;
  if (webhookText && !/^<.+>$/.test(webhookText)) {
    const webhookUrl = new URL(webhookText);
    const allowedDiscordHosts = new Set([
      "discord.com",
      "canary.discord.com",
      "ptb.discord.com",
      "discordapp.com",
    ]);
    if (
      webhookUrl.protocol !== "https:" ||
      !allowedDiscordHosts.has(webhookUrl.hostname) ||
      !webhookUrl.pathname.startsWith("/api/webhooks/")
    ) {
      throw new RankedDomainError(
        "INVALID_CONFIG",
        "DISCORD_WEBHOOK_URL must be an official HTTPS Discord webhook URL",
      );
    }
    const pollMs = positiveInteger(
      requireValue(environment, "DISCORD_OUTBOX_POLL_MS"),
      "DISCORD_OUTBOX_POLL_MS",
    );
    const batchSize = positiveInteger(
      requireValue(environment, "DISCORD_OUTBOX_BATCH_SIZE"),
      "DISCORD_OUTBOX_BATCH_SIZE",
    );
    const requestTimeoutMs = positiveInteger(
      requireValue(environment, "DISCORD_REQUEST_TIMEOUT_MS"),
      "DISCORD_REQUEST_TIMEOUT_MS",
    );
    const leaseMs = positiveInteger(
      requireValue(environment, "DISCORD_OUTBOX_LEASE_MS"),
      "DISCORD_OUTBOX_LEASE_MS",
    );
    const retryBaseMs = positiveInteger(
      requireValue(environment, "DISCORD_RETRY_BASE_MS"),
      "DISCORD_RETRY_BASE_MS",
    );
    const retryMaximumMs = positiveInteger(
      requireValue(environment, "DISCORD_RETRY_MAXIMUM_MS"),
      "DISCORD_RETRY_MAXIMUM_MS",
    );
    const maximumAttempts = positiveInteger(
      requireValue(environment, "DISCORD_MAXIMUM_ATTEMPTS"),
      "DISCORD_MAXIMUM_ATTEMPTS",
    );
    if (retryMaximumMs < retryBaseMs) {
      throw new RankedDomainError(
        "INVALID_CONFIG",
        "DISCORD_RETRY_MAXIMUM_MS must be greater than or equal to DISCORD_RETRY_BASE_MS",
      );
    }
    if (leaseMs <= requestTimeoutMs) {
      throw new RankedDomainError(
        "INVALID_CONFIG",
        "DISCORD_OUTBOX_LEASE_MS must be greater than DISCORD_REQUEST_TIMEOUT_MS",
      );
    }
    discordRelay = {
      webhookUrl: webhookUrl.toString(),
      pollMs,
      batchSize,
      requestTimeoutMs,
      leaseMs,
      retryBaseMs,
      retryMaximumMs,
      maximumAttempts,
    };
  }

  return {
    rankedModUpdates: parseRankedModUpdates(environment.RANKED_MOD_UPDATES_JSON),
    rankedDownloadBaseUrl: normalizeRankedDownloadBaseUrl(environment.RANKED_DOWNLOAD_BASE_URL),
    nodeEnv,
    port,
    databaseUrl,
    rankedConfigUrl,
    rankedConfigRefreshMs,
    rankedConfigFetchTimeoutMs,
    rankedCsmpFetchTimeoutMs,
    sessionTokenSecret,
    corsOrigins,
    discordRelay,
    debugBot,
    debugSpectate: environment.ENABLE_DEBUG_SPECTATE?.trim().toLowerCase() === "true"
      ? { password: requireValue(environment, "DEBUG_SPECTATE_PASSWORD") }
      : null,
  };
};
