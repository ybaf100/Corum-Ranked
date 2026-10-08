import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, beforeEach, describe, expect, it } from "vitest";
import type { CsmpTier } from "@corum-ranked/rules";
import type { IdGenerator, ServerClock } from "../src/common/runtime.module.js";
import { TokenService } from "../src/common/token.service.js";
import type { CsmpTierSource } from "../src/config/csmp-tier.source.js";
import type { RankedConfigService } from "../src/config/ranked-config.service.js";
import { SessionService } from "../src/session/session.service.js";
import { SessionGuard } from "../src/session/session.guard.js";
import type { RankedSessionContext } from "../src/session/session.types.js";
import type { CreateSessionDto } from "../src/session/session.dto.js";
import { configDocumentFixture, environmentFixture } from "./fixtures.js";
import { PgliteDatabase } from "./pglite-database.js";

let pglite: PGlite;
let database: PgliteDatabase;

beforeAll(async () => {
  pglite = new PGlite();
  database = new PgliteDatabase(pglite);
  for (const migrationName of ["0001_initial_ranked.sql", "0002_attempt_start_leases.sql"]) {
    const migrationPath = fileURLToPath(
      new URL(`../../../migrations/${migrationName}`, import.meta.url),
    );
    await pglite.exec(await readFile(migrationPath, "utf8"));
  }
}, 60_000);

afterAll(async () => {
  await pglite.close();
});

beforeEach(async () => {
  await pglite.exec(
    "TRUNCATE TABLE ranked_sessions, ranked_profiles, ranked_players CASCADE",
  );
});

class SequenceIds implements IdGenerator {
  private value = 10;
  public next(): string {
    this.value += 1;
    return `00000000-0000-4000-8000-${String(this.value).padStart(12, "0")}`;
  }
}

class FixedClock implements ServerClock {
  public milliseconds = Date.parse("2026-08-20T00:00:00.000Z");
  public now(): Date {
    return new Date(this.milliseconds);
  }
  public advance(milliseconds: number) { this.milliseconds += milliseconds; }
}

class FakeCsmpSource implements CsmpTierSource {
  public tier: CsmpTier = "BRONZE";
  public calls = 0;
  public async fetchCurrentTier(): Promise<CsmpTier> {
    this.calls += 1;
    return this.tier;
  }
}

const cleanDto = (accountId: string): CreateSessionDto => ({
  gdAccountId: accountId,
  gdUsername: `Player${accountId}`,
  clientVersion: "v0.1.0",
  clientInstanceId: "test-process-instance",
  installedMods: [
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
    {
      id: "geode.node-ids",
      version: "v1.23.3",
      enabled: true,
      loaded: true,
      internal: false,
      system: false,
    },
  ],
});

const buildService = (source: FakeCsmpSource, clock = new FixedClock()) => {
  const document = configDocumentFixture();
  const config = {
    getSnapshot: () => ({ ...structuredClone(document), fetchedAt: "2026-08-20T00:00:00Z" }),
  } as RankedConfigService;
  return new SessionService(
    database,
    source,
    clock,
    new SequenceIds(),
    config,
    new TokenService(environmentFixture()),
  );
};

describe("Ranked session/profile creation", () => {
  const contextFor = async (token: string): Promise<RankedSessionContext> => {
    const result = await database.query<{ id: string; player_id: string }>(
      "SELECT id, player_id FROM ranked_sessions WHERE token_hash = $1",
      [new TokenService(environmentFixture()).hash(token)],
    );
    return { sessionId: result.rows[0]!.id, playerId: result.rows[0]!.player_id,
      gdAccountId: "7001", gdUsername: "Player7001", displayedTier: "UNRANKED", hiddenMmr: 2500, placementGames: 0 };
  };

  it("blocks a second device at lobby admission and permits it immediately after Close", async () => {
    const clock = new FixedClock(), service = buildService(new FakeCsmpSource(), clock);
    const first = await service.create(cleanDto("7001"));
    const other = { ...cleanDto("7001"), clientInstanceId: "second-device-instance" };
    await expect(service.create(other)).rejects.toMatchObject({ response: { code: "RANKED_ACCOUNT_ACTIVE" } });
    await service.close(await contextFor(first.sessionToken));
    const second = await service.create(other);
    expect(second.sessionToken).not.toBe(first.sessionToken);
    await expect(service.heartbeat(await contextFor(first.sessionToken))).rejects.toMatchObject({ status: 401 });
    expect((await service.heartbeat(await contextFor(second.sessionToken))).sessionLeaseSeconds).toBe(20);
  });

  it("expires a crashed process in 20s; heartbeat renews and an expired token cannot revive", async () => {
    const clock = new FixedClock(), service = buildService(new FakeCsmpSource(), clock);
    const first = await service.create(cleanDto("7001")), firstContext = await contextFor(first.sessionToken);
    const other = { ...cleanDto("7001"), clientInstanceId: "second-device-instance" };
    clock.advance(15000);await service.heartbeat(firstContext);
    clock.advance(19000);await expect(service.create(other)).rejects.toMatchObject({ status: 409 });
    clock.advance(1000);await expect(service.heartbeat(firstContext)).rejects.toMatchObject({ status: 401 });
    await expect(service.create(other)).resolves.toMatchObject({ sessionLeaseSeconds: 20, sessionHeartbeatSeconds: 5 });
    await expect(service.heartbeat(firstContext)).rejects.toMatchObject({ status: 401 });
  });

  it("same-process creation retries rotate tokens; late Close cannot release the replacement", async () => {
    const service = buildService(new FakeCsmpSource());
    const first = await service.create(cleanDto("7001")), old = await contextFor(first.sessionToken);
    const second = await service.create(cleanDto("7001")), replacement = await contextFor(second.sessionToken);
    await service.close(old);await expect(service.heartbeat(replacement)).resolves.toBeDefined();
    await expect(service.create({ ...cleanDto("7001"), clientInstanceId: "another-device-instance" }))
      .rejects.toMatchObject({ status: 409 });
  });

  it("guards all authenticated routes against expired/revoked leases, independently of token TTL", async () => {
    const clock = new FixedClock(), service = buildService(new FakeCsmpSource(), clock);
    const first = await service.create(cleanDto("7001"));
    const guard = new SessionGuard(database, new TokenService(environmentFixture()), clock);
    const request = { headers: { authorization: `Bearer ${first.sessionToken}` } };
    const context = { switchToHttp: () => ({ getRequest: () => request }) } as any;
    // Service fixture uses an old version; update only this persisted row to
    // current so the test exercises the lease rather than version admission.
    const { REQUIRED_RANKED_CLIENT_VERSION } = await import("../src/session/client-version.js");
    await database.query("UPDATE ranked_sessions SET client_version = $1", [REQUIRED_RANKED_CLIENT_VERSION]);
    await expect(guard.canActivate(context)).resolves.toBe(true);
    clock.advance(20000);await expect(guard.canActivate(context)).rejects.toMatchObject({ status: 401 });
    await service.create(cleanDto("7001"));await expect(guard.canActivate(context)).rejects.toMatchObject({ status: 401 });
  });
  it("persists a CSMP seed once and never reseeds after CSMP changes", async () => {
    const source = new FakeCsmpSource();
    const service = buildService(source);
    const first = await service.create(cleanDto("7001"));
    source.tier = "GOLD";
    const second = await service.create(cleanDto("7001"));
    const profile = await database.query<{
      hidden_mmr: number;
      initial_csmp_tier: string;
      initial_seed_mmr: number;
    }>(
      `SELECT hidden_mmr, initial_csmp_tier, initial_seed_mmr
       FROM ranked_profiles rp
       JOIN ranked_players p ON p.id = rp.player_id
       WHERE p.gd_account_id = $1`,
      ["7001"],
    );
    expect(source.calls).toBe(1);
    expect(profile.rows[0]).toMatchObject({
      hidden_mmr: 2500,
      initial_csmp_tier: "BRONZE",
      initial_seed_mmr: 2500,
    });
    expect(first.player.displayedTier).toBe("UNRANKED");
    expect(second.sessionToken).not.toBe(first.sessionToken);
  });

  it("ignores an unallowed disabled package when creating a profile", async () => {
    const source = new FakeCsmpSource();
    const service = buildService(source);
    const dto = cleanDto("7002");
    dto.installedMods.push({
      id: "not.allowed",
      version: "1.0.0",
      enabled: false,
      loaded: false,
      internal: false,
      system: false,
    });
    await expect(service.create(dto)).resolves.toBeDefined();
    const players = await database.query<{ count: number }>(
      "SELECT COUNT(*)::int AS count FROM ranked_players WHERE gd_account_id = $1",
      ["7002"],
    );
    expect(players.rows[0]?.count).toBe(1);
  });
});
