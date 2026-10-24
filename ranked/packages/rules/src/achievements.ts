import { achievementText } from "./achievement-translations.generated.js";

export const ACHIEVEMENT_GRADES = [
  "BRONZE",
  "SILVER",
  "GOLD",
  "PLATINUM",
  "DIAMOND",
] as const;

export type AchievementGrade = (typeof ACHIEVEMENT_GRADES)[number];
export type AchievementLevel = 1 | 2 | 3 | 4 | 5;
export type AchievementScope = "SOLO_RANKED" | "SURVIVAL" | "SHARED";

export type AchievementRarity = "COMMON" | "RARE" | "EPIC" | "LEGENDARY" | "MYTHIC";

export interface AchievementLevelDefinition {
  readonly level: AchievementLevel;
  readonly grade: AchievementGrade;
  readonly displayLabel: "I" | "II" | "III" | "IV" | "V";
  readonly displayColor: string;
  readonly threshold: number;
  readonly ap: number;
}

export interface ProgressiveAchievementDefinition {
  readonly id: ProgressiveAchievementId;
  readonly type: "PROGRESSIVE";
  readonly scope: AchievementScope;
  readonly metric: ProgressiveAchievementId;
  readonly nameKo: string;
  readonly nameEn: string;
  readonly descriptionKo: string;
  readonly descriptionEn: string;
  readonly titleEnabled: boolean;
  readonly levels: readonly AchievementLevelDefinition[];
}

export interface OneShotAchievementDefinition {
  readonly id: OneShotAchievementId;
  readonly type: "ONE_SHOT";
  readonly scope: AchievementScope;
  readonly condition: OneShotAchievementId;
  readonly nameKo: string;
  readonly nameEn: string;
  readonly descriptionKo: string;
  readonly descriptionEn: string;
  readonly titleEnabled: boolean;
  readonly ap: number;
  readonly rarity: AchievementRarity;
  readonly hidden?: boolean;
  readonly hintKo?: string;
  readonly hintEn?: string;
}

export const PROGRESSIVE_ACHIEVEMENT_IDS = [
  "ranked_wins",
  "ranked_matches",
  "ranked_clears",
  "perfect_wins",
  "win_streak",
  "deathmatch_wins",
  "last_attempt_clears",
  "reverse_sweeps",
  "sweep_wins",
  "promotion_wins",
  "survival_matches",
  "survival_wins",
  "survival_podiums",
  "survival_finals",
  "survival_round_survives",
  "survival_round_wins",
  "survival_podium_streak",
  "shared_matches",
  "shared_wins",
  "shared_mastery",
  "shared_clears",
] as const;

export const ONE_SHOT_ACHIEVEMENT_IDS = [
  "reverse_sweep_first",
  "last_attempt_match_win",
  "deathmatch_tie",
  "deathmatch_close_win",
  "deathmatch_pixel_win",
  "deathmatch_domination",
  "deathmatch_hundred_win",
  "deathmatch_double_hundred",
  "gold_reached",
  "perfect_win_streak_3",
  "placement_complete",
  "survival_first_match",
  "survival_first_podium",
  "survival_first_win",
  "survival_cutoff_escape",
  "survival_round_perfect",
  "survival_clean_champion",
  "survival_final_perfect_win",
  "survival_underdog",
  "survival_deathmatch_entry",
  "survival_secret_1",
  "survival_true_champion",
  "survival_secret_2",
  "dual_mode_debut",
  "dual_mode_winner",
  "dual_gold_reached",
  "master_of_two_worlds",
] as const;

export type ProgressiveAchievementId = (typeof PROGRESSIVE_ACHIEVEMENT_IDS)[number];
export type OneShotAchievementId = (typeof ONE_SHOT_ACHIEVEMENT_IDS)[number];
export type AchievementId = ProgressiveAchievementId | OneShotAchievementId;

const LEVEL_PRESENTATION = [
  { level: 1, grade: "BRONZE", displayLabel: "I", displayColor: "#B2BAC9", ap: 5 },
  { level: 2, grade: "SILVER", displayLabel: "II", displayColor: "#57BBFF", ap: 10 },
  { level: 3, grade: "GOLD", displayLabel: "III", displayColor: "#C679FF", ap: 20 },
  { level: 4, grade: "PLATINUM", displayLabel: "IV", displayColor: "#FFB046", ap: 40 },
  { level: 5, grade: "DIAMOND", displayLabel: "V", displayColor: "#8DEBFF", ap: 75 },
] as const;

const levels = (thresholds: readonly [number, number, number, number, number]) =>
  LEVEL_PRESENTATION.map((presentation, index) => ({
    ...presentation,
    threshold: thresholds[index],
  })) as readonly AchievementLevelDefinition[];

export const PROGRESSIVE_ACHIEVEMENTS: readonly ProgressiveAchievementDefinition[] = [
  {
    id: "ranked_wins", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "ranked_wins",
    ...achievementText("ranked_wins"),
    titleEnabled: true, levels: levels([1, 10, 50, 100, 500]),
  },
  {
    id: "ranked_matches", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "ranked_matches",
    ...achievementText("ranked_matches"),
    titleEnabled: true, levels: levels([10, 50, 100, 500, 1_000]),
  },
  {
    id: "ranked_clears", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "ranked_clears",
    ...achievementText("ranked_clears"),
    titleEnabled: true, levels: levels([1, 10, 100, 500, 1_000]),
  },
  {
    id: "perfect_wins", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "perfect_wins",
    ...achievementText("perfect_wins"),
    titleEnabled: true, levels: levels([1, 5, 10, 25, 50]),
  },
  {
    id: "win_streak", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "win_streak",
    ...achievementText("win_streak"),
    titleEnabled: true, levels: levels([3, 5, 10, 15, 20]),
  },
  {
    id: "deathmatch_wins", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "deathmatch_wins",
    ...achievementText("deathmatch_wins"),
    titleEnabled: true, levels: levels([1, 5, 10, 25, 50]),
  },
  {
    id: "last_attempt_clears", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "last_attempt_clears",
    ...achievementText("last_attempt_clears"),
    titleEnabled: true, levels: levels([1, 3, 10, 25, 50]),
  },
  {
    id: "reverse_sweeps", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "reverse_sweeps",
    ...achievementText("reverse_sweeps"),
    titleEnabled: true, levels: levels([1, 3, 10, 25, 50]),
  },
  {
    id: "sweep_wins", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "sweep_wins",
    ...achievementText("sweep_wins"),
    titleEnabled: true, levels: levels([1, 5, 20, 50, 100]),
  },
  {
    id: "promotion_wins", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "promotion_wins",
    ...achievementText("promotion_wins"),
    titleEnabled: true, levels: levels([1, 2, 3, 5, 10]),
  },
  { id: "survival_matches", type: "PROGRESSIVE", scope: "SURVIVAL", metric: "survival_matches", ...achievementText("survival_matches"), titleEnabled: true, levels: levels([5, 25, 100, 250, 500]) },
  { id: "survival_wins", type: "PROGRESSIVE", scope: "SURVIVAL", metric: "survival_wins", ...achievementText("survival_wins"), titleEnabled: true, levels: levels([1, 3, 10, 25, 50]) },
  { id: "survival_podiums", type: "PROGRESSIVE", scope: "SURVIVAL", metric: "survival_podiums", ...achievementText("survival_podiums"), titleEnabled: true, levels: levels([1, 5, 20, 50, 100]) },
  { id: "survival_finals", type: "PROGRESSIVE", scope: "SURVIVAL", metric: "survival_finals", ...achievementText("survival_finals"), titleEnabled: true, levels: levels([1, 5, 20, 50, 100]) },
  { id: "survival_round_survives", type: "PROGRESSIVE", scope: "SURVIVAL", metric: "survival_round_survives", ...achievementText("survival_round_survives"), titleEnabled: true, levels: levels([10, 50, 200, 500, 1000]) },
  { id: "survival_round_wins", type: "PROGRESSIVE", scope: "SURVIVAL", metric: "survival_round_wins", ...achievementText("survival_round_wins"), titleEnabled: true, levels: levels([1, 10, 25, 50, 100]) },
  { id: "survival_podium_streak", type: "PROGRESSIVE", scope: "SURVIVAL", metric: "survival_podium_streak", ...achievementText("survival_podium_streak"), titleEnabled: true, levels: levels([2, 3, 5, 8, 12]) },
  { id: "shared_matches", type: "PROGRESSIVE", scope: "SHARED", metric: "shared_matches", ...achievementText("shared_matches"), titleEnabled: true, levels: levels([20, 100, 250, 500, 1000]) },
  { id: "shared_wins", type: "PROGRESSIVE", scope: "SHARED", metric: "shared_wins", ...achievementText("shared_wins"), titleEnabled: true, levels: levels([5, 25, 100, 250, 500]) },
  { id: "shared_mastery", type: "PROGRESSIVE", scope: "SHARED", metric: "shared_mastery", ...achievementText("shared_mastery"), titleEnabled: true, levels: levels([25, 100, 300, 700, 1500]) },
  { id: "shared_clears", type: "PROGRESSIVE", scope: "SHARED", metric: "shared_clears", ...achievementText("shared_clears"), titleEnabled: true, levels: levels([25, 100, 300, 700, 1500]) },
];

export const ONE_SHOT_ACHIEVEMENTS: readonly OneShotAchievementDefinition[] = [
  { id: "reverse_sweep_first", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "reverse_sweep_first", ...achievementText("reverse_sweep_first"), titleEnabled: true, ap: 25, rarity: "RARE" },
  { id: "last_attempt_match_win", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "last_attempt_match_win", ...achievementText("last_attempt_match_win"), titleEnabled: true, ap: 25, rarity: "RARE" },
  { id: "deathmatch_tie", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "deathmatch_tie", ...achievementText("deathmatch_tie"), titleEnabled: true, ap: 25, rarity: "RARE" },
  { id: "deathmatch_close_win", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "deathmatch_close_win", ...achievementText("deathmatch_close_win"), titleEnabled: true, ap: 25, rarity: "RARE" },
  { id: "deathmatch_pixel_win", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "deathmatch_pixel_win", ...achievementText("deathmatch_pixel_win"), titleEnabled: true, ap: 50, rarity: "EPIC" },
  { id: "deathmatch_domination", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "deathmatch_domination", ...achievementText("deathmatch_domination"), titleEnabled: true, ap: 25, rarity: "RARE" },
  { id: "deathmatch_hundred_win", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "deathmatch_hundred_win", ...achievementText("deathmatch_hundred_win"), titleEnabled: true, ap: 50, rarity: "EPIC" },
  { id: "deathmatch_double_hundred", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "deathmatch_double_hundred", ...achievementText("deathmatch_double_hundred"), titleEnabled: true, ap: 200, rarity: "MYTHIC" },
  { id: "gold_reached", type: "ONE_SHOT", scope: "SHARED", condition: "gold_reached", ...achievementText("gold_reached"), titleEnabled: true, ap: 50, rarity: "EPIC" },
  { id: "perfect_win_streak_3", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "perfect_win_streak_3", ...achievementText("perfect_win_streak_3"), titleEnabled: true, ap: 100, rarity: "LEGENDARY" },
  { id: "placement_complete", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "placement_complete", ...achievementText("placement_complete"), titleEnabled: true, ap: 10, rarity: "COMMON" },
  { id: "survival_first_match", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_first_match", ...achievementText("survival_first_match"), titleEnabled: true, ap: 10, rarity: "COMMON" },
  { id: "survival_first_podium", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_first_podium", ...achievementText("survival_first_podium"), titleEnabled: true, ap: 25, rarity: "RARE" },
  { id: "survival_first_win", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_first_win", ...achievementText("survival_first_win"), titleEnabled: true, ap: 50, rarity: "EPIC" },
  { id: "survival_cutoff_escape", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_cutoff_escape", ...achievementText("survival_cutoff_escape"), titleEnabled: true, ap: 10, rarity: "COMMON" },
  { id: "survival_round_perfect", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_round_perfect", ...achievementText("survival_round_perfect"), titleEnabled: true, ap: 25, rarity: "RARE" },
  { id: "survival_clean_champion", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_clean_champion", ...achievementText("survival_clean_champion"), titleEnabled: true, ap: 100, rarity: "LEGENDARY" },
  { id: "survival_final_perfect_win", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_final_perfect_win", ...achievementText("survival_final_perfect_win"), titleEnabled: true, ap: 100, rarity: "LEGENDARY" },
  { id: "survival_underdog", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_underdog", ...achievementText("survival_underdog"), titleEnabled: true, ap: 50, rarity: "EPIC" },
  { id: "survival_deathmatch_entry", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_deathmatch_entry", ...achievementText("survival_deathmatch_entry"), titleEnabled: true, ap: 50, rarity: "EPIC" },
  { id: "survival_secret_1", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_secret_1", ...achievementText("survival_secret_1"), titleEnabled: true, ap: 75, rarity: "EPIC", hidden: true },
  { id: "survival_true_champion", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_true_champion", ...achievementText("survival_true_champion"), titleEnabled: true, ap: 200, rarity: "MYTHIC" },
  { id: "survival_secret_2", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_secret_2", ...achievementText("survival_secret_2"), titleEnabled: true, ap: 200, rarity: "MYTHIC", hidden: true },
  { id: "dual_mode_debut", type: "ONE_SHOT", scope: "SHARED", condition: "dual_mode_debut", ...achievementText("dual_mode_debut"), titleEnabled: true, ap: 10, rarity: "COMMON" },
  { id: "dual_mode_winner", type: "ONE_SHOT", scope: "SHARED", condition: "dual_mode_winner", ...achievementText("dual_mode_winner"), titleEnabled: true, ap: 25, rarity: "RARE" },
  { id: "dual_gold_reached", type: "ONE_SHOT", scope: "SHARED", condition: "dual_gold_reached", ...achievementText("dual_gold_reached"), titleEnabled: true, ap: 100, rarity: "LEGENDARY" },
  { id: "master_of_two_worlds", type: "ONE_SHOT", scope: "SHARED", condition: "master_of_two_worlds", ...achievementText("master_of_two_worlds"), titleEnabled: true, ap: 200, rarity: "MYTHIC" },
];

export type AchievementDefinition =
  | ProgressiveAchievementDefinition
  | OneShotAchievementDefinition;

export const ACHIEVEMENT_DEFINITIONS: readonly AchievementDefinition[] = [
  ...PROGRESSIVE_ACHIEVEMENTS,
  ...ONE_SHOT_ACHIEVEMENTS,
];

export const achievementDefinition = (id: string): AchievementDefinition | undefined =>
  ACHIEVEMENT_DEFINITIONS.find((definition) => definition.id === id);

export const progressiveLevelForMetric = (
  definition: ProgressiveAchievementDefinition,
  metricValue: number,
): AchievementLevel | 0 => {
  let level: AchievementLevel | 0 = 0;
  for (const candidate of definition.levels) {
    if (metricValue >= candidate.threshold) level = candidate.level;
  }
  return level;
};

export interface AchievementProgressPresentation {
  readonly level: AchievementLevel | 0;
  readonly displayLabel: "I" | "II" | "III" | "IV" | "V" | null;
  readonly metricValue: number;
  readonly nextThreshold: number | null;
  readonly gauge: number;
  readonly maxed: boolean;
}

export const progressiveProgressPresentation = (
  definition: ProgressiveAchievementDefinition,
  rawMetricValue: number,
): AchievementProgressPresentation => {
  const metricValue = Math.max(0, Math.trunc(rawMetricValue));
  const level = progressiveLevelForMetric(definition, metricValue);
  if (level === 5) {
    return { level, displayLabel: "V", metricValue, nextThreshold: null, gauge: 1, maxed: true };
  }
  const next = definition.levels[level];
  if (!next) throw new Error(`Achievement ${definition.id} has no next level`);
  const start = level === 0 ? 0 : definition.levels[level - 1]?.threshold ?? 0;
  const span = Math.max(1, next.threshold - start);
  return {
    level,
    displayLabel: level === 0 ? null : definition.levels[level - 1]?.displayLabel ?? null,
    metricValue,
    nextThreshold: next.threshold,
    gauge: Math.max(0, Math.min(1, (metricValue - start) / span)),
    maxed: false,
  };
};

export interface AchievementMetrics {
  readonly ranked_wins: number;
  readonly ranked_matches: number;
  readonly ranked_clears: number;
  readonly perfect_wins: number;
  readonly current_win_streak: number;
  readonly win_streak: number;
  readonly deathmatch_wins: number;
  readonly last_attempt_clears: number;
  readonly reverse_sweeps: number;
  readonly sweep_wins: number;
  readonly promotion_wins: number;
  readonly current_perfect_win_streak: number;
}

export interface AchievementDeathmatchFact {
  readonly outcome: "WIN" | "LOSS" | "TIE";
  readonly ownBest: number;
  readonly opponentBest: number;
}

export interface AchievementMatchFacts {
  readonly won: boolean;
  readonly rankedClears: number;
  readonly perfectWin: boolean;
  readonly lastAttemptClears: number;
  readonly reverseSweep: boolean;
  readonly sweepWin: boolean;
  readonly promotionSucceeded: boolean;
  readonly reachedGold: boolean;
  readonly deathmatches: readonly AchievementDeathmatchFact[];
}

export interface AchievementMatchEvaluation {
  readonly metrics: AchievementMetrics;
  readonly oneShots: readonly OneShotAchievementId[];
}

export type AchievementSeriesSide = "A" | "B";

export const isReverseSweep = (
  roundResults: readonly string[],
  winnerSide: AchievementSeriesSide,
): boolean => {
  const opponentSide = winnerSide === "A" ? "B" : "A";
  return roundResults.length === 3 &&
    roundResults[0] === opponentSide &&
    roundResults[1] === winnerSide &&
    roundResults[2] === winnerSide;
};

export const isSweepWin = (
  roundResults: readonly string[],
  winnerSide: AchievementSeriesSide,
): boolean => roundResults.length === 2 && roundResults.every((result) => result === winnerSide);

export const EMPTY_ACHIEVEMENT_METRICS: AchievementMetrics = {
  ranked_wins: 0,
  ranked_matches: 0,
  ranked_clears: 0,
  perfect_wins: 0,
  current_win_streak: 0,
  win_streak: 0,
  deathmatch_wins: 0,
  last_attempt_clears: 0,
  reverse_sweeps: 0,
  sweep_wins: 0,
  promotion_wins: 0,
  current_perfect_win_streak: 0,
};

export const evaluateAchievementMatch = (
  current: AchievementMetrics,
  facts: AchievementMatchFacts,
): AchievementMatchEvaluation => {
  const currentWinStreak = facts.won ? current.current_win_streak + 1 : 0;
  const currentPerfectWinStreak = facts.perfectWin
    ? current.current_perfect_win_streak + 1
    : 0;
  const decisiveDeathmatchWin = facts.deathmatches.some((item) => item.outcome === "WIN");
  const metrics: AchievementMetrics = {
    ranked_wins: current.ranked_wins + (facts.won ? 1 : 0),
    ranked_matches: current.ranked_matches + 1,
    ranked_clears: current.ranked_clears + Math.max(0, facts.rankedClears),
    perfect_wins: current.perfect_wins + (facts.perfectWin ? 1 : 0),
    current_win_streak: currentWinStreak,
    win_streak: Math.max(current.win_streak, currentWinStreak),
    deathmatch_wins: current.deathmatch_wins + (decisiveDeathmatchWin ? 1 : 0),
    last_attempt_clears: current.last_attempt_clears + Math.max(0, facts.lastAttemptClears),
    reverse_sweeps: current.reverse_sweeps + (facts.reverseSweep ? 1 : 0),
    sweep_wins: current.sweep_wins + (facts.sweepWin ? 1 : 0),
    promotion_wins: current.promotion_wins + (facts.promotionSucceeded ? 1 : 0),
    current_perfect_win_streak: currentPerfectWinStreak,
  };

  const oneShots = new Set<OneShotAchievementId>();
  if (facts.reverseSweep) oneShots.add("reverse_sweep_first");
  if (facts.won && facts.lastAttemptClears > 0) oneShots.add("last_attempt_match_win");
  if (facts.reachedGold) oneShots.add("gold_reached");
  if (currentPerfectWinStreak >= 3) oneShots.add("perfect_win_streak_3");
  for (const deathmatch of facts.deathmatches) {
    const own = Math.max(0, Math.min(100, deathmatch.ownBest));
    const opponent = Math.max(0, Math.min(100, deathmatch.opponentBest));
    // Death Match scores are persisted with millipercent precision. Normalize
    // before strict boundary checks so binary floating-point representation does
    // not turn an exact 0.1%p result into a false "< 0.1%p" unlock.
    const difference = Math.round(Math.abs(own - opponent) * 1_000) / 1_000;
    if (deathmatch.outcome === "TIE") {
      oneShots.add("deathmatch_tie");
      if (own === 100 && opponent === 100) oneShots.add("deathmatch_double_hundred");
      continue;
    }
    if (deathmatch.outcome !== "WIN") continue;
    if (difference < 1) oneShots.add("deathmatch_close_win");
    if (difference < 0.1) oneShots.add("deathmatch_pixel_win");
    if (difference >= 50) oneShots.add("deathmatch_domination");
    if (own === 100) oneShots.add("deathmatch_hundred_win");
  }
  return { metrics, oneShots: [...oneShots] };
};

export const achievementScopeLabel = (scope: AchievementScope, language: "ko" | "en"): string => {
  const labels = { SOLO_RANKED: { ko: "솔로 랭크", en: "Solo Ranked" }, SURVIVAL: { ko: "서바이벌", en: "Survival" }, SHARED: { ko: "공통", en: "Shared" } };
  return labels[scope][language];
};
