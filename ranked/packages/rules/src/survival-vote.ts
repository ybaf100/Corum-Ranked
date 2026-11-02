import { RankedDomainError } from "./errors.js";
import type { SurvivalDemonDifficulty, SurvivalMapRange } from "./survival-maps.js";

/** One preference per player for the whole match. Never shown per player and
 * never announced: clients only learn how many players have voted. */
export type SurvivalDifficultyChoice = "EASY" | "NORMAL" | "HARD";
export type SurvivalDifficultyBallot = SurvivalDifficultyChoice | "NONE";
export const SURVIVAL_DIFFICULTY_CHOICES: readonly SurvivalDifficultyChoice[] = ["EASY", "NORMAL", "HARD"];
export const SURVIVAL_DIFFICULTY_BALLOTS: readonly SurvivalDifficultyBallot[] = ["EASY", "NORMAL", "HARD", "NONE"];
/** MATCH FOUND (1 s) and the roster reveal play before the cards appear. */
export const SURVIVAL_VOTE_OPENS_MS = 4_000;
export const SURVIVAL_VOTE_MS = 10_000;
/** Cards fade and the board returns before map selection is presented. */
export const SURVIVAL_VOTE_OUTRO_MS = 1_500;
/** Longest intro with a vote nobody answers: reveal, full window and outro. */
export const SURVIVAL_VOTE_INTRO_MS = SURVIVAL_VOTE_OPENS_MS + SURVIVAL_VOTE_MS + SURVIVAL_VOTE_OUTRO_MS;
/** Rank weights in permille: 1st 55 %, 2nd 35 %, 3rd 10 % instead of 33/33/33. */
export const SURVIVAL_VOTE_RANK_WEIGHTS: readonly number[] = [550, 350, 100];

export interface SurvivalDifficultyVote {
  opensAt: number;
  closesAt: number;
  /** Server secret: resolves "NONE", missing and bot ballots. Never in a client view. */
  seed: string;
  ballots: Record<string, SurvivalDifficultyBallot>;
  resolvedAt: number | null;
  /** Permille per difficulty slice, summing to 1000. Never in a client view. */
  weights: Record<SurvivalDifficultyChoice, number> | null;
}

/** FNV-1a: deterministic per (seed, participant), opaque without the seed. */
function hash32(value: string): number {
  let hash = 0x811c9dc5;
  for (let i = 0; i < value.length; i += 1) {
    hash ^= value.charCodeAt(i);
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash >>> 0;
}

export function survivalOpenDifficultyVote(createdAt: number, seed: string): SurvivalDifficultyVote {
  if (!Number.isFinite(createdAt) || !/^[a-f0-9]{16,128}$/.test(seed))
    throw new RankedDomainError("INVALID_CONFIG", "Invalid Survival difficulty vote seed");
  const opensAt = createdAt + SURVIVAL_VOTE_OPENS_MS;
  return { opensAt, closesAt: opensAt + SURVIVAL_VOTE_MS, seed, ballots: {}, resolvedAt: null, weights: null };
}

/** "Select none" (and an absent ballot) is a uniformly random hidden vote. */
export function survivalEffectiveBallot(vote: SurvivalDifficultyVote, participantId: string): SurvivalDifficultyChoice {
  const ballot = vote.ballots[participantId] ?? "NONE";
  if (ballot !== "NONE") return ballot;
  return SURVIVAL_DIFFICULTY_CHOICES[hash32(`${vote.seed}:${participantId}`) % 3]!;
}

/** Bots appear to vote at a stable moment inside the window (count only). */
export function survivalBotVotedAt(vote: SurvivalDifficultyVote, participantId: string): number {
  return vote.opensAt + 1_000 + (hash32(`${vote.seed}:bot:${participantId}`) % 7_000);
}

/** Vote counts set the ranking; ranks receive 55/35/10. Tied ranks share their
 * combined weight equally (permille stays integral and sums to exactly 1000). */
export function survivalVoteWeights(counts: Readonly<Record<SurvivalDifficultyChoice, number>>): Record<SurvivalDifficultyChoice, number> {
  for (const choice of SURVIVAL_DIFFICULTY_CHOICES)
    if (!Number.isSafeInteger(counts[choice]) || counts[choice] < 0) throw new RankedDomainError("INVALID_CONFIG", "Invalid vote count");
  const ordered = [...SURVIVAL_DIFFICULTY_CHOICES].sort((a, b) => counts[b] - counts[a]);
  const weights = { EASY: 0, NORMAL: 0, HARD: 0 } as Record<SurvivalDifficultyChoice, number>;
  let index = 0;
  while (index < ordered.length) {
    let end = index;
    while (end + 1 < ordered.length && counts[ordered[end + 1]!] === counts[ordered[index]!]) end += 1;
    const share = SURVIVAL_VOTE_RANK_WEIGHTS.slice(index, end + 1).reduce((sum, w) => sum + w, 0);
    const size = end - index + 1;
    for (let k = index; k <= end; k += 1) weights[ordered[k]!] = Math.floor(share / size);
    // Remainder permille goes to the harder slice of the tie, deterministically.
    const remainder = share - Math.floor(share / size) * size;
    if (remainder) {
      const hardest = ordered.slice(index, end + 1).sort((a, b) => SURVIVAL_DIFFICULTY_CHOICES.indexOf(b) - SURVIVAL_DIFFICULTY_CHOICES.indexOf(a))[0]!;
      weights[hardest] += remainder;
    }
    index = end + 1;
  }
  return weights;
}

export function survivalResolveDifficultyVote(vote: SurvivalDifficultyVote, participantIds: readonly string[], now: number): SurvivalDifficultyVote {
  if (vote.resolvedAt !== null) return vote;
  const counts = { EASY: 0, NORMAL: 0, HARD: 0 } as Record<SurvivalDifficultyChoice, number>;
  for (const id of participantIds) counts[survivalEffectiveBallot(vote, id)] += 1;
  return { ...vote, resolvedAt: now, weights: survivalVoteWeights(counts) };
}

/** Draw order for one map: the weighted pick first, then the remaining slices
 * by weight (fallbacks when a slice has no READY candidate). `roll` is in [0,1000). */
export function survivalSliceOrder(weights: Readonly<Record<SurvivalDifficultyChoice, number>> | null, roll: number): SurvivalDifficultyChoice[] {
  if (!weights) return ["NORMAL", "HARD", "EASY"];
  if (!Number.isInteger(roll) || roll < 0 || roll >= 1000) throw new RankedDomainError("INVALID_CONFIG", "Slice roll out of range");
  let cursor = roll, first: SurvivalDifficultyChoice = "HARD";
  for (const choice of SURVIVAL_DIFFICULTY_CHOICES) {
    if (cursor < weights[choice]) { first = choice; break; }
    cursor -= weights[choice];
  }
  const rest = SURVIVAL_DIFFICULTY_CHOICES.filter(c => c !== first)
    .sort((a, b) => weights[b] - weights[a] || SURVIVAL_DIFFICULTY_CHOICES.indexOf(b) - SURVIVAL_DIFFICULTY_CHOICES.indexOf(a));
  return [first, ...rest];
}

type Unit = { stars: number } | { demon: SurvivalDemonDifficulty };
/** Splits an allowed range (stars ascending, then demons ascending) into easy,
 * normal and hard slices. One unit: all slices equal. Two: normal spans both. */
export function survivalSliceRange(range: SurvivalMapRange, slice: SurvivalDifficultyChoice): SurvivalMapRange {
  const units: Unit[] = [];
  if (range.minimumStars !== null && range.maximumStars !== null)
    for (let stars = range.minimumStars; stars <= range.maximumStars; stars += 1) units.push({ stars });
  for (const demon of range.demons) units.push({ demon });
  if (!units.length) return structuredClone(range);
  const edge = Math.max(1, Math.floor(units.length / 3));
  const easy = units.slice(0, edge), hard = units.slice(units.length - edge);
  const middle = units.slice(edge, units.length - edge);
  const chosen = slice === "EASY" ? easy : slice === "HARD" ? hard : middle.length ? middle : units;
  const stars = chosen.filter((u): u is { stars: number } => "stars" in u).map(u => u.stars);
  return {
    minimumStars: stars.length ? Math.min(...stars) : null,
    maximumStars: stars.length ? Math.max(...stars) : null,
    demons: chosen.filter((u): u is { demon: SurvivalDemonDifficulty } => "demon" in u).map(u => u.demon),
  };
}
