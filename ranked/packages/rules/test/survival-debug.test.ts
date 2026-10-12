import { describe, expect, it } from "vitest";
import { DEFAULT_SURVIVAL_CONFIG, survivalBotPlan, survivalBotTiebreak, survivalDebugLp } from "../src/index.js";

describe("Survival debug simulations", () => {
  it("reproduces every draw and keeps six monotone integer records within bounds", () => {
    for (let seed = 1; seed < 100; seed++) for (let bot = 0; bot < 15; bot++) {
      const plan = survivalBotPlan(seed, "stage-1:1", bot);
      expect(plan).toEqual(survivalBotPlan(seed, "stage-1:1", bot));
      expect(plan).toHaveLength(6);
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
  it("uses exact five update times and a 40% candidate-draw probability", () => {
    const outcomes = { changed: 0, held: 0 };
    for (let seed = 1; seed <= 1000; seed++) {
      const plans = Array.from({ length: 15 }, (_, bot) => survivalBotPlan(seed, "round", bot));
      for (const plan of plans) {
        expect(plan.map(e => e.offsetMs)).toEqual([0, 20000, 50000, 80000, 110000, 140000]);
        plan.slice(1).forEach(e => e.changed ? outcomes.changed++ : outcomes.held++);
      }
    }
    const rate = outcomes.changed / (outcomes.changed + outcomes.held);
    expect(rate).toBeGreaterThan(0.39);
    expect(rate).toBeLessThan(0.41);
  });
  it("draws independent normal candidates with mean 50 and standard deviation 20, instead of climbing to 100", () => {
    const initial: number[] = [], final: number[] = [];
    for (let seed = 1; seed <= 2000; seed++) for (let bot = 0; bot < 15; bot++) {
      const plan = survivalBotPlan(seed, "normal", bot);
      initial.push(plan[0]!.best); final.push(plan.at(-1)!.best);
    }
    const mean = initial.reduce((a,b) => a+b, 0) / initial.length;
    const deviation = Math.sqrt(initial.reduce((a,b) => a+(b-mean)**2, 0) / initial.length);
    expect(mean).toBeGreaterThan(49); expect(mean).toBeLessThan(51);
    expect(deviation).toBeGreaterThan(19); expect(deviation).toBeLessThan(21);
    const finalMean = final.reduce((a,b) => a+b, 0) / final.length;
    expect(finalMean).toBeGreaterThan(58); expect(finalMean).toBeLessThan(72);
    expect(final.filter(n => n === 100).length / final.length).toBeLessThan(0.03);
    expect(survivalBotPlan(42,"normal",0)).not.toEqual(survivalBotPlan(42,"normal",1));
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
