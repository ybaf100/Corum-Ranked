import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, beforeEach, describe, expect, it } from "vitest";
import type { ServerClock } from "../src/common/runtime.module.js";
import type { DiscordRelayEnvironment, ServerEnvironment } from "../src/config/server-environment.js";
import { DiscordRelayWorker } from "../src/relay/discord-relay.worker.js";
import type { DiscordTransport } from "../src/relay/discord-transport.js";
import { environmentFixture } from "./fixtures.js";
import { PgliteDatabase } from "./pglite-database.js";

let pglite: PGlite;
let database: PgliteDatabase;

const relayConfig = (): DiscordRelayEnvironment => ({
  webhookUrl: "https://discord.com/api/webhooks/123/test-token",
  pollMs: 1_000,
  batchSize: 5,
  requestTimeoutMs: 1_000,
  leaseMs: 10_000,
  retryBaseMs: 5_000,
  retryMaximumMs: 60_000,
  maximumAttempts: 1,
});

class FixedClock implements ServerClock {
  public now(): Date {
    return new Date("2026-08-20T12:00:00.000Z");
  }
}

class RecordingTransport implements DiscordTransport {
  public readonly messages: string[] = [];
  public shouldFail = false;

  public async send(_url: string, content: string, _timeoutMs: number): Promise<void> {
    this.messages.push(content);
    if (this.shouldFail) throw new Error("simulated Discord outage");
  }
}

const insertEvent = async (id: string): Promise<void> => {
  await database.query(
    `INSERT INTO ranked_outbox_events (
       id, aggregate_type, aggregate_id, event_type, deduplication_key,
       payload, available_at
     ) VALUES ($1, 'RANKED_MATCH', $2, 'ROUND_START', $3, $4::jsonb, $5)`,
    [
      id,
      "00000000-0000-4000-8000-000000000099",
      `event:${id}`,
      JSON.stringify({
        matchId: "test-match",
        players: { A: "Alpha", B: "Beta" },
        roundNumber: 1,
        banner: "NONE",
        mapTitle: "Test Map",
        qualifyingPercent: 50,
      }),
      "2026-08-20T11:59:00.000Z",
    ],
  );
};

beforeAll(async () => {
  pglite = new PGlite();
  database = new PgliteDatabase(pglite);
  const migrationPath = fileURLToPath(
    new URL("../../../migrations/0001_initial_ranked.sql", import.meta.url),
  );
  await pglite.exec(await readFile(migrationPath, "utf8"));
}, 60_000);

beforeEach(async () => {
  await database.query("DELETE FROM ranked_outbox_events");
});

afterAll(async () => {
  await pglite.close();
});

describe("Discord relay outbox", () => {
  it("marks successfully delivered events without exposing the webhook", async () => {
    const id = "00000000-0000-4000-8000-000000000001";
    await insertEvent(id);
    const transport = new RecordingTransport();
    const environment: ServerEnvironment = { ...environmentFixture(), discordRelay: relayConfig() };
    const worker = new DiscordRelayWorker(database, new FixedClock(), environment, transport);

    await worker.deliverOnce();

    const row = await database.query<{ delivered_at: string | null; last_error: string | null }>(
      "SELECT delivered_at, last_error FROM ranked_outbox_events WHERE id = $1",
      [id],
    );
    expect(row.rows[0]?.delivered_at).not.toBeNull();
    expect(row.rows[0]?.last_error).toBeNull();
    expect(transport.messages[0]).toContain("Round 1 Start");
    expect(transport.messages[0]).not.toContain("test-token");
  });

  it("abandons a final failed delivery without throwing into game flow", async () => {
    const id = "00000000-0000-4000-8000-000000000002";
    await insertEvent(id);
    const transport = new RecordingTransport();
    transport.shouldFail = true;
    const environment: ServerEnvironment = { ...environmentFixture(), discordRelay: relayConfig() };
    const worker = new DiscordRelayWorker(database, new FixedClock(), environment, transport);

    await expect(worker.deliverOnce()).resolves.toBeUndefined();

    const row = await database.query<{
      abandoned_at: string | null;
      delivered_at: string | null;
      last_error: string | null;
    }>("SELECT abandoned_at, delivered_at, last_error FROM ranked_outbox_events WHERE id = $1", [id]);
    expect(row.rows[0]?.delivered_at).toBeNull();
    expect(row.rows[0]?.abandoned_at).not.toBeNull();
    expect(row.rows[0]?.last_error).toContain("simulated Discord outage");
  });
});
