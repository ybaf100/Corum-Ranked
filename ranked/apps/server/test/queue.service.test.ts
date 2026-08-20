import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, beforeEach, describe, expect, it } from "vitest";
import { SeededRandom, type CsmpTier } from "@corum-ranked/rules";
import type { IdGenerator, ServerClock } from "../src/common/runtime.module.js";
import { TokenService } from "../src/common/token.service.js";
import type { CsmpTierSource } from "../src/config/csmp-tier.source.js";
import type { RankedConfigService } from "../src/config/ranked-config.service.js";
import { QueueService } from "../src/queue/queue.service.js";
import type { CreateSessionDto } from "../src/session/session.dto.js";
import { SessionService } from "../src/session/session.service.js";
import type { RankedSessionContext } from "../src/session/session.types.js";
import { configDocumentFixture, environmentFixture } from "./fixtures.js";
import { PgliteDatabase } from "./pglite-database.js";

let pglite: PGlite;
let database: PgliteDatabase;

beforeAll(async () => {
  pglite = new PGlite();
  database = new PgliteDatabase(pglite);
  const migrationPath = fileURLToPath(
    new URL("../../../migrations/0001_initial_ranked.sql", import.meta.url),
  );
  await pglite.exec(await readFile(migrationPath, "utf8"));
}, 60_000);

afterAll(async () => {
  await pglite.close();
});

beforeEach(async () => {
  await database.query("TRUNCATE ranked_players, ranked_config_snapshots CASCADE");
});

class SequenceIds implements IdGenerator {
  private value = 100;
  public next(): string {
    this.value += 1;
    return `00000000-0000-4000-8001-${String(this.value).padStart(12, "0")}`;
  }
}

class FixedClock implements ServerClock {
  private current = new Date("2026-08-20T01:00:00.000Z");

  public now(): Date {
    return new Date(this.current);
  }

  public advance(seconds: number): void {
    this.current = new Date(this.current.getTime() + seconds * 1_000);
  }
}

class MutableCsmpSource implements CsmpTierSource {
  public tier: CsmpTier = "BRONZE";
  public async fetchCurrentTier(): Promise<CsmpTier> {
    return this.tier;
  }
}

const cleanMods: CreateSessionDto["installedMods"] = [
  {
    id: "hwanhee1.corum_ranked",
    version: "0.1.0",
    enabled: true,
    loaded: true,
    internal: false,
    system: false,
  },
  {
    id: "syzzi.click_between_frames",
    version: "1.5.0",
    enabled: true,
    loaded: true,
    internal: false,
    system: false,
    settings: {
      "soft-toggle": false,
      "click-on-steps": false,
      "physics-bypass": false,
    },
  },
];

const contextFor = async (
  accountId: string,
  tier: CsmpTier,
  sessions: SessionService,
  source: MutableCsmpSource,
  tokens: TokenService,
): Promise<RankedSessionContext> => {
  source.tier = tier;
  const created = await sessions.create({
    gdAccountId: accountId,
    gdUsername: `Player${accountId}`,
    clientVersion: "v0.1.0",
    installedMods: structuredClone(cleanMods),
  });
  const result = await database.query<{
    session_id: string;
    player_id: string;
    gd_username: string;
    displayed_tier: RankedSessionContext["displayedTier"];
    hidden_mmr: number;
    placement_games: number;
  }>(
    `SELECT s.id AS session_id, p.id AS player_id, p.gd_username,
            rp.displayed_tier, rp.hidden_mmr, rp.placement_games
     FROM ranked_sessions s
     JOIN ranked_players p ON p.id = s.player_id
     JOIN ranked_profiles rp ON rp.player_id = p.id
     WHERE s.token_hash = $1`,
    [tokens.hash(created.sessionToken)],
  );
  const row = result.rows[0]!;
  return {
    sessionId: row.session_id,
    playerId: row.player_id,
    gdAccountId: accountId,
    gdUsername: row.gd_username,
    displayedTier: row.displayed_tier,
    hiddenMmr: Number(row.hidden_mmr),
    placementGames: Number(row.placement_games),
  };
};

describe("transactional matchmaking queue", () => {
  it("atomically pairs compatible players and snapshots candidates/config", async () => {
    const document = configDocumentFixture("queue-test");
    const config = {
      getSnapshot: () => ({ ...structuredClone(document), fetchedAt: "2026-08-20T01:00:00Z" }),
    } as RankedConfigService;
    const source = new MutableCsmpSource();
    const ids = new SequenceIds();
    const clock = new FixedClock();
    const tokens = new TokenService(environmentFixture());
    const sessions = new SessionService(database, source, clock, ids, config, tokens);
    const queue = new QueueService(
      database,
      clock,
      ids,
      new SeededRandom(44),
      config,
      sessions,
      tokens,
    );
    const first = await contextFor("8101", "BRONZE", sessions, source, tokens);
    const second = await contextFor("8102", "BRONZE", sessions, source, tokens);
    expect(await queue.join(first, { installedMods: structuredClone(cleanMods) })).toMatchObject({
      status: "QUEUED",
    });
    const matched = await queue.join(second, { installedMods: structuredClone(cleanMods) });
    expect(matched.status).toBe("MATCHED");
    const firstStatus = await queue.status(first);
    const secondStatus = await queue.status(second);
    expect(firstStatus).toMatchObject({ status: "MATCHED", side: "A" });
    expect(secondStatus).toMatchObject({ status: "MATCHED", side: "B" });
    expect(firstStatus.matchId).toBe(secondStatus.matchId);
    expect(firstStatus.matchToken).not.toBe(secondStatus.matchToken);

    const match = await database.query<{
      candidate_maps_snapshot: unknown[];
      effective_tier: string;
      state: string;
    }>("SELECT candidate_maps_snapshot, effective_tier, state FROM ranked_matches");
    expect(match.rows).toHaveLength(1);
    expect(match.rows[0]).toMatchObject({ effective_tier: "BRONZE", state: "MATCHED" });
    expect(match.rows[0]!.candidate_maps_snapshot).toHaveLength(5);
    const snapshots = await database.query<{ count: number }>(
      "SELECT COUNT(*)::int AS count FROM ranked_config_snapshots",
    );
    expect(snapshots.rows[0]?.count).toBe(1);
  });

  it("expires a silent queue entry before it can be matched", async () => {
    const document = configDocumentFixture("queue-expiry-test");
    const config = {
      getSnapshot: () => ({ ...structuredClone(document), fetchedAt: "2026-08-20T01:00:00Z" }),
    } as RankedConfigService;
    const source = new MutableCsmpSource();
    const ids = new SequenceIds();
    const clock = new FixedClock();
    const tokens = new TokenService(environmentFixture());
    const sessions = new SessionService(database, source, clock, ids, config, tokens);
    const queue = new QueueService(
      database,
      clock,
      ids,
      new SeededRandom(45),
      config,
      sessions,
      tokens,
    );
    const stale = await contextFor("8201", "BRONZE", sessions, source, tokens);
    expect(await queue.join(stale, { installedMods: structuredClone(cleanMods) })).toMatchObject({
      status: "QUEUED",
    });
    clock.advance(document.operational.timeouts!.queueHeartbeatSeconds + 1);
    const current = await contextFor("8202", "BRONZE", sessions, source, tokens);
    expect(await queue.join(current, { installedMods: structuredClone(cleanMods) })).toMatchObject({
      status: "QUEUED",
    });
    expect(await queue.status(stale)).toMatchObject({ status: "EXPIRED" });
  });
});
