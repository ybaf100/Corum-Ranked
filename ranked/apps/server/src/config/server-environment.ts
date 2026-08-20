import { RankedDomainError } from "@corum-ranked/rules";

export const SERVER_ENVIRONMENT = Symbol("SERVER_ENVIRONMENT");

export interface ServerEnvironment {
  readonly nodeEnv: "development" | "test" | "production";
  readonly port: number;
  readonly databaseUrl: string;
  readonly rankedConfigUrl: string;
  readonly rankedConfigRefreshMs: number;
  readonly rankedConfigFetchTimeoutMs: number;
  readonly sessionTokenSecret: string;
  readonly corsOrigins: readonly string[];
  readonly discordRelay: DiscordRelayEnvironment | null;
  readonly debugBotMatch: DebugBotEnvironment | null;
}

export interface DebugBotEnvironment {
  readonly password: string;
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

const enabledFlag = (environment: NodeJS.ProcessEnv, key: string): boolean => {
  const value = environment[key]?.trim().toLowerCase() || "false";
  if (value === "true" || value === "1") return true;
  if (value === "false" || value === "0") return false;
  throw new RankedDomainError("INVALID_CONFIG", `${key} must be true or false`);
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

  const debugBotEnabled = enabledFlag(environment, "ENABLE_DEBUG_BOT_MATCH");
  if (nodeEnv === "production" && debugBotEnabled) {
    throw new RankedDomainError(
      "INVALID_CONFIG",
      "ENABLE_DEBUG_BOT_MATCH cannot be enabled in production",
    );
  }
  const debugBotMatch: DebugBotEnvironment | null = debugBotEnabled
    ? { password: requireValue(environment, "DEBUG_BOT_PASSWORD") }
    : null;

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
    nodeEnv,
    port,
    databaseUrl,
    rankedConfigUrl,
    rankedConfigRefreshMs,
    rankedConfigFetchTimeoutMs,
    sessionTokenSecret,
    corsOrigins,
    discordRelay,
    debugBotMatch,
  };
};
