import { describe, expect, it } from "vitest";
import { loadServerEnvironment } from "../src/config/server-environment.js";

const validEnvironment = (): NodeJS.ProcessEnv => ({
  NODE_ENV: "test",
  PORT: "3000",
  DATABASE_URL: "postgresql://ranked:test@localhost:5432/ranked",
  RANKED_CONFIG_URL: "http://localhost/config?action=ranked_config",
  RANKED_CONFIG_REFRESH_MS: "60000",
  RANKED_CONFIG_FETCH_TIMEOUT_MS: "5000",
  RANKED_SESSION_TOKEN_SECRET: "test-only",
});

describe("server environment", () => {
  it("loads explicitly provided non-secret and secret settings", () => {
    expect(loadServerEnvironment(validEnvironment())).toMatchObject({
      port: 3000,
      rankedConfigRefreshMs: 60_000,
      rankedConfigFetchTimeoutMs: 5_000,
      rankedCsmpFetchTimeoutMs: 30_000,
      discordRelay: null,
      debugBot: null,
    });
  });

  it("allows the CSMP source timeout to be tuned independently", () => {
    expect(loadServerEnvironment({
      ...validEnvironment(),
      RANKED_CSMP_FETCH_TIMEOUT_MS: "45000",
    }).rankedCsmpFetchTimeoutMs).toBe(45_000);
  });

  it("enables the development bot only with an explicit body-password configuration", () => {
    const loaded = loadServerEnvironment({
      ...validEnvironment(),
      ENABLE_DEBUG_BOT_MATCH: "true",
      DEBUG_BOT_PASSWORD: "2008",
    });
    expect(loaded.debugBot).toMatchObject({
      password: "2008",
      tickMs: 125,
      attemptDelayMs: 1_200,
      difficulties: {
        EASY: { mmrOffset: -250, qualifyingChance: 0.22, clearChance: 0.01, progressPerSecond: 11 },
        NORMAL: { mmrOffset: 0, qualifyingChance: 0.45, clearChance: 0.04, progressPerSecond: 16 },
        HARD: { mmrOffset: 250, qualifyingChance: 0.68, clearChance: 0.10, progressPerSecond: 22 },
      },
    });
  });

  it("fails closed when the bot flag is enabled without a password", () => {
    expect(() => loadServerEnvironment({
      ...validEnvironment(),
      ENABLE_DEBUG_BOT_MATCH: "true",
    })).toThrow("DEBUG_BOT_PASSWORD must be explicitly configured");
  });

  it("rejects a Bot clear probability above its qualifying probability", () => {
    expect(() => loadServerEnvironment({
      ...validEnvironment(),
      ENABLE_DEBUG_BOT_MATCH: "true",
      DEBUG_BOT_PASSWORD: "2008",
      DEBUG_BOT_EASY_QUALIFYING_CHANCE: "0.1",
      DEBUG_BOT_EASY_CLEAR_CHANCE: "0.2",
    })).toThrow("CLEAR_CHANCE cannot exceed");
  });

  it("loads Discord relay controls only when a webhook is configured", () => {
    expect(
      loadServerEnvironment({
        ...validEnvironment(),
        DISCORD_WEBHOOK_URL: "https://discord.com/api/webhooks/123/test-token",
        DISCORD_OUTBOX_POLL_MS: "1000",
        DISCORD_OUTBOX_BATCH_SIZE: "8",
        DISCORD_REQUEST_TIMEOUT_MS: "3000",
        DISCORD_OUTBOX_LEASE_MS: "10000",
        DISCORD_RETRY_BASE_MS: "5000",
        DISCORD_RETRY_MAXIMUM_MS: "60000",
        DISCORD_MAXIMUM_ATTEMPTS: "5",
      }).discordRelay,
    ).toMatchObject({
      pollMs: 1_000,
      batchSize: 8,
      maximumAttempts: 5,
    });
  });

  it("rejects non-Discord webhook destinations", () => {
    expect(() =>
      loadServerEnvironment({
        ...validEnvironment(),
        DISCORD_WEBHOOK_URL: "https://attacker.example/api/webhooks/123/token",
      }),
    ).toThrow("official HTTPS Discord webhook URL");
  });

  it("does not silently invent unresolved timing configuration", () => {
    const environment = validEnvironment();
    delete environment.RANKED_CONFIG_REFRESH_MS;
    expect(() => loadServerEnvironment(environment)).toThrow(
      "RANKED_CONFIG_REFRESH_MS must be explicitly configured",
    );
  });

  it("rejects placeholder secrets and non-HTTPS production config sources", () => {
    expect(() =>
      loadServerEnvironment({
        ...validEnvironment(),
        RANKED_SESSION_TOKEN_SECRET: "<set-in-deployment>",
      }),
    ).toThrow("RANKED_SESSION_TOKEN_SECRET must be explicitly configured");
    expect(() =>
      loadServerEnvironment({
        ...validEnvironment(),
        NODE_ENV: "production",
      }),
    ).toThrow("RANKED_CONFIG_URL must use HTTPS");
  });
});
