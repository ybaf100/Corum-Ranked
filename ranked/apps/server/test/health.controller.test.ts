import { describe, expect, it } from "vitest";
import type { RankedConfigService } from "../src/config/ranked-config.service.js";
import type { DatabaseService } from "../src/database/database.service.js";
import { HealthController } from "../src/health.controller.js";

describe("HealthController", () => {
  it("keeps public database reads ready while Ranked matchmaking is closed", async () => {
    const database = {
      ping: async () => true,
      schemaStatus: async () => ({ ready: true, missing: [] }),
    } as unknown as DatabaseService;
    const config = {
      getStatus: () => ({
        ready: false,
        queueEnabled: false,
        generation: null,
        fetchedAt: null,
        lastAttemptAt: null,
        lastError: "config unavailable",
      }),
    } as RankedConfigService;

    const response = await new HealthController(database, config).ready();

    expect(response).toMatchObject({
      ready: true,
      publicDataReady: true,
      rankedReady: false,
      databaseReady: true,
      config: { ready: false, queueEnabled: false },
    });
  });
});
