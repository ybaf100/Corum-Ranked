import type { RandomSource } from "@corum-ranked/rules";
import type { DebugBotDifficultyEnvironment } from "../config/server-environment.js";

export interface DebugBotMapBehaviorMetadata {
  readonly difficulty: string | number | null | undefined;
  readonly length: string | number | null | undefined;
}

export interface DebugBotAttemptPlanInput {
  readonly qualifyingPercent: number;
  readonly botHiddenMmr: number;
  readonly map: DebugBotMapBehaviorMetadata;
  readonly profile: DebugBotDifficultyEnvironment;
  /** FORCE/TRIGGER scenarios remain deliberately fast and deterministic for debugging. */
  readonly syntheticScenario: boolean;
}

export interface DebugBotAttemptPlan {
  readonly targetPercent: number;
  readonly durationMs: number;
  readonly restartDelayMs: number;
  readonly effectiveQualifyingChance: number;
  readonly effectiveClearChance: number;
  readonly mapDifficulty: number;
  readonly mapLengthSeconds: number;
}

const clamp = (value: number, minimum: number, maximum: number): number =>
  Math.min(maximum, Math.max(minimum, value));

const randomRange = (random: RandomSource, minimum: number, maximum: number): number =>
  minimum + (maximum - minimum) * random.next();

/** Accepts the live 1..21+ scale. Legacy text falls back to the middle (11). */
export const parseDebugBotMapDifficulty = (value: string | number | null | undefined): number => {
  if (typeof value === "number" && Number.isFinite(value)) return clamp(Math.round(value), 1, 21);
  const text = String(value ?? "").trim();
  const matched = text.match(/(?:^|\D)(\d{1,2})(?:\+)?(?:\D|$)/);
  const parsed = matched ? Number(matched[1]) : Number.NaN;
  return Number.isFinite(parsed) ? clamp(Math.round(parsed), 1, 21) : 11;
};

/**
 * Converts Geometry Dash length labels into an approximate full-run duration.
 * Exact seconds and m:ss are accepted as well.
 */
export const estimateDebugBotMapLengthSeconds = (
  value: string | number | null | undefined,
): number => {
  if (typeof value === "number" && Number.isFinite(value)) return clamp(value, 12, 300);
  const text = String(value ?? "").trim().toLowerCase();
  if (!text) return 60;

  const clock = text.match(/^(\d{1,2}):(\d{2})$/);
  if (clock) {
    const seconds = Number(clock[1]) * 60 + Number(clock[2]);
    if (Number.isFinite(seconds)) return clamp(seconds, 12, 300);
  }
  const numeric = Number(text.replace(/\s*(?:s|sec|secs|second|seconds|초)\s*$/i, ""));
  if (Number.isFinite(numeric) && numeric > 0) return clamp(numeric, 12, 300);

  if (/\b(?:tiny)\b|매우\s*짧|초단/.test(text)) return 22;
  if (/\b(?:short)\b|짧/.test(text)) return 38;
  if (/\b(?:medium|med)\b|보통|중간/.test(text)) return 60;
  if (/\b(?:extra\s*long|xl)\b|매우\s*길|초장/.test(text)) return 120;
  if (/\b(?:long)\b|길/.test(text)) return 88;
  if (/platform|플랫포머|플랫폼/.test(text)) return 150;
  return 60;
};

const expectedMmrForDifficulty = (difficulty: number): number =>
  400 + (difficulty - 1) * 200;

const lengthProbabilityMultiplier = (lengthSeconds: number): number => {
  if (lengthSeconds <= 25) return 1.18;
  if (lengthSeconds <= 45) return 1.10;
  if (lengthSeconds <= 70) return 1;
  if (lengthSeconds <= 100) return 0.88;
  if (lengthSeconds <= 130) return 0.78;
  return 0.68;
};

const chooseFailedTarget = (
  random: RandomSource,
  qualifyingPercent: number,
  skill: number,
): number => {
  const upper = Math.max(1, Math.ceil(qualifyingPercent) - 1);
  if (upper <= 1) return 1;

  // Occasional near-misses make the Bot look less like uniform RNG.
  const nearMissChance = clamp(0.05 + (skill + 2.5) * 0.025, 0.04, 0.16);
  if (random.next() < nearMissChance && upper >= 6) {
    const missBy = Math.max(1, Math.round(randomRange(random, 1, Math.min(7, upper - 1))));
    return Math.max(1, upper - missBy + 1);
  }

  // Weak Bots die earlier more often; strong Bots have a flatter failed-run curve.
  const exponent = clamp(1.65 - skill * 0.22, 0.85, 2.10);
  return clamp(Math.max(1, Math.floor(Math.pow(random.next(), exponent) * upper)), 1, upper);
};

const chooseQualifiedTarget = (
  random: RandomSource,
  qualifyingPercent: number,
  skill: number,
): number => {
  const minimum = clamp(Math.ceil(qualifyingPercent), 1, 99);
  if (minimum >= 99) return 99;
  const span = 99 - minimum;
  // Most non-clears die near qualifying; stronger Bots produce more deep runs.
  const exponent = clamp(1.75 - skill * 0.20, 0.90, 2.20);
  const normalized = Math.pow(random.next(), exponent);
  return clamp(minimum + Math.floor(normalized * (span + 1)), minimum, 99);
};

export const createDebugBotAttemptPlan = (
  input: DebugBotAttemptPlanInput,
  random: RandomSource,
): DebugBotAttemptPlan => {
  const qualifyingPercent = clamp(
    Number.isFinite(input.qualifyingPercent) ? input.qualifyingPercent : 50,
    1,
    99,
  );
  const mapDifficulty = parseDebugBotMapDifficulty(input.map.difficulty);
  const mapLengthSeconds = estimateDebugBotMapLengthSeconds(input.map.length);
  const expectedMmr = expectedMmrForDifficulty(mapDifficulty);
  const skill = clamp((input.botHiddenMmr - expectedMmr) / 900, -2.5, 2.5);
  const lengthMultiplier = lengthProbabilityMultiplier(mapLengthSeconds);

  // Difficulty selection provides the base profile. Hidden MMR versus the map's
  // 1..21+ expected MMR then adjusts it, and map length independently reduces
  // deep-run/clear probability because longer maps create more failure points.
  const effectiveQualifyingChance = clamp(
    input.profile.qualifyingChance * Math.exp(skill * 0.42) * Math.sqrt(lengthMultiplier),
    0.025,
    0.78,
  );
  const effectiveClearChance = clamp(
    input.profile.clearChance * Math.exp(skill * 0.68) * lengthMultiplier,
    0.0005,
    Math.min(0.32, effectiveQualifyingChance * 0.42),
  );

  const outcome = random.next();
  let targetPercent: number;
  if (outcome < effectiveClearChance) {
    targetPercent = 100;
  } else if (outcome < effectiveQualifyingChance) {
    targetPercent = chooseQualifiedTarget(random, qualifyingPercent, skill);
  } else {
    targetPercent = chooseFailedTarget(random, qualifyingPercent, skill);
  }

  if (input.syntheticScenario) {
    return {
      targetPercent,
      durationMs: Math.max(700, Math.round(targetPercent * randomRange(random, 28, 48))),
      restartDelayMs: Math.round(randomRange(random, 250, 650)),
      effectiveQualifyingChance,
      effectiveClearChance,
      mapDifficulty,
      mapLengthSeconds,
    };
  }

  // Normal Bot matches follow map duration instead of the old 11/16/22 %/s.
  // Example: a 60s map takes roughly 30s to reach 50% at 1.0x pace.
  const paceJitter = randomRange(random, 0.94, 1.08);
  const paceMultiplier = clamp(input.profile.paceMultiplier * paceJitter, 0.72, 1.28);
  const runSeconds = Math.max(
    0.9,
    (mapLengthSeconds * (targetPercent / 100)) / paceMultiplier,
  );

  // Early deaths restart quickly; deep runs/clears take longer. Jitter prevents
  // consecutive attempts from looking metronomic.
  const depthFactor = targetPercent / 100;
  const restartDelayMs = Math.round(
    randomRange(random, 650, 1_250) + depthFactor * randomRange(random, 350, 1_350),
  );

  return {
    targetPercent,
    durationMs: Math.round(runSeconds * 1_000),
    restartDelayMs,
    effectiveQualifyingChance,
    effectiveClearChance,
    mapDifficulty,
    mapLengthSeconds,
  };
};
