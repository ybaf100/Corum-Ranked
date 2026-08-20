import type {
  DebugBotDifficulty,
  DebugBotDifficultyProfile,
} from "./debug-bot.types.js";

export const DEBUG_BOT_DIFFICULTY_PROFILES: Readonly<
  Record<DebugBotDifficulty, DebugBotDifficultyProfile>
> = Object.freeze({
  EASY: Object.freeze({
    ratingOffset: -200,
    averageProgress: 42,
    qualifyingReachProbability: 0.3,
    clearProbability: 0.03,
    progressPerSecond: 28,
    restartDelayMs: 450,
  }),
  NORMAL: Object.freeze({
    ratingOffset: 0,
    averageProgress: 64,
    qualifyingReachProbability: 0.58,
    clearProbability: 0.1,
    progressPerSecond: 42,
    restartDelayMs: 300,
  }),
  HARD: Object.freeze({
    ratingOffset: 200,
    averageProgress: 82,
    qualifyingReachProbability: 0.82,
    clearProbability: 0.24,
    progressPerSecond: 60,
    restartDelayMs: 200,
  }),
});
