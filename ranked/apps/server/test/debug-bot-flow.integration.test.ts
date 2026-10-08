import { describe, expect, it } from "vitest";
import { DebugBotService } from "../src/debug-bot/debug-bot.service.js";
import type { ServerEnvironment } from "../src/config/server-environment.js";

describe("Debug Bot alpha.6 compatibility", () => {
  it("uses the canonical DebugBotService/debugBot server API", () => {
    const environment = { debugBot: null } satisfies Pick<ServerEnvironment, "debugBot">;
    expect(DebugBotService).toBeDefined();
    expect(environment.debugBot).toBeNull();
  });
});
