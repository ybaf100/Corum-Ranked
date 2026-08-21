import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PGlite } from "@electric-sql/pglite";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import type { CsmpTier } from "@corum-ranked/rules";
import type { IdGenerator, ServerClock } from "../src/common/runtime.module.js";
import { TokenService } from "../src/common/token.service.js";
import type { CsmpTierSource } from "../src/config/csmp-tier.source.js";
import type { RankedConfigService } from "../src/config/ranked-config.service.js";
import { SessionService } from "../src/session/session.service.js";
import type { CreateSessionDto } from "../src/session/session.dto.js";
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

class SequenceIds implements IdGenerator {
  private value = 10;
  public next(): string {
    this.value += 1;
    return `00000000-0000-4000-8000-${String(this.value).padStart(12, "0")}`;
  }
}

class FixedClock implements ServerClock {
  public now(): Date {
    return new Date("2026-08-20T00:00:00.000Z");
  }
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
  ],
});

const buildService = (source: FakeCsmpSource) => {
  const document = configDocumentFixture();
  const config = {
    getSnapshot: () => ({ ...structuredClone(document), fetchedAt: "2026-08-20T00:00:00Z" }),
  } as RankedConfigService;
  return new SessionService(
    database,
    source,
    new FixedClock(),
    new SequenceIds(),
    config,
    new TokenService(environmentFixture()),
  );
};

describe("Ranked session/profile creation", () => {
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
