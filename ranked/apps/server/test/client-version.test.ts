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
      evaluateRankedClientVersion("0.5.0-beta.20", [clientMod("0.5.0-beta.20")]).allowed,
    ).toBe(true);
  });

  it("rejects an older declared client version", () => {
    const result = evaluateRankedClientVersion("v0.5.0-beta.8", [
      clientMod(REQUIRED_RANKED_CLIENT_VERSION),
    ]);
    expect(result).toMatchObject({
      allowed: false,
      requiredVersion: "v0.5.0-beta.20",
      clientVersion: "v0.5.0-beta.8",
    });
  });

  it("rejects when the active installed mod itself is not current", () => {
    const result = evaluateRankedClientVersion(REQUIRED_RANKED_CLIENT_VERSION, [
      clientMod("v0.5.0-beta.7"),
    ]);
    expect(result).toMatchObject({
      allowed: false,
      installedModVersion: "v0.5.0-beta.7",
    });
  });

  it("rejects beta16 after the beta17 spectator safety patch", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.16", [clientMod("v0.5.0-beta.16")]).allowed).toBe(false);
  });

  it("rejects beta17 after the beta18 dedicated remote renderer patch", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.17", [clientMod("v0.5.0-beta.17")]).allowed).toBe(false);
  });

  it("rejects beta18 after the beta19 native point-assignment fix", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.18", [clientMod("v0.5.0-beta.18")]).allowed).toBe(false);
  });

  it("rejects beta19 after the beta20 spectator camera/handoff patch", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.19", [clientMod("v0.5.0-beta.19")]).allowed).toBe(false);
  });

  it("rejects when the active Corum Ranked mod snapshot is missing", () => {
    expect(evaluateRankedClientVersion(REQUIRED_RANKED_CLIENT_VERSION, []).allowed).toBe(false);
  });
});
