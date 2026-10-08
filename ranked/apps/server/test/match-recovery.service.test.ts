import { describe, expect, it } from "vitest";
import type { ServerClock } from "../src/common/runtime.module.js";
import type { DatabasePort, SqlExecutor, SqlResult } from "../src/database/database.port.js";
import { MatchRecoveryService } from "../src/match/match-recovery.service.js";
import { configDocumentFixture } from "./fixtures.js";

class FixedClock implements ServerClock {
  public now(): Date {
    return new Date("2026-08-20T13:00:00.000Z");
  }
}

describe("match restart recovery", () => {
  it("applies each match snapshot policy instead of the current global config", async () => {
    const cancelledBase = configDocumentFixture("cancelled-snapshot");
    const resumedBase = configDocumentFixture("resumed-snapshot");
    const cancelledSnapshot = {
      ...cancelledBase,
      operational: {
        ...cancelledBase.operational,
        failurePolicy: {
          ...cancelledBase.operational.failurePolicy!,
          restartRecoveryAction: "CANCEL_MATCH" as const,
        },
      },
    };
    const resumedSnapshot = {
      ...resumedBase,
      operational: {
        ...resumedBase.operational,
        failurePolicy: {
          ...resumedBase.operational.failurePolicy!,
          restartRecoveryAction: "RESUME" as const,
        },
      },
    };
    const cancelledIds: string[] = [];

    const executor: SqlExecutor = {
      query: async <Row>(sql: string, values: readonly unknown[] = []): Promise<SqlResult<Row>> => {
        if (sql.includes("SELECT match.id")) {
          return {
            rows: [
              { id: "cancel-me", source_payload: cancelledSnapshot },
              { id: "resume-me", source_payload: resumedSnapshot },
            ] as Row[],
            rowCount: 2,
          };
        }
        if (sql.includes("SERVER_RESTART_RECOVERY")) {
          cancelledIds.push(String(values[0]));
          return { rows: [], rowCount: 1 };
        }
        throw new Error(`Unexpected SQL: ${sql}`);
      },
    };
    const database: DatabasePort = {
      ...executor,
      transaction: async (operation) => operation(executor),
      ping: async () => true,
    };
    const service = new MatchRecoveryService(database, new FixedClock());

    await expect(service.recover()).resolves.toEqual({ cancelled: 1, resumed: 1 });
    expect(cancelledIds).toEqual(["cancel-me"]);
  });
});
