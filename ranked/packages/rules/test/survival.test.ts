import { describe, expect, it } from "vitest";
import { SURVIVAL_PROMOTION_RULES, DEFAULT_SURVIVAL_CONFIG as config, survivalPlacementLp, survivalTierForLp, updateSurvivalRating, validateSurvivalConfig } from "../src/survival.js";

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

describe("Survival promotion series", () => {
  const at16 = (rank: number) => ({ rank, playerCount: 16 });
  const at64 = (rank: number) => ({ rank, playerCount: 64 });
  it("holds below the boundary and opens a RED->AQUA series instead of promoting", () => {
    const u = updateSurvivalRating({ lp: 290, tier: "RED", demotionWarning: false }, 30, config, at16(3));
    expect(u.promotionStarted).toBe(true);expect(u.profile.lp).toBe(299);expect(u.profile.tier).toBe("RED");
    expect(u.profile.promotion).toMatchObject({ sourceTier: "RED", targetTier: "AQUA", cutoff: 300, chances: 3, top16: 10, top64: 40, results: [] });
    expect(SURVIVAL_PROMOTION_RULES).toMatchObject({ AQUA: { top16: 5, top64: 15, chances: 3 }, BRONZE: { top16: 2, top64: 5, chances: 5 }, SILVER: { top16: 1, top64: 1, chances: 5 } });
  });
  it("promotes on the first qualifying finish and freezes LP while the series runs", () => {
    const start = updateSurvivalRating({ lp: 290, tier: "RED", demotionWarning: false }, 30, config, at16(1)).profile;
    const miss = updateSurvivalRating(start, 50, config, at16(11));
    expect(miss.actualDelta).toBe(0);expect(miss.profile.promotion?.results).toEqual(["LOSS"]);expect(miss.profile.promotion?.ranks).toEqual([11]);expect(miss.profile.lp).toBe(299);
    const pass = updateSurvivalRating(miss.profile, -20, config, at64(40));
    expect(pass.promotionSucceeded).toBe(true);expect(pass.profile).toEqual({ lp: 350, tier: "AQUA", demotionWarning: false, promotion: null });expect(pass.actualDelta).toBe(51);
  });
  it("costs 50 LP after every chance is used without a qualifying finish", () => {
    let profile = updateSurvivalRating({ lp: 880, tier: "BRONZE", demotionWarning: false }, 40, config, at16(5)).profile;
    expect(profile.promotion).toMatchObject({ targetTier: "SILVER", chances: 5, top16: 2, top64: 5 });
    for (let i = 0; i < 4; i++) { const u = updateSurvivalRating(profile, 10, config, at16(3)); expect(u.promotionFailed).toBeFalsy(); profile = u.profile; }
    const failed = updateSurvivalRating(profile, 10, config, at64(6));
    expect(failed.promotionFailed).toBe(true);expect(failed.profile).toEqual({ lp: 850, tier: "BRONZE", demotionWarning: false, promotion: null });
  });
  it("SILVER->GOLD requires a win; Gold has no further series; legacy callers promote directly", () => {
    const start = updateSurvivalRating({ lp: 1190, tier: "SILVER", demotionWarning: false }, 20, config, at16(1)).profile;
    expect(updateSurvivalRating(start, 0, config, at16(2)).profile.promotion?.results).toEqual(["LOSS"]);
    expect(updateSurvivalRating(start, 0, config, at64(1)).profile).toMatchObject({ tier: "GOLD", lp: 1250 });
    expect(updateSurvivalRating({ lp: 1300, tier: "GOLD", demotionWarning: false }, 50, config, at16(1)).profile).toEqual({ lp: 1350, tier: "GOLD", demotionWarning: false, promotion: null });
    expect(updateSurvivalRating({ lp: 290, tier: "RED", demotionWarning: false }, 30, config).profile.tier).toBe("AQUA");
  });
});
