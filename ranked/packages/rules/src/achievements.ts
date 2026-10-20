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
  "survival_win_streak",
  "shared_matches",
  "shared_wins",
  "shared_mastery",
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
  "survival_final_photo_finish",
  "survival_round_perfect",
  "survival_clean_champion",
  "survival_last_seed_champion",
  "survival_final_perfect_win",
  "survival_never_below_cutoff",
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
  { level: 5, grade: "DIAMOND", displayLabel: "V", displayColor: "#FFB8C6", ap: 75 },
] as const;

const levels = (thresholds: readonly [number, number, number, number, number]) =>
  LEVEL_PRESENTATION.map((presentation, index) => ({
    ...presentation,
    threshold: thresholds[index],
  })) as readonly AchievementLevelDefinition[];

export const PROGRESSIVE_ACHIEVEMENTS: readonly ProgressiveAchievementDefinition[] = [
  {
    id: "ranked_wins", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "ranked_wins",
    nameKo: "승리자", nameEn: "Winner",
    descriptionKo: "Solo Ranked에서 승리하세요.", descriptionEn: "In Solo Ranked: Win matches.",
    titleEnabled: true, levels: levels([1, 10, 50, 100, 500]),
  },
  {
    id: "ranked_matches", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "ranked_matches",
    nameKo: "경쟁자", nameEn: "Competitor",
    descriptionKo: "Solo Ranked에서 경기를 완료하세요.", descriptionEn: "In Solo Ranked: Complete matches.",
    titleEnabled: true, levels: levels([10, 50, 100, 500, 1_000]),
  },
  {
    id: "ranked_clears", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "ranked_clears",
    nameKo: "클리어러", nameEn: "Clearer",
    descriptionKo: "Solo Ranked에서 맵을 Clear하세요.", descriptionEn: "In Solo Ranked: Clear maps.",
    titleEnabled: true, levels: levels([1, 10, 100, 500, 1_000]),
  },
  {
    id: "perfect_wins", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "perfect_wins",
    nameKo: "완벽주의자", nameEn: "Perfectionist",
    descriptionKo: "Solo Ranked에서 PERFECT WIN을 달성하세요.", descriptionEn: "In Solo Ranked: Earn PERFECT WINs.",
    titleEnabled: true, levels: levels([1, 5, 10, 25, 50]),
  },
  {
    id: "win_streak", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "win_streak",
    nameKo: "연승", nameEn: "Win Streak",
    descriptionKo: "Solo Ranked에서 최고 연승 기록을 높이세요.", descriptionEn: "In Solo Ranked: Raise your best win streak.",
    titleEnabled: true, levels: levels([3, 5, 10, 15, 20]),
  },
  {
    id: "deathmatch_wins", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "deathmatch_wins",
    nameKo: "결투사", nameEn: "Duelist",
    descriptionKo: "Solo Ranked에서 Death Match에서 승리하세요.", descriptionEn: "In Solo Ranked: Win Death Matches.",
    titleEnabled: true, levels: levels([1, 5, 10, 25, 50]),
  },
  {
    id: "last_attempt_clears", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "last_attempt_clears",
    nameKo: "마지막 기회", nameEn: "Last Chance",
    descriptionKo: "Solo Ranked에서 LAST ATTEMPT에서 Clear하세요.", descriptionEn: "In Solo Ranked: Clear during LAST ATTEMPT.",
    titleEnabled: true, levels: levels([1, 3, 10, 25, 50]),
  },
  {
    id: "reverse_sweeps", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "reverse_sweeps",
    nameKo: "역전승", nameEn: "Reverse Sweep",
    descriptionKo: "Solo Ranked에서 0:1에서 2:1로 역전승하세요.", descriptionEn: "In Solo Ranked: Come back from 0-1 to win 2-1.",
    titleEnabled: true, levels: levels([1, 3, 10, 25, 50]),
  },
  {
    id: "sweep_wins", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "sweep_wins",
    nameKo: "완승", nameEn: "Sweep",
    descriptionKo: "Solo Ranked에서 경기를 2:0으로 승리하세요.", descriptionEn: "In Solo Ranked: Win matches 2-0.",
    titleEnabled: true, levels: levels([1, 5, 20, 50, 100]),
  },
  {
    id: "promotion_wins", type: "PROGRESSIVE", scope: "SOLO_RANKED", metric: "promotion_wins",
    nameKo: "승격", nameEn: "Promotion",
    descriptionKo: "Solo Ranked에서 Promotion에 성공하세요.", descriptionEn: "In Solo Ranked: Succeed in Promotion series.",
    titleEnabled: true, levels: levels([1, 2, 3, 5, 10]),
  },
  { id: "survival_matches", type: "PROGRESSIVE", scope: "SURVIVAL", metric: "survival_matches", nameKo: "생존 참가자", nameEn: "Survival Competitor", descriptionKo: "서바이벌 경기를 완료하세요.", descriptionEn: "Complete Survival matches.", titleEnabled: true, levels: levels([5, 25, 100, 250, 500]) },
  { id: "survival_wins", type: "PROGRESSIVE", scope: "SURVIVAL", metric: "survival_wins", nameKo: "최후의 1인", nameEn: "Last One Standing", descriptionKo: "서바이벌에서 우승하세요.", descriptionEn: "Win Survival matches.", titleEnabled: true, levels: levels([1, 3, 10, 25, 50]) },
  { id: "survival_podiums", type: "PROGRESSIVE", scope: "SURVIVAL", metric: "survival_podiums", nameKo: "입상자", nameEn: "Podium Finisher", descriptionKo: "서바이벌에서 Top 3에 입상하세요.", descriptionEn: "Finish in the Survival Top 3.", titleEnabled: true, levels: levels([1, 5, 20, 50, 100]) },
  { id: "survival_finals", type: "PROGRESSIVE", scope: "SURVIVAL", metric: "survival_finals", nameKo: "결승 진출자", nameEn: "Finalist", descriptionKo: "서바이벌 결승에 진출하세요.", descriptionEn: "Reach Survival finals.", titleEnabled: true, levels: levels([1, 5, 20, 50, 100]) },
  { id: "survival_round_survives", type: "PROGRESSIVE", scope: "SURVIVAL", metric: "survival_round_survives", nameKo: "불사조", nameEn: "Phoenix", descriptionKo: "서바이벌 라운드를 통과하세요.", descriptionEn: "Qualify through Survival rounds.", titleEnabled: true, levels: levels([10, 50, 200, 500, 1000]) },
  { id: "survival_round_wins", type: "PROGRESSIVE", scope: "SURVIVAL", metric: "survival_round_wins", nameKo: "라운드 지배자", nameEn: "Round Ruler", descriptionKo: "라운드에서 단독 1위를 달성하세요.", descriptionEn: "Finish Survival rounds as the sole first place.", titleEnabled: true, levels: levels([1, 10, 25, 50, 100]) },
  { id: "survival_podium_streak", type: "PROGRESSIVE", scope: "SURVIVAL", metric: "survival_podium_streak", nameKo: "꾸준함", nameEn: "Consistency", descriptionKo: "최고 연속 Top 3 기록을 높이세요.", descriptionEn: "Raise your best consecutive Survival Top 3 finishes.", titleEnabled: true, levels: levels([2, 3, 5, 8, 12]) },
  { id: "survival_win_streak", type: "PROGRESSIVE", scope: "SURVIVAL", metric: "survival_win_streak", nameKo: "무적 생존", nameEn: "Unbeaten Survivor", descriptionKo: "최고 연속 우승 기록을 높이세요.", descriptionEn: "Raise your best consecutive Survival wins.", titleEnabled: true, levels: levels([2, 3, 5, 7, 10]) },
  { id: "shared_matches", type: "PROGRESSIVE", scope: "SHARED", metric: "shared_matches", nameKo: "개척자", nameEn: "Pioneer", descriptionKo: "두 모드의 완료 경기 수를 높이세요.", descriptionEn: "Complete matches across both modes.", titleEnabled: true, levels: levels([20, 100, 250, 500, 1000]) },
  { id: "shared_wins", type: "PROGRESSIVE", scope: "SHARED", metric: "shared_wins", nameKo: "정복자", nameEn: "Conqueror", descriptionKo: "솔랭 승리와 서바이벌 우승을 기록하세요.", descriptionEn: "Win Solo Ranked and Survival matches.", titleEnabled: true, levels: levels([5, 25, 100, 250, 500]) },
  { id: "shared_mastery", type: "PROGRESSIVE", scope: "SHARED", metric: "shared_mastery", nameKo: "돌파자", nameEn: "Trailblazer", descriptionKo: "솔랭 맵 클리어와 서바이벌 통과 라운드를 기록하세요.", descriptionEn: "Clear Solo maps and qualify through Survival rounds.", titleEnabled: true, levels: levels([25, 100, 300, 700, 1500]) },
];

export const ONE_SHOT_ACHIEVEMENTS: readonly OneShotAchievementDefinition[] = [
  { id: "reverse_sweep_first", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "reverse_sweep_first", nameKo: "뒤집었다", nameEn: "Turned It Around", descriptionKo: "Solo Ranked에서 0:1에서 2:1로 첫 역전승을 달성하세요.", descriptionEn: "In Solo Ranked: Win 2-1 after falling behind 0-1.", titleEnabled: true, ap: 25, rarity: "RARE" },
  { id: "last_attempt_match_win", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "last_attempt_match_win", nameKo: "아직 끝나지 않았다", nameEn: "It Is Not Over", descriptionKo: "Solo Ranked에서 LAST ATTEMPT에서 Clear한 뒤 Match에서 승리하세요.", descriptionEn: "In Solo Ranked: Clear on LAST ATTEMPT, then win the match.", titleEnabled: true, ap: 25, rarity: "RARE" },
  { id: "deathmatch_tie", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "deathmatch_tie", nameKo: "다시 한 번", nameEn: "Once More", descriptionKo: "Solo Ranked에서 Death Match 동점으로 새 맵을 맞이하세요.", descriptionEn: "In Solo Ranked: Tie a Death Match and force a new map.", titleEnabled: true, ap: 25, rarity: "RARE" },
  { id: "deathmatch_close_win", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "deathmatch_close_win", nameKo: "간발의 차", nameEn: "By a Hair", descriptionKo: "Solo Ranked에서 1%p 미만 차이로 Death Match에서 승리하세요.", descriptionEn: "In Solo Ranked: Win a Death Match by less than 1 percentage point.", titleEnabled: true, ap: 25, rarity: "RARE" },
  { id: "deathmatch_pixel_win", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "deathmatch_pixel_win", nameKo: "한 끗 차이", nameEn: "Pixel Perfect", descriptionKo: "Solo Ranked에서 0.1%p 미만 차이로 Death Match에서 승리하세요.", descriptionEn: "In Solo Ranked: Win a Death Match by less than 0.1 percentage point.", titleEnabled: true, ap: 50, rarity: "EPIC" },
  { id: "deathmatch_domination", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "deathmatch_domination", nameKo: "압도적", nameEn: "Dominating", descriptionKo: "Solo Ranked에서 50%p 이상 차이로 Death Match에서 승리하세요.", descriptionEn: "In Solo Ranked: Win a Death Match by at least 50 percentage points.", titleEnabled: true, ap: 25, rarity: "RARE" },
  { id: "deathmatch_hundred_win", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "deathmatch_hundred_win", nameKo: "완벽한 결투", nameEn: "Perfect Duel", descriptionKo: "Solo Ranked에서 Death Match에서 100%를 기록하고 승리하세요.", descriptionEn: "In Solo Ranked: Record 100% and win a Death Match.", titleEnabled: true, ap: 50, rarity: "EPIC" },
  { id: "deathmatch_double_hundred", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "deathmatch_double_hundred", nameKo: "양보는 없다", nameEn: "No Concessions", descriptionKo: "Solo Ranked에서 양쪽 모두 100%로 Death Match에서 비기세요.", descriptionEn: "In Solo Ranked: Tie a Death Match with both sides at 100%.", titleEnabled: true, ap: 200, rarity: "MYTHIC" },
  { id: "gold_reached", type: "ONE_SHOT", scope: "SHARED", condition: "gold_reached", nameKo: "정상", nameEn: "The Summit", descriptionKo: "Solo Ranked 또는 Survival에서 GOLD 티어에 처음 도달하세요.", descriptionEn: "Reach GOLD in Solo Ranked or Survival for the first time.", titleEnabled: true, ap: 50, rarity: "EPIC" },
  { id: "perfect_win_streak_3", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "perfect_win_streak_3", nameKo: "무결점", nameEn: "Flawless", descriptionKo: "Solo Ranked에서 PERFECT WIN을 3경기 연속 달성하세요.", descriptionEn: "In Solo Ranked: Earn three consecutive PERFECT WINs.", titleEnabled: true, ap: 100, rarity: "LEGENDARY" },
  { id: "placement_complete", type: "ONE_SHOT", scope: "SOLO_RANKED", condition: "placement_complete", nameKo: "첫 걸음", nameEn: "First Step", descriptionKo: "Solo Ranked에서 정상 Placement를 완료하세요.", descriptionEn: "In Solo Ranked: Complete Placement normally.", titleEnabled: true, ap: 10, rarity: "COMMON" },
  { id: "survival_first_match", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_first_match", nameKo: "서바이벌 데뷔", nameEn: "Survival Debut", descriptionKo: "첫 서바이벌 경기를 완료하세요.", descriptionEn: "Complete your first Survival match.", titleEnabled: true, ap: 10, rarity: "COMMON" },
  { id: "survival_first_podium", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_first_podium", nameKo: "첫 입상", nameEn: "First Podium", descriptionKo: "서바이벌에서 처음으로 Top 3에 입상하세요.", descriptionEn: "Finish in the Survival Top 3 for the first time.", titleEnabled: true, ap: 10, rarity: "COMMON" },
  { id: "survival_first_win", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_first_win", nameKo: "첫 우승", nameEn: "First Champion", descriptionKo: "서바이벌에서 처음으로 우승하세요.", descriptionEn: "Win your first Survival match.", titleEnabled: true, ap: 25, rarity: "RARE" },
  { id: "survival_cutoff_escape", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_cutoff_escape", nameKo: "턱걸이 생존", nameEn: "Just Made It", descriptionKo: "정확히 컷오프 경계 순위로 통과하세요.", descriptionEn: "Qualify at the exact official cutoff rank.", titleEnabled: true, ap: 25, rarity: "RARE" },
  { id: "survival_final_photo_finish", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_final_photo_finish", nameKo: "사진 판정", nameEn: "Photo Finish", descriptionKo: "결승에서 1%p 미만 차이로 우승하세요.", descriptionEn: "Win the decisive final by more than 0 and less than 1 percentage point.", titleEnabled: true, ap: 50, rarity: "EPIC" },
  { id: "survival_round_perfect", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_round_perfect", nameKo: "완벽한 생존", nameEn: "Perfect Survivor", descriptionKo: "유효한 서바이벌 라운드에서 100%를 기록하세요.", descriptionEn: "Reach 100% in a valid Survival round.", titleEnabled: true, ap: 50, rarity: "EPIC" },
  { id: "survival_clean_champion", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_clean_champion", nameKo: "완전 우승", nameEn: "Clean Champion", descriptionKo: "모든 라운드에서 단독 1위로 우승하세요.", descriptionEn: "Win the match as the sole first place in every round.", titleEnabled: true, ap: 100, rarity: "LEGENDARY" },
  { id: "survival_last_seed_champion", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_last_seed_champion", nameKo: "역습의 우승자", nameEn: "Underdog Champion", descriptionKo: "결승 최하위 단독 시드에서 우승하세요.", descriptionEn: "Win from the unique last qualifying seed.", titleEnabled: true, ap: 100, rarity: "LEGENDARY" },
  { id: "survival_final_perfect_win", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_final_perfect_win", nameKo: "완벽한 피날레", nameEn: "Perfect Finale", descriptionKo: "결승에서 100%를 기록하고 최종 우승하세요.", descriptionEn: "Reach 100% in the decisive final and win.", titleEnabled: true, ap: 200, rarity: "MYTHIC" },
  { id: "survival_never_below_cutoff", type: "ONE_SHOT", scope: "SURVIVAL", condition: "survival_never_below_cutoff", nameKo: "흔들리지 않는 자", nameEn: "Unshaken", descriptionKo: "모든 탈락 라운드에서 컷오프보다 높은 순위로 통과하고 우승하세요.", descriptionEn: "Win after qualifying strictly above every elimination cutoff rank.", titleEnabled: true, ap: 200, rarity: "MYTHIC" },
  { id: "dual_mode_debut", type: "ONE_SHOT", scope: "SHARED", condition: "dual_mode_debut", nameKo: "두 세계의 시작", nameEn: "Two Worlds Begin", descriptionKo: "두 모드에서 각각 한 경기를 완료하세요.", descriptionEn: "Complete a match in each mode.", titleEnabled: true, ap: 10, rarity: "COMMON" },
  { id: "dual_mode_winner", type: "ONE_SHOT", scope: "SHARED", condition: "dual_mode_winner", nameKo: "두 세계의 승자", nameEn: "Winner of Two Worlds", descriptionKo: "솔랭 1승과 서바이벌 1우승을 기록하세요.", descriptionEn: "Win once in Solo Ranked and once in Survival.", titleEnabled: true, ap: 25, rarity: "RARE" },
  { id: "dual_gold_reached", type: "ONE_SHOT", scope: "SHARED", condition: "dual_gold_reached", nameKo: "양쪽의 정상", nameEn: "Twin Summits", descriptionKo: "정상 경기로 두 모드에서 모두 GOLD에 도달하세요.", descriptionEn: "Reach GOLD through valid results in both modes.", titleEnabled: true, ap: 100, rarity: "LEGENDARY" },
  { id: "master_of_two_worlds", type: "ONE_SHOT", scope: "SHARED", condition: "master_of_two_worlds", nameKo: "두 세계의 지배자", nameEn: "Master of Two Worlds", descriptionKo: "두 모드에서 각각 성장형 V를 달성하세요.", descriptionEn: "Reach a progressive stage V in each mode.", titleEnabled: true, ap: 200, rarity: "MYTHIC" },
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
