import { describe, expect, it } from "vitest";
import { DEFAULT_SURVIVAL_CONFIG, survivalDebugLp } from "../src/index.js";

describe("Survival debug simulations", () => {
  it("builds every requested average without changing account LP, including virtual fallback", () => {
    for (const group of ["RED_BRONZE", "SILVER", "GOLD"] as const) for (const lp of [0, 150, 1200, 20000, 2147483647]) {
      const result = survivalDebugLp(123, lp, group, DEFAULT_SURVIVAL_CONFIG);
      expect(result.bots).toHaveLength(15);
      expect(result.bots.every(n => Number.isSafeInteger(n) && n >= 0)).toBe(true);
      expect(result.averageLp).toBe((result.humanSnapshotLp + result.bots.reduce((a,b) => a+b, 0)) / 16);
      if (group === "RED_BRONZE") expect(result.averageLp).toBeLessThan(900);
      if (group === "SILVER") { expect(result.averageLp).toBeGreaterThanOrEqual(900); expect(result.averageLp).toBeLessThan(1200); }
      if (group === "GOLD") expect(result.averageLp).toBeGreaterThanOrEqual(1200);
    }
  });
  it("uses configured group boundaries even with divided tiers", () => {
    const config = { ...DEFAULT_SURVIVAL_CONFIG, tierBands: DEFAULT_SURVIVAL_CONFIG.tierBands.map(b => ({...b,
      minimumLp: b.minimumLp * 2})) };
    expect(survivalDebugLp(1, 150, "SILVER", config).averageLp).toBe(2100);
  });
});
