import { RANKED_TIERS, type BaseRankedTier } from "./types.js";
import { baseRankedTier } from "./solo-tiers.js";

/** Survival elimination effects. The round's 1st place plays its equipped
 * effect on every eliminated player; DEFAULT is the plain fall. */
export const ELIMINATION_EFFECTS = ["DEFAULT", "BOLT", "SLICE", "SHATTER", "VORTEX"] as const;
export type EliminationEffect = (typeof ELIMINATION_EFFECTS)[number];

export function normalizeEliminationEffect(value: unknown): EliminationEffect {
  return typeof value === "string" && (ELIMINATION_EFFECTS as readonly string[]).includes(value)
    ? value as EliminationEffect : "DEFAULT";
}

/** Private achievement grade: claimed AP total (4,965 AP achievable). */
export const ACHIEVEMENT_AP_GRADES: readonly { tier: BaseRankedTier; minAp: number }[] = [
  { tier: "RED", minAp: 100 },
  { tier: "AQUA", minAp: 400 },
  { tier: "BRONZE", minAp: 1000 },
  { tier: "SILVER", minAp: 2000 },
  { tier: "GOLD", minAp: 3200 },
];

export interface AchievementApGrade {
  tier: BaseRankedTier | null;
  ap: number;
  /** AP where the current grade starts (0 without a grade). */
  floorAp: number;
  nextTier: BaseRankedTier | null;
  nextAp: number | null;
}

export function achievementApGrade(totalAp: number): AchievementApGrade {
  const ap = Math.max(0, Math.floor(Number.isFinite(totalAp) ? totalAp : 0));
  let index = -1;
  ACHIEVEMENT_AP_GRADES.forEach((grade, i) => { if (ap >= grade.minAp) index = i; });
  const next = ACHIEVEMENT_AP_GRADES[index + 1] ?? null;
  return {
    tier: index >= 0 ? ACHIEVEMENT_AP_GRADES[index]!.tier : null,
    ap,
    floorAp: index >= 0 ? ACHIEVEMENT_AP_GRADES[index]!.minAp : 0,
    nextTier: next?.tier ?? null,
    nextAp: next?.minAp ?? null,
  };
}

export function rankedTierAtLeast(tier: BaseRankedTier | null, minimum: BaseRankedTier): boolean {
  return tier !== null && RANKED_TIERS.indexOf(tier) >= RANKED_TIERS.indexOf(minimum);
}

/** Highest base tier among the given tier names (bands, MASTER/CHAMPION fold to GOLD). */
export function highestBaseTier(tiers: readonly (string | null | undefined)[]): BaseRankedTier | null {
  let best: BaseRankedTier | null = null;
  for (const raw of tiers) {
    const tier = raw ? baseRankedTier(raw) : null;
    if (tier && (best === null || RANKED_TIERS.indexOf(tier) > RANKED_TIERS.indexOf(best))) best = tier;
  }
  return best;
}

export interface EliminationUnlockFacts {
  /** Highest Survival tier ever reached. */
  survivalPeak: BaseRankedTier | null;
  /** Highest Solo Ranked tier ever reached. */
  soloPeak: BaseRankedTier | null;
  /** Claimed AP total. */
  totalAp: number;
}

export type EliminationRequirement =
  | { kind: "NONE" }
  | { kind: "SURVIVAL_TIER"; tier: BaseRankedTier }
  | { kind: "SURVIVAL_AND_SOLO_TIER"; tier: BaseRankedTier }
  | { kind: "ACHIEVEMENT_GRADE"; tier: BaseRankedTier };

export const ELIMINATION_REQUIREMENTS: Readonly<Record<EliminationEffect, EliminationRequirement>> = {
  DEFAULT: { kind: "NONE" },
  BOLT: { kind: "SURVIVAL_TIER", tier: "SILVER" },
  SLICE: { kind: "SURVIVAL_AND_SOLO_TIER", tier: "GOLD" },
  SHATTER: { kind: "ACHIEVEMENT_GRADE", tier: "BRONZE" },
  VORTEX: { kind: "ACHIEVEMENT_GRADE", tier: "GOLD" },
};

export function eliminationEffectUnlocked(effect: EliminationEffect, facts: EliminationUnlockFacts): boolean {
  const requirement = ELIMINATION_REQUIREMENTS[effect];
  switch (requirement.kind) {
    case "NONE": return true;
    case "SURVIVAL_TIER": return rankedTierAtLeast(facts.survivalPeak, requirement.tier);
    case "SURVIVAL_AND_SOLO_TIER":
      return rankedTierAtLeast(facts.survivalPeak, requirement.tier) && rankedTierAtLeast(facts.soloPeak, requirement.tier);
    case "ACHIEVEMENT_GRADE": return rankedTierAtLeast(achievementApGrade(facts.totalAp).tier, requirement.tier);
  }
}
