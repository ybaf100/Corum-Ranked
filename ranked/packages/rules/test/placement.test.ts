import { describe, expect, it } from "vitest";
import {
  DEFAULT_PLACEMENT_POLICY,
  SeededRandom,
  calculatePlacementMapScore,
  calculatePlacementRating,
  calculatePlacementStageScore,
  finalizePlacementScore,
  placementGradeForMapScore,
  scorePlacementAttempt,
  selectPlacementReferenceMaps,
  selectPlacementStageMaps,
  type PlacementAttemptResult,
  type PlacementMapScore,
  type PlacementRatingMapResult,
} from "../src/index.js";
import { allPoolMaps, tierBandsFixture } from "./fixtures.js";

const policy = DEFAULT_PLACEMENT_POLICY;

const attempt = (
  attemptId: string,
  awardedScore: number,
  cleared = awardedScore === 250,
): PlacementAttemptResult => ({
  attemptId,
  progressPercent: cleared ? 100 : Math.min(99, awardedScore),
  cleared,
  valid: true,
  awardedScore,
});

const map = (mapScore: number): PlacementMapScore => ({
  mapScore,
  grade: placementGradeForMapScore(mapScore, policy),
  clearCount: 0,
  completedByClearTarget: false,
  topAttempts: [],
  validAttemptCount: 0,
});

describe("solo Stage Placement scoring", () => {
  it("reuses the existing qualifying, 50%, 75%, and Clear attempt score curve", () => {
    expect(scorePlacementAttempt("q", 49, false, 50).awardedScore).toBe(0);
    expect(scorePlacementAttempt("50", 50, false, 50).awardedScore).toBe(75);
    expect(scorePlacementAttempt("75", 75, false, 50).awardedScore).toBe(150);
    expect(scorePlacementAttempt("clear", 100, true, 50).awardedScore).toBe(250);
  });

  it("sums only the top two and replaces the lower prior top-two result", () => {
    const first = calculatePlacementMapScore([
      attempt("a", 70, false),
      attempt("b", 120, false),
      attempt("c", 180, false),
      attempt("d", 20, false),
    ], policy);
    expect(first.mapScore).toBe(300);
    expect(first.topAttempts.map((value) => value.attemptId)).toEqual(["c", "b"]);

    const replaced = calculatePlacementMapScore([
      attempt("a", 70, false),
      attempt("b", 120, false),
      attempt("c", 180, false),
      attempt("d", 200, false),
    ], policy);
    expect(replaced.mapScore).toBe(380);
    expect(replaced.topAttempts.map((value) => value.attemptId)).toEqual(["d", "c"]);
  });

  it("pads zero, one, and two attempts implicitly without failing", () => {
    expect(calculatePlacementMapScore([], policy).mapScore).toBe(0);
    expect(calculatePlacementMapScore([attempt("a", 75, false)], policy).mapScore).toBe(75);
    expect(calculatePlacementMapScore([
      attempt("a", 75, false),
      attempt("b", 150, false),
    ], policy).mapScore).toBe(225);
  });

  it("deduplicates an idempotent retry and rejects a conflicting retry", () => {
    const original = attempt("same", 150, false);
    expect(calculatePlacementMapScore([original, { ...original }], policy).mapScore).toBe(150);
    expect(() => calculatePlacementMapScore([
      original,
      { ...original, awardedScore: 151 },
    ], policy)).toThrow(/conflicts/);
  });

  it("finalizes two Clears at exactly 500 and forces S", () => {
    const score = calculatePlacementMapScore([
      attempt("c1", 250),
      attempt("c2", 250),
      attempt("lower", 150, false),
    ], policy);
    expect(score.mapScore).toBe(500);
    expect(score.grade).toBe("S");
    expect(score.completedByClearTarget).toBe(true);
  });
});

describe("solo Stage Placement progression", () => {
  it("averages two raw Map Scores", () => {
    const stage = calculatePlacementStageScore([map(400), map(300)], 3, policy);
    expect(stage.stageScore).toBe(350);
    expect(stage.grade).toBe("A");
  });

  it.each([
    [470, 470, 3, "S", 2, 5],
    [500, 351, 3, "S", 1, 4],
    [400, 360, 3, "A", 1, 4],
    [300, 300, 3, "B", 0, 3],
    [200, 200, 3, "C", -1, 2],
    [100, 100, 3, "D", -2, 1],
    [250, 0, 3, "D", -1, 2],
  ] as const)("moves Pool from the Stage and both Map Grades", (
    first,
    second,
    pool,
    grade,
    delta,
    nextPool,
  ) => {
    const result = calculatePlacementStageScore([map(first), map(second)], pool, policy);
    expect(result.grade).toBe(grade);
    expect(result.poolDelta).toBe(delta);
    expect(result.nextPool).toBe(nextPool);
  });

  it("clamps next Pool to 1 through 6", () => {
    expect(calculatePlacementStageScore([map(0), map(0)], 1, policy).nextPool).toBe(1);
    expect(calculatePlacementStageScore([map(500), map(500)], 6, policy).nextPool).toBe(6);
  });

  it("never duplicates a same-stage canonical map and prefers globally unused maps", () => {
    const maps = allPoolMaps();
    const first = selectPlacementStageMaps(3, maps, [], new SeededRandom(1));
    expect(first[0].canonicalLevelId).not.toBe(first[1].canonicalLevelId);
    const second = selectPlacementStageMaps(
      3,
      maps,
      first.map((entry) => entry.canonicalLevelId),
      new SeededRandom(2),
    );
    expect(new Set([...first, ...second].map((entry) => entry.canonicalLevelId)).size).toBe(4);
  });

  it("selects one deterministic median qualifying Reference Map per Pool", () => {
    const references = selectPlacementReferenceMaps(allPoolMaps());
    expect(references).toHaveLength(6);
    expect(references.map((entry) => entry.pool)).toEqual([1, 2, 3, 4, 5, 6]);
    expect(selectPlacementReferenceMaps(allPoolMaps())).toEqual(references);
  });
});

describe("solo Placement Rating and finalization", () => {
  const tenMaps = (score = 300, pool = 3): PlacementRatingMapResult[] =>
    Array.from({ length: 10 }, (_, index) => ({
      pool: pool as 1 | 2 | 3 | 4 | 5 | 6,
      mapScore: score,
      mapGrade: placementGradeForMapScore(score, policy),
      stageIndex: Math.floor(index / 2),
    }));

  it("uses every Map result, its Pool, and all Stage aggregates", () => {
    const baseline = calculatePlacementRating(tenMaps(), policy);
    const strongerLastMap = tenMaps();
    strongerLastMap[9] = {
      ...strongerLastMap[9]!,
      mapScore: 400,
      mapGrade: placementGradeForMapScore(400, policy),
    };
    const stronger = calculatePlacementRating(strongerLastMap, policy);
    expect(stronger.placementRating).toBeGreaterThan(baseline.placementRating);
    expect(stronger.mapComponent).toBeGreaterThan(baseline.mapComponent);
    expect(stronger.stageComponent).toBeGreaterThan(baseline.stageComponent);
    expect(stronger.overallComponent).toBeGreaterThan(baseline.overallComponent);
    expect(calculatePlacementRating(tenMaps(300, 4), policy).placementRating)
      .toBeGreaterThan(baseline.placementRating);
  });

  it("keeps the existing inclusive cutoff exclusion and downward cutoff-16 rule", () => {
    expect(finalizePlacementScore(1_015, tierBandsFixture).visibleRankedScore).toBe(984);
    expect(finalizePlacementScore(1_016, tierBandsFixture).visibleRankedScore).toBe(1_016);
    expect(finalizePlacementScore(-500, tierBandsFixture).visibleRankedScore).toBe(0);
  });
});

describe("Placement scoring", () => {
});
