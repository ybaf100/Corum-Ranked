import { Test } from "@nestjs/testing";
import { describe, expect, it } from "vitest";
import { AppModule } from "../src/app.module.js";
import { SERVER_ENVIRONMENT } from "../src/config/server-environment.js";
import { DATABASE, type DatabasePort } from "../src/database/database.port.js";
import { DatabaseService } from "../src/database/database.service.js";
import { MatchService } from "../src/match/match.service.js";
import {
  MATCH_RUNTIME_STATE,
  type MatchRuntimeStatePort,
} from "../src/match/match-runtime-state.js";
import { DiscordRelayWorker } from "../src/relay/discord-relay.worker.js";
import { environmentFixture } from "./fixtures.js";

const unusedDatabase: DatabasePort = {
  query: async () => ({ rows: [], rowCount: 0 }),
  transaction: async (operation) => operation(unusedDatabase),
  ping: async () => true,
};

describe("Nest application module", () => {
  it("resolves the isolated Ranked server, match service, and relay dependency graph", async () => {
    const module = await Test.createTestingModule({ imports: [AppModule] })
      .overrideProvider(SERVER_ENVIRONMENT)
      .useValue(environmentFixture())
      .overrideProvider(DatabaseService)
      .useValue(unusedDatabase)
      .overrideProvider(DATABASE)
      .useValue(unusedDatabase)
      .compile();

    expect(module.get(MatchService)).toBeInstanceOf(MatchService);
    expect(module.get<MatchRuntimeStatePort>(MATCH_RUNTIME_STATE)).toBeDefined();
    expect(module.get(DiscordRelayWorker)).toBeInstanceOf(DiscordRelayWorker);
    await module.close();
  });
});
