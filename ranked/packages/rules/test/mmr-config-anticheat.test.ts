import { describe, expect, it } from "vitest";
import {
  calculateMmrUpdate,
  displayedTierForProfile,
  evaluateClientEnvironment,
  seedProfileOnce,
  validateOperationalConfig,
  type InstalledModSnapshot,
  type RankedProfileSeedState,
} from "../src/index.js";
import {
  clientPolicyFixture,
  completeOperationalConfig,
  tierBandsFixture,
} from "./fixtures.js";

describe("operational config and MMR", () => {
  it("does not enable queue while unresolved operating values are absent", () => {
    const complete = completeOperationalConfig();
    const incomplete = {
      enabled: complete.enabled,
      generation: complete.generation,
      rules: complete.rules,
      tierBands: complete.tierBands,
      csmpTierNames: complete.csmpTierNames,
      csmpSeeds: {},
      cbf: complete.cbf,
    };
    const result = validateOperationalConfig(incomplete);
    expect(result.valid).toBe(false);
    expect(result.queueReady).toBe(false);
    expect(result.errors).toContain("csmpSeeds.NONE must be configured");
  });

  it("accepts a complete explicitly configured policy", () => {
    expect(validateOperationalConfig(completeOperationalConfig())).toEqual({
      valid: true,
      queueReady: true,
      errors: [],
    });
  });

  it("fails closed when either opponent-strength weight is outside 0..2", () => {
    const config = completeOperationalConfig();
    expect(validateOperationalConfig({
      ...config,
      mmrPolicy: { ...config.mmrPolicy!, mmrOpponentStrengthWeight: 2.01 },
    }).valid).toBe(false);
    expect(validateOperationalConfig({
      ...config,
      mmrPolicy: { ...config.mmrPolicy!, visibleScoreOpponentStrengthWeight: -0.01 },
    }).valid).toBe(false);
  });

  it("applies a Bronze CSMP seed exactly once", () => {
    const initial: RankedProfileSeedState = {
      hiddenMmr: null,
      placementGamesPlayed: 0,
      initialCsmpTier: null,
      initialSeedMmr: null,
      seedAppliedAt: null,
    };
    const config = completeOperationalConfig();
    const first = seedProfileOnce(initial, "BRONZE", config.csmpSeeds, "2026-08-20T00:00:00Z");
    const second = seedProfileOnce(
      first.profile,
      "GOLD",
      config.csmpSeeds,
      "2026-08-21T00:00:00Z",
    );
    expect(first.applied).toBe(true);
    expect(first.profile).toMatchObject({
      hiddenMmr: 2_500,
      initialCsmpTier: "BRONZE",
      initialSeedMmr: 2_500,
    });
    expect(second.applied).toBe(false);
    expect(second.profile).toEqual(first.profile);
  });

  it("accepts and snapshots an operator-defined CSMP tier without changing Ranked display tiers", () => {
    const initial: RankedProfileSeedState = {
      hiddenMmr: null,
      placementGamesPlayed: 0,
      initialCsmpTier: null,
      initialSeedMmr: null,
      seedAppliedAt: null,
    };
    const config = completeOperationalConfig();
    const seeds = { ...config.csmpSeeds, DIAMOND: 5_200 };
    expect(validateOperationalConfig({
      ...config,
      csmpTierNames: [...config.csmpTierNames, "DIAMOND"],
      csmpSeeds: seeds,
    }).valid).toBe(true);
    expect(seedProfileOnce(initial, " diamond ", seeds, "2026-08-24T00:00:00Z")).toMatchObject({
      applied: true,
      profile: {
        hiddenMmr: 5_200,
        initialCsmpTier: "DIAMOND",
        initialSeedMmr: 5_200,
      },
    });
  });

  it("shows UNRANKED until the configured placement count is reached", () => {
    const policy = completeOperationalConfig().mmrPolicy!;
    expect(displayedTierForProfile(2_500, 4, policy, tierBandsFixture)).toBe("UNRANKED");
    expect(displayedTierForProfile(2_500, 5, policy, tierBandsFixture)).toBe("BRONZE");
  });

  it("maps negative Hidden MMR to the lowest displayed tier after placement", () => {
    const policy = completeOperationalConfig().mmrPolicy!;
    expect(displayedTierForProfile(-271, policy.placementGames, policy, tierBandsFixture)).toBe(
      "RED",
    );
  });

  it("uses explicitly configured placement and normal K factors", () => {
    const policy = completeOperationalConfig().mmrPolicy!;
    const result = calculateMmrUpdate(
      {
        ratingA: 2_000,
        ratingB: 2_000,
        placementGamesA: 0,
        placementGamesB: 10,
        winner: "A",
      },
      policy,
    );
    expect(result.deltaA).toBe(32);
    expect(result.deltaB).toBe(-16);
  });
});

describe("allowlist and CBF gate", () => {
  const ranked: InstalledModSnapshot = {
    id: "hwanhee1.corum_ranked",
    version: "0.1.0",
    enabled: true,
    loaded: true,
    internal: false,
    system: false,
  };
  const cbf: InstalledModSnapshot = {
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
  };

  it("allows a clean environment and ignores internal/system entries", () => {
    const decision = evaluateClientEnvironment(
      [
        ranked,
        cbf,
        {
          id: "geode.internal-loader",
          version: "5.8.2",
          enabled: true,
          loaded: true,
          internal: true,
          system: true,
        },
      ],
      clientPolicyFixture(),
    );
    expect(decision.allowed).toBe(true);
  });

  it("ignores an unallowed package when it is disabled", () => {
    const decision = evaluateClientEnvironment(
      [
        ranked,
        cbf,
        {
          id: "not.allowed",
          version: "1.0.0",
          enabled: false,
          loaded: false,
          internal: false,
          system: false,
        },
      ],
      clientPolicyFixture(),
    );
    expect(decision.allowed).toBe(true);
    expect(decision.unauthorizedModIds).toEqual([]);
    expect(decision.allowedModIds).toContain("syzzi.click_between_frames");
  });

  it("blocks an unallowed package when it is active", () => {
    const decision = evaluateClientEnvironment(
      [
        ranked,
        cbf,
        {
          id: "not.allowed",
          version: "1.0.0",
          enabled: true,
          loaded: true,
          internal: false,
          system: false,
        },
      ],
      clientPolicyFixture(),
    );
    expect(decision.allowed).toBe(false);
    expect(decision.unauthorizedModIds).toEqual(["not.allowed"]);
  });

  it("blocks missing or inactive CBF", () => {
    const missing = evaluateClientEnvironment([ranked], clientPolicyFixture());
    expect(missing.allowed).toBe(false);
    expect(missing.cbfIssues).toContain("CBF_NOT_INSTALLED");

    const inactive = evaluateClientEnvironment(
      [ranked, { ...cbf, enabled: false, loaded: false }],
      clientPolicyFixture(),
    );
    expect(inactive.allowed).toBe(false);
    expect(inactive.cbfIssues).toContain("CBF_NOT_ACTIVE");
  });

  it("blocks a mismatched CBF input/physics profile without changing it silently", () => {
    const decision = evaluateClientEnvironment(
      [ranked, { ...cbf, settings: { ...cbf.settings, "click-on-steps": true } }],
      clientPolicyFixture(),
    );
    expect(decision.allowed).toBe(false);
    expect(decision.cbfIssues).toContain("CBF_SETTING_MISMATCH:click-on-steps");
  });
});
