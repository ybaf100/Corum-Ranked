import { describe, expect, it } from "vitest";
import { SeededRandom } from "@corum-ranked/rules";
import type { DebugBotDifficultyEnvironment } from "../src/config/server-environment.js";
import {
  createDebugBotAttemptPlan,
  estimateDebugBotMapLengthSeconds,
  parseDebugBotMapDifficulty,
} from "../src/debug-bot/debug-bot.behavior.js";

const normal: DebugBotDifficultyEnvironment = {
  mmrOffset: 0,
  qualifyingChance: 0.30,
  clearChance: 0.015,
  paceMultiplier: 1,
};

describe("humanized Debug Bot attempt model", () => {
  it("parses the live 1..21+ map difficulty format", () => {
    expect(parseDebugBotMapDifficulty("1")).toBe(1);
    expect(parseDebugBotMapDifficulty("21+")).toBe(21);
    expect(parseDebugBotMapDifficulty("Difficulty 18")).toBe(18);
    expect(parseDebugBotMapDifficulty("legacy text")).toBe(11);
  });

  it("uses map length labels and exact durations", () => {
    expect(estimateDebugBotMapLengthSeconds("Tiny")).toBe(22);
    expect(estimateDebugBotMapLengthSeconds("Long")).toBe(88);
    expect(estimateDebugBotMapLengthSeconds("1:30")).toBe(90);
    expect(estimateDebugBotMapLengthSeconds(75)).toBe(75);
  });

  it("makes harder and longer maps less likely to produce deep runs", () => {
    const easier = createDebugBotAttemptPlan({
      qualifyingPercent: 50,
      botHiddenMmr: 2_500,
      map: { difficulty: "7", length: "Short" },
      profile: normal,
      syntheticScenario: false,
    }, new SeededRandom(1));
    const harder = createDebugBotAttemptPlan({
      qualifyingPercent: 50,
      botHiddenMmr: 2_500,
      map: { difficulty: "21+", length: "Long" },
      profile: normal,
      syntheticScenario: false,
    }, new SeededRandom(1));

    expect(easier.effectiveQualifyingChance).toBeGreaterThan(harder.effectiveQualifyingChance);
    expect(easier.effectiveClearChance).toBeGreaterThan(harder.effectiveClearChance);
  });

  it("uses Hidden MMR as a skill input on the same map", () => {
    const low = createDebugBotAttemptPlan({
      qualifyingPercent: 50,
      botHiddenMmr: 800,
      map: { difficulty: "12", length: "Medium" },
      profile: normal,
      syntheticScenario: false,
    }, new SeededRandom(2));
    const high = createDebugBotAttemptPlan({
      qualifyingPercent: 50,
      botHiddenMmr: 3_600,
      map: { difficulty: "12", length: "Medium" },
      profile: normal,
      syntheticScenario: false,
    }, new SeededRandom(2));

    expect(high.effectiveQualifyingChance).toBeGreaterThan(low.effectiveQualifyingChance);
    expect(high.effectiveClearChance).toBeGreaterThan(low.effectiveClearChance);
  });

  it("paces normal attempts from the map duration", () => {
    const plan = createDebugBotAttemptPlan({
      qualifyingPercent: 50,
      botHiddenMmr: 2_500,
      map: { difficulty: "11", length: "Long" },
      profile: normal,
      syntheticScenario: false,
    }, new SeededRandom(44));
    expect(plan.durationMs).toBeGreaterThanOrEqual(900);
    expect(plan.restartDelayMs).toBeGreaterThanOrEqual(650);
  });

  it("keeps FORCE/TRIGGER scenarios intentionally fast", () => {
    const plan = createDebugBotAttemptPlan({
      qualifyingPercent: 50,
      botHiddenMmr: 2_500,
      map: { difficulty: "21+", length: "XL" },
      profile: normal,
      syntheticScenario: true,
    }, new SeededRandom(7));
    expect(plan.durationMs).toBeLessThanOrEqual(4_800);
  });
});
