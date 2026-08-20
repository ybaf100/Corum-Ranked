import { describe, expect, it } from "vitest";
import type { RankedConfigSource } from "../src/config/ranked-config.source.js";
import { RankedConfigService } from "../src/config/ranked-config.service.js";
import { configDocumentFixture, environmentFixture } from "./fixtures.js";

class FakeSource implements RankedConfigSource {
  public document = configDocumentFixture();
  public failure: Error | null = null;

  public async fetchConfig() {
    if (this.failure) throw this.failure;
    return structuredClone(this.document);
  }
}

describe("Ranked config snapshot loader", () => {
  it("accepts a valid source document and returns an isolated snapshot", async () => {
    const source = new FakeSource();
    const service = new RankedConfigService(source, environmentFixture());
    expect(await service.refresh()).toBe(true);
    const snapshot = service.getSnapshot();
    source.document = {
      ...source.document,
      maps: [
        {
          ...source.document.maps[0]!,
          alternateLevelId: "88888888",
          title: "changed later",
        },
        ...source.document.maps.slice(1),
      ],
    };
    expect(snapshot.maps[0]!.title).not.toBe("changed later");
    expect(snapshot.maps[0]!.alternateLevelId).not.toBe("88888888");
    expect(service.getStatus()).toMatchObject({ ready: true, generation: "test-1" });
  });

  it("rejects canonical/alternate aliases owned by different Ranked maps", async () => {
    const source = new FakeSource();
    const firstAlternate = source.document.maps[0]!.alternateLevelId!;
    source.document = {
      ...source.document,
      maps: source.document.maps.map((map, index) =>
        index === 1 ? { ...map, canonicalLevelId: firstAlternate } : map,
      ),
    };
    const service = new RankedConfigService(source, environmentFixture());
    expect(await service.refresh()).toBe(false);
    expect(service.getStatus().lastError).toContain("belongs to multiple canonical maps");
  });

  it("keeps the last known valid snapshot after a failed refresh", async () => {
    const source = new FakeSource();
    const service = new RankedConfigService(source, environmentFixture());
    expect(await service.refresh()).toBe(true);
    source.failure = new Error("temporary Apps Script failure");
    expect(await service.refresh()).toBe(false);
    expect(service.getSnapshot().generation).toBe("test-1");
    expect(service.getStatus()).toMatchObject({
      ready: true,
      lastError: "temporary Apps Script failure",
    });
  });

  it("refuses an invalid cold-start config instead of enabling the queue", async () => {
    const source = new FakeSource();
    source.document = {
      ...source.document,
      operational: { ...source.document.operational, csmpSeeds: {} },
    };
    const service = new RankedConfigService(source, environmentFixture());
    expect(await service.refresh()).toBe(false);
    expect(service.getStatus().ready).toBe(false);
    expect(() => service.getSnapshot()).toThrow("Ranked config has no valid snapshot");
  });
});
