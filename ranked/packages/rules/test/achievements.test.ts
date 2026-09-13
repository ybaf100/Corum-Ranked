import { describe, expect, it } from "vitest";
import {
  EMPTY_ACHIEVEMENT_METRICS,
  ONE_SHOT_ACHIEVEMENTS,
  PROGRESSIVE_ACHIEVEMENTS,
  evaluateAchievementMatch,
  isReverseSweep,
  isSweepWin,
  progressiveLevelForMetric,
  progressiveProgressPresentation,
} from "../src/achievements.js";

const definition = (id: string) => {
  const value = PROGRESSIVE_ACHIEVEMENTS.find((candidate) => candidate.id === id);
  if (!value) throw new Error(`Missing definition ${id}`);
  return value;
};

const facts = (overrides: Partial<Parameters<typeof evaluateAchievementMatch>[1]> = {}) => ({
  won: false,
  rankedClears: 0,
  perfectWin: false,
  lastAttemptClears: 0,
  reverseSweep: false,
  sweepWin: false,
  promotionSucceeded: false,
  reachedGold: false,
  deathmatches: [],
  ...overrides,
});

describe("achievement configuration and progression", () => {
  it("uses the exact I-V thresholds and independently configured AP", () => {
    expect(PROGRESSIVE_ACHIEVEMENTS).toHaveLength(10);
    expect(definition("ranked_wins").levels.map((level) => level.threshold)).toEqual([1, 10, 50, 100, 500]);
    expect(definition("ranked_matches").levels.map((level) => level.threshold)).toEqual([10, 50, 100, 500, 1_000]);
    expect(definition("ranked_clears").levels.map((level) => level.threshold)).toEqual([1, 10, 100, 500, 1_000]);
    expect(definition("perfect_wins").levels.map((level) => level.threshold)).toEqual([1, 5, 10, 25, 50]);
    expect(definition("win_streak").levels.map((level) => level.threshold)).toEqual([3, 5, 10, 15, 20]);
    expect(definition("deathmatch_wins").levels.map((level) => level.threshold)).toEqual([1, 5, 10, 25, 50]);
    expect(definition("last_attempt_clears").levels.map((level) => level.threshold)).toEqual([1, 3, 10, 25, 50]);
    expect(definition("reverse_sweeps").levels.map((level) => level.threshold)).toEqual([1, 3, 10, 25, 50]);
    expect(definition("sweep_wins").levels.map((level) => level.threshold)).toEqual([1, 5, 20, 50, 100]);
    expect(definition("promotion_wins").levels.map((level) => level.threshold)).toEqual([1, 2, 3, 5, 10]);
    for (const item of PROGRESSIVE_ACHIEVEMENTS) {
      expect(item.levels.map((level) => level.ap)).toEqual([5, 10, 20, 40, 75]);
      expect(item.levels.map((level) => level.displayLabel)).toEqual(["I", "II", "III", "IV", "V"]);
    }
  });

  it("defines every one-shot name, AP value, and rarity in the shared config", () => {
    expect(ONE_SHOT_ACHIEVEMENTS.map(({ id, nameKo, ap, rarity }) => ({ id, nameKo, ap, rarity })))
      .toEqual([
        { id: "reverse_sweep_first", nameKo: "뒤집었다", ap: 25, rarity: "RARE" },
        { id: "last_attempt_match_win", nameKo: "아직 끝나지 않았다", ap: 25, rarity: "RARE" },
        { id: "deathmatch_tie", nameKo: "다시 한 번", ap: 25, rarity: "RARE" },
        { id: "deathmatch_close_win", nameKo: "간발의 차", ap: 25, rarity: "RARE" },
        { id: "deathmatch_pixel_win", nameKo: "한 끗 차이", ap: 50, rarity: "EPIC" },
        { id: "deathmatch_domination", nameKo: "압도적", ap: 25, rarity: "RARE" },
        { id: "deathmatch_hundred_win", nameKo: "완벽한 결투", ap: 50, rarity: "EPIC" },
        { id: "deathmatch_double_hundred", nameKo: "양보는 없다", ap: 100, rarity: "LEGENDARY" },
        { id: "gold_reached", nameKo: "정상", ap: 50, rarity: "EPIC" },
        { id: "perfect_win_streak_3", nameKo: "무결점", ap: 100, rarity: "LEGENDARY" },
        { id: "placement_complete", nameKo: "첫 걸음", ap: 10, rarity: "COMMON" },
      ]);
  });

  it("unlocks I and multiple crossed levels without creating a sixth reward", () => {
    const wins = definition("ranked_wins");
    expect(progressiveLevelForMetric(wins, 1)).toBe(1);
    expect(progressiveLevelForMetric(wins, 73)).toBe(3);
    expect(progressiveLevelForMetric(wins, 500)).toBe(5);
    expect(progressiveLevelForMetric(wins, 812)).toBe(5);
  });

  it("keeps maxed gauge full while preserving the live metric", () => {
    expect(progressiveProgressPresentation(definition("ranked_wins"), 812)).toEqual({
      level: 5,
      displayLabel: "V",
      metricValue: 812,
      nextThreshold: null,
      gauge: 1,
      maxed: true,
    });
  });

  it("keeps historical best streak after a loss", () => {
    const prior = { ...EMPTY_ACHIEVEMENT_METRICS, current_win_streak: 8, win_streak: 8 };
    const result = evaluateAchievementMatch(prior, facts({ won: false }));
    expect(result.metrics.current_win_streak).toBe(0);
    expect(result.metrics.win_streak).toBe(8);
  });
});

describe("achievement match conditions", () => {
  it("recognizes only an exact 0-1 to 2-1 reverse sweep and an exact 2-0 sweep", () => {
    expect(isReverseSweep(["B", "A", "A"], "A")).toBe(true);
    expect(isReverseSweep(["DRAW", "A", "A"], "A")).toBe(false);
    expect(isReverseSweep(["A", "B", "A"], "A")).toBe(false);
    expect(isSweepWin(["A", "A"], "A")).toBe(true);
    expect(isSweepWin(["A", "B"], "A")).toBe(false);
  });

  it("counts perfect, reverse sweep, sweep, last attempt, promotion and DM wins", () => {
    const result = evaluateAchievementMatch(EMPTY_ACHIEVEMENT_METRICS, facts({
      won: true,
      rankedClears: 7,
      perfectWin: true,
      lastAttemptClears: 1,
      reverseSweep: true,
      sweepWin: true,
      promotionSucceeded: true,
      reachedGold: true,
      deathmatches: [{ outcome: "WIN", ownBest: 80, opponentBest: 79.5 }],
    }));
    expect(result.metrics).toMatchObject({
      ranked_wins: 1, ranked_matches: 1, ranked_clears: 7, perfect_wins: 1,
      win_streak: 1, deathmatch_wins: 1, last_attempt_clears: 1,
      reverse_sweeps: 1, sweep_wins: 1, promotion_wins: 1,
    });
    expect(result.oneShots).toEqual(expect.arrayContaining([
      "reverse_sweep_first", "last_attempt_match_win", "gold_reached", "deathmatch_close_win",
    ]));
  });

  it("uses strict close thresholds and inclusive domination threshold", () => {
    const result = evaluateAchievementMatch(EMPTY_ACHIEVEMENT_METRICS, facts({
      deathmatches: [
        { outcome: "WIN", ownBest: 90, opponentBest: 89.01 },
        { outcome: "WIN", ownBest: 80, opponentBest: 79.901 },
        { outcome: "WIN", ownBest: 70, opponentBest: 20 },
      ],
    }));
    expect(result.oneShots).toContain("deathmatch_close_win");
    expect(result.oneShots).toContain("deathmatch_pixel_win");
    expect(result.oneShots).toContain("deathmatch_domination");

    const exactOne = evaluateAchievementMatch(EMPTY_ACHIEVEMENT_METRICS, facts({
      deathmatches: [{ outcome: "WIN", ownBest: 80, opponentBest: 79 }],
    }));
    expect(exactOne.oneShots).not.toContain("deathmatch_close_win");

    const exactPointOne = evaluateAchievementMatch(EMPTY_ACHIEVEMENT_METRICS, facts({
      deathmatches: [{ outcome: "WIN", ownBest: 80, opponentBest: 79.9 }],
    }));
    expect(exactPointOne.oneShots).toContain("deathmatch_close_win");
    expect(exactPointOne.oneShots).not.toContain("deathmatch_pixel_win");
  });

  it("unlocks 100 win and double-100 tie independently", () => {
    const result = evaluateAchievementMatch(EMPTY_ACHIEVEMENT_METRICS, facts({
      deathmatches: [
        { outcome: "WIN", ownBest: 100, opponentBest: 99.5 },
        { outcome: "TIE", ownBest: 100, opponentBest: 100 },
      ],
    }));
    expect(result.oneShots).toEqual(expect.arrayContaining([
      "deathmatch_hundred_win", "deathmatch_tie", "deathmatch_double_hundred",
    ]));
  });

  it("unlocks the legendary perfect-win streak at three", () => {
    const prior = { ...EMPTY_ACHIEVEMENT_METRICS, current_perfect_win_streak: 2 };
    const result = evaluateAchievementMatch(prior, facts({ won: true, perfectWin: true }));
    expect(result.oneShots).toContain("perfect_win_streak_3");
  });
});
