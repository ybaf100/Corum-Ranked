import { describe, expect, it, vi } from "vitest";
import type { ServerClock } from "../src/common/runtime.module.js";
import type { ServerEnvironment } from "../src/config/server-environment.js";
import { DebugBotService } from "../src/debug-bot/debug-bot.service.js";

describe("Debug Bot Death Match live progress", () => {
  it("publishes intermediate Death Match progress before the authoritative End", async () => {
    const nowMs = Date.parse("2026-09-13T00:00:01.000Z");
    const updateAttemptProgress = vi.fn().mockResolvedValue({ accepted: true });
    const endAttempt = vi.fn().mockResolvedValue({ accepted: true });
    const matches = { updateAttemptProgress, endAttempt };
    const service = new DebugBotService(
      {
        debugBot: {
          password: "test",
          tickMs: 125,
          attemptDelayMs: 200,
          difficulties: {
            EASY: { mmrOffset: -250, qualifyingChance: 0.3, clearChance: 0, paceMultiplier: 0.96 },
            NORMAL: { mmrOffset: 0, qualifyingChance: 0.6, clearChance: 0.1, paceMultiplier: 1 },
            HARD: { mmrOffset: 250, qualifyingChance: 0.8, clearChance: 0.2, paceMultiplier: 1.05 },
          },
        },
      } as unknown as ServerEnvironment,
      {} as any,
      { now: () => new Date(nowMs) } as ServerClock,
      { next: () => 0.5 } as any,
      {} as any,
      matches as any,
      {} as any,
      {} as any,
    );

    const botContext = {
      sessionId: "bot-session",
      playerId: "bot-player",
      gdAccountId: "2",
      gdUsername: "BOT NORMAL",
      displayedTier: "BRONZE",
      hiddenMmr: 2_000,
      placementGames: 5,
    };
    const driver = {
      creation: {
        matchId: "match-1",
        playerMatchToken: "player-token",
        botMatchToken: "bot-token",
        playerContext: botContext,
        botContext,
        settings: {
          difficulty: "NORMAL",
          scenario: "NORMAL_MATCH",
          botBan: "NO_BAN",
          sendDiscordEvents: false,
          mmrOffset: 0,
        },
      },
      attemptId: "attempt-1",
      attemptProgress: 0,
      attemptTarget: 80,
      attemptStartedAtMs: nowMs - 500,
      attemptDurationMs: 1_000,
      attemptRestartDelayMs: 0,
      nextAttemptAtMs: 0,
      eventSequence: 0,
      banSubmitted: false,
      seededRounds: new Set<number>(),
      busy: false,
    };

    await (service as any).simulateBotAttempt(
      driver,
      {
        state: "DEATHMATCH_PLAYING",
        deathmatch: { map: { playableLevelId: "12345" } },
      },
      true,
    );

    expect(updateAttemptProgress).toHaveBeenCalledTimes(1);
    expect(updateAttemptProgress).toHaveBeenCalledWith(
      "match-1",
      "bot-token",
      botContext,
      {
        levelId: "12345",
        attemptId: "attempt-1",
        progressPercent: 40,
      },
    );
    expect(driver.attemptProgress).toBe(40);
    expect(endAttempt).not.toHaveBeenCalled();
  });
});
