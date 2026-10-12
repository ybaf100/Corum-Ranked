import { describe, expect, it } from "vitest";
import { DEFAULT_SURVIVAL_CONFIG, survivalBotPlan, survivalBotTiebreak, survivalDebugLp } from "../src/index.js";

describe("Survival debug simulations", () => {
  it("reproduces every draw and keeps five monotone integer records within bounds", () => {
    for (let seed = 1; seed < 100; seed++) for (let bot = 0; bot < 15; bot++) {
      const plan = survivalBotPlan(seed, "stage-1:1", bot);
      expect(plan).toEqual(survivalBotPlan(seed, "stage-1:1", bot));
      expect(plan).toHaveLength(5);
      plan.forEach((event, index) => {
        expect(Number.isInteger(event.best)).toBe(true);
        expect(event.best).toBeGreaterThanOrEqual(index ? plan[index - 1]!.best : 0);
        expect(event.best).toBeLessThanOrEqual(100);
        if (index) {
          expect(event.offsetMs).toBeGreaterThan(plan[index - 1]!.offsetMs);
          if (!event.changed) expect(event.best).toBe(plan[index - 1]!.best);
        }
      });
    }
  });
  it("has distinct timing for all bots and both equally likely change branches", () => {
    const outcomes = { changed: 0, held: 0 };
    for (let seed = 1; seed <= 1000; seed++) {
      const plans = Array.from({ length: 15 }, (_, bot) => survivalBotPlan(seed, "round", bot));
      for (let event = 1; event <= 4; event++) {
        const offsets = plans.map(p => p[event]!.offsetMs);
        expect(new Set(offsets).size).toBe(15);
        offsets.forEach(n => expect(Math.abs(n - [0, 40000, 90000, 120000, 150000][event]!)).toBeLessThanOrEqual(5000));
      }
      plans.flatMap(p => p.slice(1)).forEach(e => e.changed ? outcomes.changed++ : outcomes.held++);
    }
    expect(outcomes.changed / (outcomes.changed + outcomes.held)).toBeGreaterThan(0.48);
    expect(outcomes.changed / (outcomes.changed + outcomes.held)).toBeLessThan(0.52);
  });
  it("generates three independent precise Death Match records immediately", () => {
    const records = survivalBotTiebreak(42, "dm-1", 4);
    expect(records).toHaveLength(3);
    expect(records).toEqual(survivalBotTiebreak(42, "dm-1", 4));
    records.forEach(n => { expect(n).toBeGreaterThanOrEqual(0); expect(n).toBeLessThanOrEqual(100); });
    expect(records).not.toEqual(survivalBotTiebreak(42, "dm-2", 4));
  });
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
