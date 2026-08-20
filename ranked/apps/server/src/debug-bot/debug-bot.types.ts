export const DEBUG_BOT_DIFFICULTIES = ["EASY", "NORMAL", "HARD"] as const;
export type DebugBotDifficulty = (typeof DEBUG_BOT_DIFFICULTIES)[number];

export const DEBUG_BOT_SCENARIOS = [
  "NORMAL_MATCH",
  "FORCE_BOT_ONE_CLEAR",
  "FORCE_BOT_TWO_CLEARS",
  "TRIGGER_LAST_ATTEMPT",
  "TRIGGER_ROUND_DRAW",
  "TRIGGER_ROUND_THREE",
  "TRIGGER_DEATHMATCH",
] as const;
export type DebugBotScenario = (typeof DEBUG_BOT_SCENARIOS)[number];

export const DEBUG_BOT_BAN_MODES = ["RANDOM", "NO_BAN"] as const;
export type DebugBotBanMode = (typeof DEBUG_BOT_BAN_MODES)[number];

export interface DebugBotMatchConfig {
  readonly difficulty: DebugBotDifficulty;
  readonly scenario: DebugBotScenario;
  readonly botBan: DebugBotBanMode;
  readonly sendDiscordEvents: boolean;
  readonly botRating: number;
  readonly ratingOffset: number;
  readonly botPlacementGames: number;
}

export interface DebugBotDifficultyProfile {
  readonly ratingOffset: number;
  readonly averageProgress: number;
  readonly qualifyingReachProbability: number;
  readonly clearProbability: number;
  readonly progressPerSecond: number;
  readonly restartDelayMs: number;
}
