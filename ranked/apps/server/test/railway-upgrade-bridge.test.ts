import { describe, expect, it } from "vitest";
import { shouldStartLegacyRailwayBridge, createLegacyRailwayHandler } from "../src/legacy/railway-upgrade-bridge.js";
import { createServer } from "node:http";

describe("Legacy Railway upgrade-only routing", () => {
  it("activates only for retired production Railway service", () => {
    expect(shouldStartLegacyRailwayBridge({
      RAILWAY_PROJECT_ID: "9a50ad5f-c812-4200-b97a-0296492097cd",
      RAILWAY_SERVICE_ID: "f8f77cca-8054-47bb-b7e7-21581f394f0f",
    })).toBe(true);
    expect(shouldStartLegacyRailwayBridge({})).toBe(false);
    expect(shouldStartLegacyRailwayBridge({
      RAILWAY_PROJECT_ID: "another-project",
      RAILWAY_SERVICE_ID: "f8f77cca-8054-47bb-b7e7-21581f394f0f",
    })).toBe(false);
  });

  it("returns public update metadata but rejects gameplay writes", async () => {
    let calls = 0;
    const handler = createLegacyRailwayHandler(async () => {
      calls++;
      return new Response(JSON.stringify({
        requiredClientVersion: "v0.5.0-beta.93",
        modUpdates: { release: { version: "v0.5.0-beta.93" } },
      }), { status: 200 });
    });
    const server = createServer(handler).listen(0, "127.0.0.1");
    try {
      await new Promise<void>((resolve) => server.once("listening", resolve));
      const address = server.address();
      if (!address || typeof address === "string") throw new Error("Missing port");
      const base = "http://127.0.0.1:" + String(address.port);
      const response = await fetch(base + "/api/ranked/config");
      expect(response.status).toBe(200);
      expect((await response.json()).requiredClientVersion).toBe("v0.5.0-beta.93");
      const write = await fetch(base + "/api/ranked/queue/join", { method: "POST" });
      expect(write.status).toBe(426);
      expect(calls).toBe(1);
    } finally {
      await new Promise<void>((resolve, reject) =>
        server.close((err) => err ? reject(err) : resolve()));
    }
  });
});
