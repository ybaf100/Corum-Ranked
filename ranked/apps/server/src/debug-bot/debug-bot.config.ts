import type {
  DebugBotDifficulty,
  DebugBotDifficultyProfile,
} from "./debug-bot.types.js";

/**
 * Humanized NORMAL_MATCH defaults. Environment variables can override these at
 * deployment time; map difficulty (1..21+), map length, and Bot Hidden MMR are
 * applied later by debug-bot.behavior.ts on every attempt.
 */
export const DEBUG_BOT_DIFFICULTY_PROFILES: Readonly<
  Record<DebugBotDifficulty, DebugBotDifficultyProfile>
> = Object.freeze({
  EASY: Object.freeze({
    ratingOffset: -250,
    qualifyingReachProbability: 0.16,
    clearProbability: 0.004,
    paceMultiplier: 0.96,
  }),
  NORMAL: Object.freeze({
    ratingOffset: 0,
    qualifyingReachProbability: 0.30,
    clearProbability: 0.015,
    paceMultiplier: 1.00,
  }),
  HARD: Object.freeze({
    ratingOffset: 250,
    qualifyingReachProbability: 0.46,
    clearProbability: 0.045,
    paceMultiplier: 1.05,
  }),
});
