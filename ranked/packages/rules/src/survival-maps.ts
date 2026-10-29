import { RankedDomainError } from "./errors.js";
import { validateSurvivalConfig, type SurvivalConfig, type SurvivalTierBand } from "./survival.js";

export type SurvivalDifficultyGroup = "RED_BRONZE" | "SILVER" | "GOLD";
export type SurvivalMapPhase = "ROUND_1" | "ROUND_2" | "ROUND_3" | "ROUND_4" | "FINAL" | "TIEBREAK";
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
  readonly featureGrade?:"NONE"|"FEATURED"|"EPIC"|"LEGENDARY"|"MYTHIC";
}
export interface SurvivalVerifiedMap extends SurvivalMapMetadata {
  readonly levelVersion: number;
  readonly geometrySha256: string;
  readonly geometryBytes: number;
  readonly verifiedAt: string;
  readonly songIds?: readonly number[];
  readonly sfxIds?: readonly number[];
  readonly sourceSearchType?: string;
  readonly sourcePage?: number;
  readonly sourceIdBand?: string;
  readonly audioVerification?: "METADATA_ONLY";
  readonly audioDependencyCoverage?:"PRIMARY_SONG_ONLY";

}
export interface SurvivalMapRange {
  readonly minimumStars: number | null;
  readonly maximumStars: number | null;
  readonly demons: readonly SurvivalDemonDifficulty[];
}
const range = (minimumStars: number | null, maximumStars: number | null,
  demons: readonly SurvivalDemonDifficulty[] = []): SurvivalMapRange => ({ minimumStars, maximumStars, demons });
const RANGES: Record<SurvivalDifficultyGroup, Record<SurvivalMapPhase, SurvivalMapRange>> = {
  RED_BRONZE: { ROUND_1: range(4,6), ROUND_2: range(5,7), ROUND_3: range(6,8), ROUND_4: range(6,8),
    FINAL: range(7,9,["EASY"]), TIEBREAK: range(null,null,["MEDIUM"]) },
  SILVER: { ROUND_1: range(5,7), ROUND_2: range(6,8), ROUND_3: range(7,9), ROUND_4: range(7,9),
    FINAL: range(null,null,["EASY","MEDIUM"]), TIEBREAK: range(null,null,["HARD"]) },
  GOLD: { ROUND_1: range(6,8), ROUND_2: range(7,9), ROUND_3: range(8,9,["EASY"]), ROUND_4: range(8,9,["EASY"]),
    FINAL: range(null,null,["EASY","MEDIUM","HARD"]), TIEBREAK: range(null,null,["INSANE"]) },
};
export function survivalMapRange(group: SurvivalDifficultyGroup, phase: SurvivalMapPhase): SurvivalMapRange {
  return structuredClone(RANGES[group][phase]);
}

/** Official GD difficulty, independent of a map's featured grade. */
export const SURVIVAL_MAP_DIFFICULTIES = [
  {id:"EASY",name:"Easy",nativeCode:1,minimumStars:2,maximumStars:2},
  {id:"NORMAL",name:"Normal",nativeCode:2,minimumStars:3,maximumStars:3},
  {id:"HARD",name:"Hard",nativeCode:3,minimumStars:4,maximumStars:5},
  {id:"HARDER",name:"Harder",nativeCode:4,minimumStars:6,maximumStars:7},
  {id:"INSANE",name:"Insane",nativeCode:5,minimumStars:8,maximumStars:9},
  ...(["EASY","MEDIUM","HARD","INSANE","EXTREME"] as const).map((d,i)=>({
    id:`${d}_DEMON`,name:`${d[0]}${d.slice(1).toLowerCase()} Demon`,
    nativeCode:[7,8,6,9,10][i]!,demonDifficulty:d,minimumStars:10,maximumStars:10})),
] as const;
export function survivalRecordedDifficulty(map:SurvivalMapMetadata) {
  if(map.automatic || map.platformer || !Number.isInteger(map.stars))return null;
  return SURVIVAL_MAP_DIFFICULTIES.find(d=>map.demon
    ?map.stars===10 && "demonDifficulty" in d && d.demonDifficulty===map.demonDifficulty
    :map.demonDifficulty===null && !("demonDifficulty" in d) && map.stars>=d.minimumStars && map.stars<=d.maximumStars)??null;
}
/** The gallery follows ALL selection rules, never today's pool or user history. */
export function survivalCollectibleDifficulties() {
  return SURVIVAL_MAP_DIFFICULTIES.filter(d=>Object.values(RANGES).some(phases=>Object.values(phases).some(r=>
    "demonDifficulty" in d?r.demons.includes(d.demonDifficulty):r.minimumStars!==null && r.maximumStars!==null &&
      d.minimumStars<=r.maximumStars && d.maximumStars>=r.minimumStars))).map(d=>({id:d.id,name:d.name,nativeCode:d.nativeCode}));
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
  if (![3,6].includes(config.tiebreakAttempts)) errors.push("Supported Survival attempt limits are 3 (legacy) and 6");
  const expected = { roundSeconds:180, round1Qualifiers:config.matchPlayerCount===64?40:10,
    round2Qualifiers:config.matchPlayerCount===64?26:6, round3Qualifiers:config.matchPlayerCount===64?12:3, finalWinners:1, scoreMultiplier:1,
    tiebreakDecimals:3, allowCutoffTies:true } as const;
  for (const [key,value] of Object.entries(expected)) {
    if (config[key as keyof SurvivalConfig] !== value) errors.push(`M3 requires ${key} = ${value}`);
  }
  if(config.matchPlayerCount===64 && config.round4Qualifiers!==5)errors.push("64-player Survival requires round4Qualifiers = 5");
  if (config.minimumLevelId < 40_000_000) errors.push("M3 requires minimumLevelId >= 40000000");
  return errors;
}
