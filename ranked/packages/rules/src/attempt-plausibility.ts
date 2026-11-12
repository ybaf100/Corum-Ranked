/**
 * Server-side timing plausibility for client-reported progress and clears.
 *
 * This is not anti-cheat: it only rejects results that no legitimate run can
 * produce, such as a clear reported a second after its attempt started. Every
 * bound is deliberately conservative so a real run is never refused:
 *  - only half of the map's estimated duration is required for a clear;
 *  - partial progress assumes the part already played ran at GD's fastest
 *    speed and the rest at its slowest (speed portals cannot beat that);
 *  - two seconds of transport/clock slack are always granted;
 *  - a map without a duration estimate is never bounded.
 */

export const IMPLAUSIBLE_ATTEMPT_TIMING = "IMPLAUSIBLE_ATTEMPT_TIMING";

/** Super fast (~1.85x) over slow (~0.81x) speed portals, rounded up. */
export const GD_SPEED_RATIO = 2.3;
/** Only this fraction of a map's estimated duration is ever required. */
export const PLAUSIBLE_DURATION_FACTOR = 0.5;
export const PLAUSIBILITY_SLACK_MS = 2_000;
/** No map with a known duration can be cleared faster than this. */
export const MINIMUM_CLEAR_MS = 2_000;

/** GD length categories (Tiny, Short, Medium, Long, XL) mapped to their lower bounds in seconds. */
export const gdLengthLowerBoundSeconds = (length: number | undefined | null): number | null => {
  switch (length) {
    case 0: return 0;
    case 1: return 10;
    case 2: return 30;
    case 3: return 60;
    case 4: return 120;
    default: return null;
  }
};

const estimateMs = (estimateSeconds: number | null | undefined): number =>
  typeof estimateSeconds === "number" && Number.isFinite(estimateSeconds) && estimateSeconds > 0
    ? estimateSeconds * 1_000 * PLAUSIBLE_DURATION_FACTOR
    : 0;

/** Share of the run's duration that progress p (0..1) needs at the most favourable speed split. */
const durationShare = (p: number): number => p >= 1 ? 1 : p <= 0 ? 0 : p / (p + (1 - p) * GD_SPEED_RATIO);

/** The shortest elapsed time (ms) in which a legitimate run reaches progressPercent, or clears. */
export const minimumAttemptElapsedMs = (
  estimateSeconds: number | null | undefined,
  progressPercent: number,
  cleared: boolean,
): number => {
  const duration = estimateMs(estimateSeconds);
  if (duration <= 0) return 0;
  const p = cleared ? 1 : Math.min(1, Math.max(0, progressPercent / 100));
  const bound = duration * durationShare(p) - PLAUSIBILITY_SLACK_MS;
  return Math.max(cleared || p >= 1 ? MINIMUM_CLEAR_MS : 0, bound);
};

export const attemptTimingPlausible = (
  elapsedMs: number,
  estimateSeconds: number | null | undefined,
  progressPercent: number,
  cleared: boolean,
): boolean => Number.isFinite(elapsedMs) && elapsedMs >= minimumAttemptElapsedMs(estimateSeconds, progressPercent, cleared);

/**
 * The highest progress percent a legitimate run can have reached after
 * elapsedMs; 100 only once a clear is plausible. Used to cap streamed progress.
 */
export const maximumPlausibleProgress = (elapsedMs: number, estimateSeconds: number | null | undefined): number => {
  const duration = estimateMs(estimateSeconds);
  if (duration <= 0) return 100;
  if (!Number.isFinite(elapsedMs) || elapsedMs < 0) return 0;
  const clearReady = elapsedMs >= minimumAttemptElapsedMs(estimateSeconds, 100, true);
  const share = (elapsedMs + PLAUSIBILITY_SLACK_MS) / duration;
  if (share >= 1) return clearReady ? 100 : 99.999;
  const p = (share * GD_SPEED_RATIO) / (1 - share + share * GD_SPEED_RATIO);
  // Keep the cap strictly below a clear until a clear itself is plausible.
  return Math.min(clearReady ? 100 : 99.999, Math.floor(p * 100_000) / 1_000);
};
