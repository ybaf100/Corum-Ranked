import { describe, expect, it } from "vitest";
import { DEFAULT_SURVIVAL_CONFIG as config, survivalPlacementLp, survivalTierForLp, updateSurvivalRating, validateSurvivalConfig } from "../src/survival.js";

describe("independent Survival M1 rules", () => {
  it("validates all agreed defaults and rejects unsafe or unsupported configuration", () => {
    expect(validateSurvivalConfig(config)).toEqual([]);
    for (const patch of [{ minimumPlayers: 7 }, { placementEnabled: true }, { promotionSeriesEnabled: true },
      { scoreMultiplier: NaN }, { tierBands: [] }, { placementLp: [50] }, { demotionTargetOffsetLp: 301 }]) {
      expect(validateSurvivalConfig({ ...config, ...patch }).length).toBeGreaterThan(0);
    }
  });
  it("starts at RED 150, promotes immediately and preserves surplus LP", () => {
    expect(survivalTierForLp(150, config.tierBands).tier).toBe("RED");
    const after = updateSurvivalRating({ lp: 290, tier: "RED", demotionWarning: false }, 50, config);
    expect(after.profile).toEqual({ lp: 340, tier: "AQUA", demotionWarning: false });
    expect(after.promoted).toBe(true);
    expect(updateSurvivalRating({ lp: 1190, tier: "SILVER", demotionWarning: true }, 50, config).profile)
      .toEqual({ lp: 1240, tier: "GOLD", demotionWarning: false });
  });
  it("uses the minimum occupied LP award, including negative ties", () => {
    expect(config.placementLp.map((_, i) => survivalPlacementLp(i + 1, 1, config.placementLp))).toEqual(config.placementLp);
    expect(survivalPlacementLp(2, 2, config.placementLp)).toBe(30);
    expect(survivalPlacementLp(11, 2, config.placementLp)).toBe(-20);
    expect(survivalPlacementLp(9, 8, config.placementLp)).toBe(-55);
    for (const ranks of [[0, 1], [16, 2], [1, 0], [1.5, 1]]) expect(() => survivalPlacementLp(ranks[0]!, ranks[1]!, config.placementLp)).toThrow();
  });
  it("protects once, recovers at floor+50 and demotes to old floor-50", () => {
    const protectedLoss = updateSurvivalRating({ lp: 910, tier: "SILVER", demotionWarning: false }, -20, config);
    expect(protectedLoss.profile).toEqual({ lp: 900, tier: "SILVER", demotionWarning: true });
    expect(protectedLoss.warningRecoveryLp).toBe(950);
    expect(updateSurvivalRating(protectedLoss.profile, 49, config).profile.demotionWarning).toBe(true);
    expect(updateSurvivalRating(protectedLoss.profile, 50, config).profile.demotionWarning).toBe(false);
    const demoted = updateSurvivalRating(protectedLoss.profile, -10, config);
    expect(demoted.profile).toEqual({ lp: 850, tier: "BRONZE", demotionWarning: false });
    expect(demoted.demoted).toBe(true);
    expect(updateSurvivalRating({ lp: 10, tier: "RED", demotionWarning: false }, -55, config).profile.lp).toBe(0);
    expect(updateSurvivalRating({ lp: 600, tier: "BRONZE", demotionWarning: false }, -10, { ...config, demotionProtectionEnabled: false }).profile.lp).toBe(550);
  });
});
