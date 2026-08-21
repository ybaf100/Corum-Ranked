import { afterEach, describe, expect, it, vi } from "vitest";
import { ServiceUnavailableException } from "@nestjs/common";
import { AppsScriptCsmpTierSource, resolveCsmpTier } from "../src/config/csmp-tier.source.js";
import { environmentFixture } from "./fixtures.js";

const stages = [
  {
    order: 1,
    name: "Red",
    required: 2,
    maps: [
      { levelId: "1", alternateLevelId: "101", title: "Red 1" },
      { levelId: "2", title: "Red 2" },
    ],
  },
  {
    order: 2,
    name: "Aqua",
    required: 1,
    maps: [{ levelId: "3", title: "Aqua 1" }],
  },
  {
    order: 3,
    name: "Bronze",
    required: 1,
    maps: [{ levelId: "4", title: "Bronze 1" }],
  },
];

describe("authoritative CSMP seed lookup", () => {
  it("requires 100% and advances only through consecutive completed tiers", () => {
    expect(
      resolveCsmpTier(stages, [
        { levelId: "101", percent: 100, status: "unverified" },
        { levelId: "2", percent: 99, status: "verified" },
        { levelId: "3", percent: 100, status: "verified" },
      ]),
    ).toBe("NONE");
    expect(
      resolveCsmpTier(stages, [
        { levelId: "101", percent: 100, status: "unverified" },
        { levelId: "2", percent: 100, status: "verified" },
        { levelId: "3", percent: 100, status: "verified" },
      ]),
    ).toBe("AQUA");
  });

  it("does not count rejected records", () => {
    expect(
      resolveCsmpTier(stages, [
        { levelId: "1", percent: 100, status: "rejected" },
        { levelId: "2", percent: 100, status: "verified" },
      ]),
    ).toBe("NONE");
  });
});


afterEach(() => {
  vi.restoreAllMocks();
});

describe("Apps Script CSMP source resilience", () => {
  it("caches the shared csmp definition while still fetching each player's records", async () => {
    const environment = {
      ...environmentFixture(),
      rankedConfigUrl: "https://example.test/exec?action=ranked_config",
      rankedConfigRefreshMs: 60_000,
    };
    const calls: string[] = [];
    vi.spyOn(globalThis, "fetch").mockImplementation(async (input) => {
      const url = new URL(String(input));
      const action = url.searchParams.get("action") || "";
      calls.push(action);
      if (action === "csmp") {
        return new Response(JSON.stringify({ ok: true, tiers: stages }), { status: 200 });
      }
      return new Response(JSON.stringify({
        ok: true,
        players: [{ records: [
          { levelId: "1", percent: 100, status: "verified" },
          { levelId: "2", percent: 100, status: "verified" },
        ] }],
      }), { status: 200 });
    });

    const source = new AppsScriptCsmpTierSource(environment);
    await expect(source.fetchCurrentTier("7001")).resolves.toBe("RED");
    await expect(source.fetchCurrentTier("7002")).resolves.toBe("RED");
    expect(calls.filter((action) => action === "csmp")).toHaveLength(1);
    expect(calls.filter((action) => action === "player_records")).toHaveLength(2);
  });

  it("retries a timed out player_records request once and exposes a 503 code", async () => {
    const environment = {
      ...environmentFixture(),
      rankedConfigUrl: "https://example.test/exec?action=ranked_config",
      rankedCsmpFetchTimeoutMs: 123,
    };
    let recordAttempts = 0;
    vi.spyOn(globalThis, "fetch").mockImplementation(async (input) => {
      const url = new URL(String(input));
      if (url.searchParams.get("action") === "csmp") {
        return new Response(JSON.stringify({ ok: true, tiers: stages }), { status: 200 });
      }
      recordAttempts += 1;
      const timeout = new Error("simulated timeout");
      timeout.name = "TimeoutError";
      throw timeout;
    });

    const source = new AppsScriptCsmpTierSource(environment);
    try {
      await source.fetchCurrentTier("7001");
      throw new Error("expected fetchCurrentTier to fail");
    } catch (error) {
      expect(error).toBeInstanceOf(ServiceUnavailableException);
      const exception = error as ServiceUnavailableException;
      expect(exception.getStatus()).toBe(503);
      expect(exception.getResponse()).toMatchObject({ code: "CSMP_SOURCE_TIMEOUT" });
    }
    expect(recordAttempts).toBe(2);
  });
});
