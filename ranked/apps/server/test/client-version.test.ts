import { describe, expect, it } from "vitest";
import {
  REQUIRED_RANKED_CLIENT_VERSION,
  evaluateRankedClientVersion,
} from "../src/session/client-version.js";

const clientMod = (version: string) => ({
  id: "hwanhee1.corum_ranked",
  version,
  enabled: true,
  loaded: true,
});

describe("Ranked client version gate", () => {
  it("accepts only the exact current Corum Ranked client version", () => {
    expect(
      evaluateRankedClientVersion(REQUIRED_RANKED_CLIENT_VERSION, [
        clientMod(REQUIRED_RANKED_CLIENT_VERSION),
      ]).allowed,
    ).toBe(true);
  });

  it("accepts the same current version with or without a leading v", () => {
    expect(
      evaluateRankedClientVersion("0.4.0-alpha.37", [clientMod("0.4.0-alpha.37")]).allowed,
    ).toBe(true);
  });

  it("rejects an older declared client version", () => {
    const result = evaluateRankedClientVersion("v0.4.0-alpha.36", [
      clientMod(REQUIRED_RANKED_CLIENT_VERSION),
    ]);
    expect(result).toMatchObject({
      allowed: false,
      requiredVersion: "v0.4.0-alpha.37",
      clientVersion: "v0.4.0-alpha.36",
    });
  });

  it("rejects when the active installed mod itself is not current", () => {
    const result = evaluateRankedClientVersion(REQUIRED_RANKED_CLIENT_VERSION, [
      clientMod("v0.4.0-alpha.36"),
    ]);
    expect(result).toMatchObject({
      allowed: false,
      installedModVersion: "v0.4.0-alpha.36",
    });
  });

  it("rejects when the active Corum Ranked mod snapshot is missing", () => {
    expect(evaluateRankedClientVersion(REQUIRED_RANKED_CLIENT_VERSION, []).allowed).toBe(false);
  });
});
