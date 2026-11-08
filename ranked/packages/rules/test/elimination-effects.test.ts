import { describe, expect, it } from "vitest";
import { achievementApGrade, eliminationEffectUnlocked, highestBaseTier, normalizeEliminationEffect } from "../src/index.js";

describe("elimination effects", () => {
  it("grades claimed AP into the five private grades", () => {
    expect(achievementApGrade(0)).toEqual({ tier: null, ap: 0, floorAp: 0, nextTier: "RED", nextAp: 100 });
    expect(achievementApGrade(1240)).toEqual({ tier: "BRONZE", ap: 1240, floorAp: 1000, nextTier: "SILVER", nextAp: 2000 });
    expect(achievementApGrade(4965)).toEqual({ tier: "GOLD", ap: 4965, floorAp: 3200, nextTier: null, nextAp: null });
  });
  it("unlocks by peak tiers and grade", () => {
    const facts = { survivalPeak: "SILVER" as const, soloPeak: "GOLD" as const, totalAp: 1240 };
    expect(eliminationEffectUnlocked("DEFAULT", facts)).toBe(true);
    expect(eliminationEffectUnlocked("BOLT", facts)).toBe(true);
    expect(eliminationEffectUnlocked("SLICE", facts)).toBe(false);
    expect(eliminationEffectUnlocked("SLICE", { ...facts, survivalPeak: "GOLD" })).toBe(true);
    expect(eliminationEffectUnlocked("SLICE", { ...facts, survivalPeak: "GOLD", soloPeak: "SILVER" })).toBe(false);
    expect(eliminationEffectUnlocked("SHATTER", facts)).toBe(true);
    expect(eliminationEffectUnlocked("VORTEX", facts)).toBe(false);
    expect(eliminationEffectUnlocked("BOLT", { ...facts, survivalPeak: null })).toBe(false);
  });
  it("folds bands and top tiers", () => {
    expect(highestBaseTier(["RED2", "SILVER1", null, "AQUA"])).toBe("SILVER");
    expect(highestBaseTier(["CHAMPION"])).toBe("GOLD");
    expect(highestBaseTier(["UNRANKED"])).toBe(null);
    expect(normalizeEliminationEffect("SLICE")).toBe("SLICE");
    expect(normalizeEliminationEffect("NOPE")).toBe("DEFAULT");
  });
});
