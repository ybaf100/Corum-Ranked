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
  it("rejects beta39 before native Pause callback and conceded-scene return fixes", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.39", [clientMod("v0.5.0-beta.39")]).allowed).toBe(false);
  });
  it("rejects beta34 before the beta35 native scene entry barrier", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.34", [clientMod("v0.5.0-beta.34")]).allowed).toBe(false);
  });
  it("rejects beta33 after beta34 automatic preparation READY timing fix", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.33", [clientMod("v0.5.0-beta.33")]).allowed).toBe(false);
  });
  it("rejects beta32 after beta33 direct round preparation and launch", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.32", [clientMod("v0.5.0-beta.32")]).allowed).toBe(false);
  });
  it("rejects beta31 after beta32 ordered live capture and match controls", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.31", [clientMod("v0.5.0-beta.31")]).allowed).toBe(false);
  });
  it("rejects beta30 after the beta31 global visibility-hook fix", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.30", [clientMod("v0.5.0-beta.30")]).allowed).toBe(false);
  });
  it("rejects beta29 after the beta30 persistence and timeline hotfix", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.29", [clientMod("v0.5.0-beta.29")]).allowed).toBe(false);
  });
  it("accepts only the exact current Corum Ranked client version", () => {
    expect(
      evaluateRankedClientVersion(REQUIRED_RANKED_CLIENT_VERSION, [
        clientMod(REQUIRED_RANKED_CLIENT_VERSION),
      ]).allowed,
    ).toBe(true);
  });

  it("accepts the same current version with or without a leading v", () => {
    expect(
      evaluateRankedClientVersion("0.5.0-beta.40", [clientMod("0.5.0-beta.40")]).allowed,
    ).toBe(true);
  });

  it("rejects an older declared client version", () => {
    const result = evaluateRankedClientVersion("v0.5.0-beta.8", [
      clientMod(REQUIRED_RANKED_CLIENT_VERSION),
    ]);
    expect(result).toMatchObject({
      allowed: false,
      requiredVersion: "v0.5.0-beta.40",
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

  it("rejects beta20 after the beta21 native Geode field-access fix", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.20", [clientMod("v0.5.0-beta.20")]).allowed).toBe(false);
  });
  it("rejects beta21 after the beta22 server-backed Debug Spectate harness", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.21", [clientMod("v0.5.0-beta.21")]).allowed).toBe(false);
  });
  it("rejects beta22 after the beta23 fresh debug-scope deadline fix", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.22", [clientMod("v0.5.0-beta.22")]).allowed).toBe(false);
  });
  it("rejects beta23 after the beta24 verified Saved Debug Spectate launch fix", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.23", [clientMod("v0.5.0-beta.23")]).allowed).toBe(false);
  });

  it("rejects when the active Corum Ranked mod snapshot is missing", () => {
    expect(evaluateRankedClientVersion(REQUIRED_RANKED_CLIENT_VERSION, []).allowed).toBe(false);
  });

  it("rejects beta25 after the beta26 native Debug Play activation fix", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.25", [clientMod("v0.5.0-beta.25")]).allowed).toBe(false);
  });
  it("rejects beta26 after the beta27 spectator audio/timeline/native presentation hotfix", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.26", [clientMod("v0.5.0-beta.26")]).allowed).toBe(false);
  });
  it("rejects beta27 after the beta28 final viewport and publisher-clock presentation hotfix", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.27", [clientMod("v0.5.0-beta.27")]).allowed).toBe(false);
  });
  it("rejects beta28 after the beta29 hidden world-driver hotfix", () => {
    expect(evaluateRankedClientVersion("v0.5.0-beta.28", [clientMod("v0.5.0-beta.28")]).allowed).toBe(false);
  });

});
