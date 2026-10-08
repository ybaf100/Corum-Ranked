import { describe, expect, it } from "vitest";
import {
  calculateMmrUpdate,
  updateRankedRatingProfile,
  type RankedRatingProfile,
} from "../src/index.js";
import { completeOperationalConfig, tierBandsFixture } from "./fixtures.js";

const policy = completeOperationalConfig().mmrPolicy!;

const profile = (overrides: Partial<RankedRatingProfile> = {}): RankedRatingProfile => ({
  hiddenMmr: 500,
  visibleRankedScore: 0,
  placementGames: policy.placementGames,
  placementResults: [],
  displayedTier: "RED",
  resultStreak: 0,
  promotion: null,
  demotionWarning: false,
  ...overrides,
});

describe("v0.5 placement and LP", () => {
  it("keeps LP hidden at zero and records O/X outcomes during placement", () => {
    const result = updateRankedRatingProfile(
      profile({ placementGames: 0, displayedTier: "UNRANKED" }),
      "A",
      "A",
      560,
      policy,
      tierBandsFixture,
    );
    expect(result.visibleScoreDelta).toBeNull();
    expect(result.profile).toMatchObject({
      placementGames: 1,
      placementResults: ["WIN"],
      visibleRankedScore: 0,
      displayedTier: "UNRANKED",
    });
  });

  it("finishes placement below an inclusive cutoff safety zone and bypasses promotion", () => {
    const result = updateRankedRatingProfile(
      profile({
        hiddenMmr: 990,
        placementGames: policy.placementGames - 1,
        placementResults: ["WIN", "LOSS", "WIN", "WIN"],
        displayedTier: "UNRANKED",
      }),
      "A",
      "A",
      1_015,
      policy,
      tierBandsFixture,
    );
    expect(result.placementCompleted).toBe(true);
    expect(result.promotionStarted).toBe(false);
    expect(result.profile.visibleRankedScore).toBe(984);
    expect(result.profile.displayedTier).toBe("RED");
    expect(result.profile.promotion).toBeNull();
    expect(result.profile.placementResults).toEqual(["WIN", "LOSS", "WIN", "WIN", "WIN"]);
  });

  it("uses hidden MMR to accelerate gains and soften losses without exposing it as LP", () => {
    const above = profile({ hiddenMmr: 700, visibleRankedScore: 500 });
    const below = profile({ hiddenMmr: 300, visibleRankedScore: 500 });
    const aboveWin = updateRankedRatingProfile(above, "A", "A", 700, policy, tierBandsFixture);
    const aboveLoss = updateRankedRatingProfile(above, "A", "B", 700, policy, tierBandsFixture);
    const belowWin = updateRankedRatingProfile(below, "A", "A", 300, policy, tierBandsFixture);
    const belowLoss = updateRankedRatingProfile(below, "A", "B", 300, policy, tierBandsFixture);
    expect(aboveWin.visibleScoreDelta).toBe(38);
    expect(aboveLoss.visibleScoreDelta).toBe(-13);
    expect(belowWin.visibleScoreDelta).toBe(13);
    expect(belowLoss.visibleScoreDelta).toBe(-38);
  });

  it("weights LP movement by stronger and weaker opponents", () => {
    const before = profile({ hiddenMmr: 1_000, visibleRankedScore: 500 });
    const beatStronger = updateRankedRatingProfile(
      before, "A", "A", 1_030, policy, tierBandsFixture,
      { opponentHiddenMmr: 1_500 },
    );
    const beatWeaker = updateRankedRatingProfile(
      before, "A", "A", 1_003, policy, tierBandsFixture,
      { opponentHiddenMmr: 500 },
    );
    const loseToStronger = updateRankedRatingProfile(
      before, "A", "B", 997, policy, tierBandsFixture,
      { opponentHiddenMmr: 1_500 },
    );
    const loseToWeaker = updateRankedRatingProfile(
      before, "A", "B", 970, policy, tierBandsFixture,
      { opponentHiddenMmr: 500 },
    );
    expect(beatStronger.visibleScoreDelta).toBeGreaterThan(beatWeaker.visibleScoreDelta!);
    expect(Math.abs(loseToStronger.visibleScoreDelta!)).toBeLessThan(
      Math.abs(loseToWeaker.visibleScoreDelta!),
    );

    const ignoreOpponent = { ...policy, visibleScoreOpponentStrengthWeight: 0 };
    const ignoredStrong = updateRankedRatingProfile(
      before, "A", "A", 1_016, ignoreOpponent, tierBandsFixture,
      { opponentHiddenMmr: 1_500 },
    );
    const ignoredWeak = updateRankedRatingProfile(
      before, "A", "A", 1_016, ignoreOpponent, tierBandsFixture,
      { opponentHiddenMmr: 500 },
    );
    expect(ignoredStrong.visibleScoreDelta).toBe(ignoredWeak.visibleScoreDelta);
  });

  it("uses the existing LP maximum for a PERFECT WIN", () => {
    const before = profile({ hiddenMmr: 1_000, visibleRankedScore: 500 });
    const normal = updateRankedRatingProfile(
      before, "A", "A", 1_003, policy, tierBandsFixture,
      { opponentHiddenMmr: 500 },
    );
    const perfect = updateRankedRatingProfile(
      before, "A", "A", 1_068, policy, tierBandsFixture,
      { opponentHiddenMmr: 500, maximumVisibleScoreGain: true },
    );
    expect(normal.visibleScoreDelta).toBe(13);
    expect(perfect.visibleScoreDelta).toBe(38);
  });
});

describe("v0.5 promotion and demotion", () => {
  it("caps at cutoff-1, does not count the trigger match, and awards cutoff+50", () => {
    const triggered = updateRankedRatingProfile(
      profile({ hiddenMmr: 1_000, visibleRankedScore: 980 }),
      "A",
      "A",
      1_020,
      policy,
      tierBandsFixture,
    );
    expect(triggered.promotionStarted).toBe(true);
    expect(triggered.profile.visibleRankedScore).toBe(999);
    expect(triggered.profile.promotion?.results).toEqual([]);

    const firstWin = updateRankedRatingProfile(
      triggered.profile,
      "A",
      "A",
      1_040,
      policy,
      tierBandsFixture,
    );
    expect(firstWin.profile.promotion?.results).toEqual(["WIN"]);
    const promoted = updateRankedRatingProfile(
      firstWin.profile,
      "A",
      "A",
      1_060,
      policy,
      tierBandsFixture,
    );
    expect(promoted.promotionSucceeded).toBe(true);
    expect(promoted.profile).toMatchObject({
      visibleRankedScore: 1_050,
      displayedTier: "AQUA",
      promotion: null,
    });
  });

  it("fails a best-of-three promotion after two losses and returns cutoff-50", () => {
    const inSeries = profile({
      hiddenMmr: 990,
      visibleRankedScore: 999,
      promotion: {
        sourceTier: "RED",
        targetTier: "AQUA",
        cutoff: 1_000,
        gamesRequired: 3,
        winsRequired: 2,
        results: ["LOSS"],
      },
    });
    const failed = updateRankedRatingProfile(inSeries, "A", "B", 970, policy, tierBandsFixture);
    expect(failed.promotionFailed).toBe(true);
    expect(failed.profile).toMatchObject({
      visibleRankedScore: 950,
      displayedTier: "RED",
      promotion: null,
    });
  });

  it("warns at the exact tier floor, then demotes on the next loss", () => {
    const warned = updateRankedRatingProfile(
      profile({ hiddenMmr: 900, visibleRankedScore: 1_033, displayedTier: "AQUA" }),
      "A",
      "B",
      900,
      policy,
      tierBandsFixture,
    );
    expect(warned.profile).toMatchObject({ visibleRankedScore: 1_000, demotionWarning: true });
    const demoted = updateRankedRatingProfile(
      warned.profile,
      "A",
      "B",
      880,
      policy,
      tierBandsFixture,
    );
    expect(demoted.demoted).toBe(true);
    expect(demoted.profile).toMatchObject({
      visibleRankedScore: 950,
      displayedTier: "RED",
      demotionWarning: false,
    });
  });

  it("protects the tier floor without a warning while hidden MMR is above it", () => {
    const protectedFloor = updateRankedRatingProfile(
      profile({ hiddenMmr: 1_050, visibleRankedScore: 1_000, displayedTier: "AQUA" }),
      "A",
      "B",
      1_025,
      policy,
      tierBandsFixture,
    );
    expect(protectedFloor.profile).toMatchObject({
      visibleRankedScore: 1_000,
      displayedTier: "AQUA",
      demotionWarning: false,
    });
  });
});

describe("v0.5 hidden MMR performance weighting", () => {
  it("configures how strongly opponent MMR affects movement", () => {
    const base = {
      ratingA: 1_000,
      placementGamesA: 5,
      placementGamesB: 5,
      winner: "A" as const,
    };
    const strong = calculateMmrUpdate({ ...base, ratingB: 1_500 }, policy);
    const weak = calculateMmrUpdate({ ...base, ratingB: 500 }, policy);
    expect(strong.deltaA).toBeGreaterThan(weak.deltaA);

    const ignoreOpponent = { ...policy, mmrOpponentStrengthWeight: 0 };
    expect(calculateMmrUpdate({ ...base, ratingB: 1_500 }, ignoreOpponent).deltaA).toBe(
      calculateMmrUpdate({ ...base, ratingB: 500 }, ignoreOpponent).deltaA,
    );
  });

  it("awards the theoretical existing-formula maximum only to the PERFECT WIN winner", () => {
    const regular = calculateMmrUpdate({
      ratingA: 1_000,
      ratingB: 1_000,
      placementGamesA: 5,
      placementGamesB: 5,
      winner: "A",
      maximumGainSide: "A",
    }, policy);
    const placement = calculateMmrUpdate({
      ratingA: 1_000,
      ratingB: 1_000,
      placementGamesA: 0,
      placementGamesB: 5,
      winner: "A",
      maximumGainSide: "A",
    }, policy);
    expect(regular.deltaA).toBe(68);
    expect(regular.deltaB).toBe(-16);
    expect(placement.deltaA).toBe(136);
    expect(() => calculateMmrUpdate({
      ratingA: 1_000,
      ratingB: 1_000,
      placementGamesA: 5,
      placementGamesB: 5,
      winner: "A",
      maximumGainSide: "B",
    }, policy)).toThrow(/match winner/);
  });
  it("moves more for decisive margins and less when match outcome disagrees with score/clears", () => {
    const close = calculateMmrUpdate({
      ratingA: 1_000,
      ratingB: 1_000,
      placementGamesA: 5,
      placementGamesB: 5,
      winner: "A",
      scoreA: 500,
      scoreB: 490,
      clearsA: 3,
      clearsB: 2,
    }, policy);
    const decisive = calculateMmrUpdate({
      ratingA: 1_000,
      ratingB: 1_000,
      placementGamesA: 5,
      placementGamesB: 5,
      winner: "A",
      scoreA: 750,
      scoreB: 100,
      clearsA: 6,
      clearsB: 0,
    }, policy);
    const disagreement = calculateMmrUpdate({
      ratingA: 1_000,
      ratingB: 1_000,
      placementGamesA: 5,
      placementGamesB: 5,
      winner: "A",
      scoreA: 450,
      scoreB: 600,
      clearsA: 4,
      clearsB: 5,
    }, policy);
    expect(decisive.deltaA).toBeGreaterThan(close.deltaA);
    expect(disagreement.outcomeDisagreement).toBe(true);
    expect(disagreement.deltaA).toBeLessThan(close.deltaA);
  });

  it("never turns a score/Clear disagreement into a margin bonus", () => {
    const disagreement = calculateMmrUpdate({
      ratingA: 1_000,
      ratingB: 1_000,
      placementGamesA: 5,
      placementGamesB: 5,
      winner: "A",
      scoreA: 1_500,
      scoreB: 100,
      clearsA: 0,
      clearsB: 6,
    }, policy);
    expect(disagreement.outcomeDisagreement).toBe(true);
    expect(disagreement.performanceFactor).toBeLessThanOrEqual(1);
  });

  it("increases movement for a continuing two-plus streak with a cap", () => {
    const neutral = calculateMmrUpdate({
      ratingA: 1_000,
      ratingB: 1_000,
      placementGamesA: 5,
      placementGamesB: 5,
      winner: "A",
      scoreA: 500,
      scoreB: 450,
      clearsA: 3,
      clearsB: 2,
      resultStreakA: 0,
    }, policy);
    const streak = calculateMmrUpdate({
      ratingA: 1_000,
      ratingB: 1_000,
      placementGamesA: 5,
      placementGamesB: 5,
      winner: "A",
      scoreA: 500,
      scoreB: 450,
      clearsA: 3,
      clearsB: 2,
      resultStreakA: 4,
    }, policy);
    expect(streak.streakFactorA).toBeGreaterThan(1);
    expect(streak.deltaA).toBeGreaterThan(neutral.deltaA);
  });
});
