import { RankedDomainError } from "./errors.js";
import { validateSurvivalConfig, type SurvivalConfig, type SurvivalTierBand } from "./survival.js";

export type SurvivalDifficultyGroup = "RED_BRONZE" | "SILVER" | "GOLD";
export type SurvivalMapPhase = "ROUND_1" | "ROUND_2" | "ROUND_3" | "FINAL" | "TIEBREAK";
export type SurvivalDemonDifficulty = "EASY" | "MEDIUM" | "HARD" | "INSANE" | "EXTREME";

export interface SurvivalMapMetadata {
  readonly name?: string;
  readonly creator?: string;
  readonly levelId: number;
  readonly stars: number;
  readonly demon: boolean;
  readonly demonDifficulty: SurvivalDemonDifficulty | null;
  readonly automatic: boolean;
  readonly platformer: boolean;
  readonly length?:number;
  readonly featureScore?:number;
  readonly epic?:number;
}
export interface SurvivalVerifiedMap extends SurvivalMapMetadata {
  readonly levelVersion: number;
  readonly geometrySha256: string;
  readonly geometryBytes: number;
  readonly verifiedAt: string;
}
export interface SurvivalMapRange {
  readonly minimumStars: number | null;
  readonly maximumStars: number | null;
  readonly demons: readonly SurvivalDemonDifficulty[];
}
const range = (minimumStars: number | null, maximumStars: number | null,
  demons: readonly SurvivalDemonDifficulty[] = []): SurvivalMapRange => ({ minimumStars, maximumStars, demons });
const RANGES: Record<SurvivalDifficultyGroup, Record<SurvivalMapPhase, SurvivalMapRange>> = {
  RED_BRONZE: { ROUND_1: range(4,6), ROUND_2: range(5,7), ROUND_3: range(6,8),
    FINAL: range(7,9,["EASY"]), TIEBREAK: range(null,null,["MEDIUM"]) },
  SILVER: { ROUND_1: range(5,7), ROUND_2: range(6,8), ROUND_3: range(7,9),
    FINAL: range(null,null,["EASY","MEDIUM"]), TIEBREAK: range(null,null,["HARD"]) },
  GOLD: { ROUND_1: range(6,8), ROUND_2: range(7,9), ROUND_3: range(8,9,["EASY"]),
    FINAL: range(null,null,["EASY","MEDIUM","HARD"]), TIEBREAK: range(null,null,["INSANE"]) },
};
export function survivalMapRange(group: SurvivalDifficultyGroup, phase: SurvivalMapPhase): SurvivalMapRange {
  return structuredClone(RANGES[group][phase]);
}

/** Codes verified against Geode's GD 2.2081 DemonDifficultyType enum.
 * Search demonFilter codes are different and are deliberately not used here. */
export function survivalDemonFromNativeCode(code: number): SurvivalDemonDifficulty | null {
  switch (code) {
    case 3: return "EASY";
    case 4: return "MEDIUM";
    case 0: return "HARD";
    case 5: return "INSANE";
    case 6: return "EXTREME";
    default: return null;
  }
}

export function survivalMapAllowed(map: SurvivalMapMetadata, group: SurvivalDifficultyGroup,
  phase: SurvivalMapPhase, minimumLevelId = 40_000_000): boolean {
  if (!Number.isSafeInteger(map.levelId) || map.levelId < Math.max(40_000_000,minimumLevelId) ||
      !Number.isInteger(map.stars) || map.stars < 1 || map.stars > 10 ||
      map.automatic || map.platformer || (map.length!==undefined && (!Number.isInteger(map.length) || map.length<0 || map.length>3))) return false;
  const allowed = RANGES[group][phase];
  if (map.demon) return map.stars === 10 && map.demonDifficulty !== null && allowed.demons.includes(map.demonDifficulty);
  return map.demonDifficulty === null && allowed.minimumStars !== null && allowed.maximumStars !== null &&
    map.stars >= allowed.minimumStars && map.stars <= allowed.maximumStars;
}

/** Means can be fractional; account profile LP remains an integer. Group IDs,
 * rather than tier display names, support future subdivided Survival bands. */
export function survivalDifficultyForAverage(averageLp: number, bands: readonly SurvivalTierBand[]): SurvivalDifficultyGroup {
  const band = [...bands].reverse().find(item => averageLp >= item.minimumLp);
  if (!Number.isFinite(averageLp) || averageLp < 0 || !band) throw new RankedDomainError("INVALID_CONFIG", "Invalid Survival average LP");
  if (["RED","AQUA","BRONZE"].includes(band.group)) return "RED_BRONZE";
  if (band.group === "SILVER" || band.group === "GOLD") return band.group;
  throw new RankedDomainError("INVALID_CONFIG", "Unsupported Survival difficulty group");
}

/** Config validation for this executable milestone, not a change to Solo or
 * the M1 profile configuration model. Unsupported rules fail at admission. */
export function validateSurvivalEngineConfig(config: SurvivalConfig): readonly string[] {
  const errors = [...validateSurvivalConfig(config)];
  const expected = { matchPlayerCount:16, roundSeconds:180, round1Qualifiers:10,
    round2Qualifiers:6, round3Qualifiers:3, finalWinners:1, scoreMultiplier:1,
    tiebreakAttempts:3, tiebreakDecimals:3, allowCutoffTies:true } as const;
  for (const [key,value] of Object.entries(expected)) {
    if (config[key as keyof SurvivalConfig] !== value) errors.push(`M3 requires ${key} = ${value}`);
  }
  if (config.minimumLevelId < 40_000_000) errors.push("M3 requires minimumLevelId >= 40000000");
  return errors;
}
