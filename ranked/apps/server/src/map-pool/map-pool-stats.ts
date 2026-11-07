// Solo map pool statistics. Pure functions over settled PVP rows so the
// comeback and per-player rules stay testable without a database.

export type Side = "A" | "B";

/** Normal round comeback: the winner trailed by at least this many points. */
export const NORMAL_COMEBACK_POINTS = 50;
/** Death Match comeback: the winner trailed by at least this many progress points. */
export const DEATHMATCH_COMEBACK_PERCENT = 10;
export const DEATH_BIN_COUNT = 10;

// GD difficulty order for the default (easiest first) list order.
const DIFFICULTY_ORDER = [
  "auto", "easy", "normal", "hard", "harder", "insane",
  "easy demon", "medium demon", "hard demon", "insane demon", "extreme demon",
];
export const difficultyRank = (difficulty: string): number => {
  const name = difficulty.trim().toLowerCase().replace(/[_\s]+/g, " ");
  if (name === "demon") return DIFFICULTY_ORDER.indexOf("hard demon");
  const index = DIFFICULTY_ORDER.indexOf(name);
  return index < 0 ? 99 : index;
};

/**
 * Display difficulty order from GD metadata. The Solo map sheet's own
 * "difficulty" is a numeric Corum rating, never a GD label.
 */
export const mapDifficultyRank = (gdDifficulty: string | null | undefined, kind: string | undefined, stars: number | null | undefined): number => {
  const label = gdDifficulty ? difficultyRank(gdDifficulty) : 99;
  if (label !== 99) return label;
  if (kind === "DEMON") return difficultyRank("Hard Demon");
  if (typeof stars === "number" && stars > 0)
    return stars >= 8 ? difficultyRank("Insane") : stars >= 6 ? difficultyRank("Harder") : stars >= 4 ? difficultyRank("Hard")
      : stars === 3 ? difficultyRank("Normal") : stars === 2 ? difficultyRank("Easy") : difficultyRank("Auto");
  return 99;
};

export interface RoundRow {
  readonly id: string;
  readonly result: "A" | "B" | "DRAW";
  readonly effectiveTier: string;
  readonly playerA: string;
  readonly playerB: string;
}
export interface AttemptRow {
  readonly roundId: string;
  readonly playerId: string;
  readonly sequence: number;
  readonly endedAt: number; // epoch ms, server time
  readonly progress: number;
  readonly cleared: boolean;
  readonly score: number;
}
export interface DeathmatchRow {
  readonly id: string;
  readonly playerA: string;
  readonly playerB: string;
  readonly winner: string;
  readonly startedAt: number | null;
  readonly finishedAt: number | null;
}
export interface DeathmatchAttemptRow {
  readonly deathmatchId: string;
  readonly playerId: string;
  readonly endedAt: number;
  readonly progress: number;
  readonly cleared: boolean;
}

const rate = (count: number, total: number): number | null =>
  total > 0 ? Math.round((count / total) * 1000) / 10 : null;
const round1 = (value: number): number => Math.round(value * 10) / 10;
const sideOf = (row: { playerA: string; playerB: string }, player: string): Side | null =>
  player === row.playerA ? "A" : player === row.playerB ? "B" : null;
const byTime = <T extends { endedAt: number }>(a: T, b: T): number => a.endedAt - b.endedAt;
const groupBy = <T>(rows: readonly T[], key: (row: T) => string): Map<string, T[]> => {
  const groups = new Map<string, T[]>();
  for (const row of rows) {
    const id = key(row);
    const list = groups.get(id);
    if (list) list.push(row); else groups.set(id, [row]);
  }
  return groups;
};

/** Largest point deficit the eventual winner had at any moment of the round. */
export const normalRoundWinnerDeficit = (round: RoundRow, attempts: readonly AttemptRow[]): number => {
  if (round.result === "DRAW") return 0;
  const totals: Record<Side, number> = { A: 0, B: 0 };
  const loser: Side = round.result === "A" ? "B" : "A";
  let deficit = 0;
  for (const attempt of [...attempts].sort(byTime)) {
    const side = sideOf(round, attempt.playerId);
    if (!side) continue;
    totals[side] += attempt.score;
    deficit = Math.max(deficit, totals[loser] - totals[round.result]);
  }
  return round1(deficit);
};

/** Largest best-progress deficit the Death Match winner had at any moment. */
export const deathmatchWinnerDeficit = (match: DeathmatchRow, attempts: readonly DeathmatchAttemptRow[]): number => {
  const winner = sideOf(match, match.winner);
  if (!winner) return 0;
  const loser: Side = winner === "A" ? "B" : "A";
  const best: Record<Side, number> = { A: 0, B: 0 };
  let deficit = 0;
  for (const attempt of [...attempts].sort(byTime)) {
    const side = sideOf(match, attempt.playerId);
    if (!side) continue;
    best[side] = Math.max(best[side], attempt.cleared ? 100 : attempt.progress);
    deficit = Math.max(deficit, best[loser] - best[winner]);
  }
  return round1(deficit);
};

export interface MapDetailInput {
  readonly rounds: readonly RoundRow[];
  readonly attempts: readonly AttemptRow[];
  readonly deathmatches: readonly DeathmatchRow[];
  readonly deathmatchAttempts: readonly DeathmatchAttemptRow[];
  readonly names: ReadonlyMap<string, string>;
}

export interface MapDetailStats {
  readonly rounds: number;
  readonly playerRounds: number;
  readonly clearRate: number | null;
  readonly averageBestPercent: number | null;
  readonly averageAttempts: number | null;
  readonly firstAttemptClears: number;
  readonly firstAttemptClearRate: number | null;
  /** Share (%) of non-clear deaths in each 10% progress bin; counts alongside. */
  readonly deathBins: readonly number[];
  readonly deathCounts: readonly number[];
  readonly deaths: number;
  readonly tiers: readonly { readonly tier: string; readonly rounds: number; readonly share: number }[];
  readonly comeback: {
    readonly normal: { readonly eligible: number; readonly count: number; readonly rate: number | null };
    readonly deathmatch: { readonly eligible: number; readonly count: number; readonly rate: number | null };
  };
  readonly deathmatches: number;
  readonly fastestClear: { readonly attempt: number; readonly player: string } | null;
  readonly longestDeathmatch: { readonly seconds: number; readonly winner: string } | null;
}

const BASE_TIERS = ["RED", "AQUA", "BRONZE", "SILVER", "GOLD"] as const;
const baseTier = (tier: string): string => tier.replace(/[0-9]+$/, "");

export const mapDetailStats = (input: MapDetailInput): MapDetailStats => {
  const attemptsByRound = groupBy(input.attempts, (attempt) => attempt.roundId);
  let playerRounds = 0, clears = 0, bestSum = 0, attemptSum = 0, firstClears = 0;
  let fastest: { attempt: number; endedAt: number; player: string } | null = null;
  const deathCounts = Array.from({ length: DEATH_BIN_COUNT }, () => 0);
  let normalEligible = 0, normalComebacks = 0;
  const tierCounts = new Map<string, number>();
  for (const round of input.rounds) {
    const attempts = attemptsByRound.get(round.id) ?? [];
    const tier = baseTier(round.effectiveTier);
    tierCounts.set(tier, (tierCounts.get(tier) ?? 0) + 1);
    if (round.result !== "DRAW") {
      normalEligible += 1;
      if (normalRoundWinnerDeficit(round, attempts) >= NORMAL_COMEBACK_POINTS) normalComebacks += 1;
    }
    for (const player of [round.playerA, round.playerB]) {
      playerRounds += 1;
      const own = attempts.filter((attempt) => attempt.playerId === player).sort((a, b) => a.sequence - b.sequence);
      attemptSum += own.length;
      bestSum += own.reduce((best, attempt) => Math.max(best, attempt.cleared ? 100 : attempt.progress), 0);
      const firstClear = own.findIndex((attempt) => attempt.cleared);
      if (firstClear >= 0) {
        clears += 1;
        if (firstClear === 0) firstClears += 1;
        const clear = own[firstClear]!;
        if (!fastest || firstClear + 1 < fastest.attempt || (firstClear + 1 === fastest.attempt && clear.endedAt < fastest.endedAt))
          fastest = { attempt: firstClear + 1, endedAt: clear.endedAt, player };
      }
      for (const attempt of own) {
        if (attempt.cleared) continue;
        deathCounts[Math.min(DEATH_BIN_COUNT - 1, Math.max(0, Math.floor(attempt.progress / 10)))]! += 1;
      }
    }
  }
  const deaths = deathCounts.reduce((sum, count) => sum + count, 0);
  const deathmatchAttempts = groupBy(input.deathmatchAttempts, (attempt) => attempt.deathmatchId);
  let deathmatchEligible = 0, deathmatchComebacks = 0;
  let longest: { seconds: number; winner: string } | null = null;
  for (const match of input.deathmatches) {
    deathmatchEligible += 1;
    if (deathmatchWinnerDeficit(match, deathmatchAttempts.get(match.id) ?? []) >= DEATHMATCH_COMEBACK_PERCENT)
      deathmatchComebacks += 1;
    if (match.startedAt !== null && match.finishedAt !== null && match.finishedAt >= match.startedAt) {
      const seconds = Math.round((match.finishedAt - match.startedAt) / 1000);
      if (!longest || seconds > longest.seconds) longest = { seconds, winner: input.names.get(match.winner) ?? "?" };
    }
  }
  const tiers = [...tierCounts.entries()]
    .sort(([a], [b]) => {
      const ia = BASE_TIERS.indexOf(a as (typeof BASE_TIERS)[number]), ib = BASE_TIERS.indexOf(b as (typeof BASE_TIERS)[number]);
      return (ia < 0 ? 99 : ia) - (ib < 0 ? 99 : ib) || a.localeCompare(b);
    })
    .map(([tier, rounds]) => ({ tier, rounds, share: rate(rounds, input.rounds.length) ?? 0 }));
  return {
    rounds: input.rounds.length,
    playerRounds,
    clearRate: rate(clears, playerRounds),
    averageBestPercent: playerRounds > 0 ? round1(bestSum / playerRounds) : null,
    averageAttempts: playerRounds > 0 ? round1(attemptSum / playerRounds) : null,
    firstAttemptClears: firstClears,
    firstAttemptClearRate: rate(firstClears, playerRounds),
    deathBins: deathCounts.map((count) => rate(count, deaths) ?? 0),
    deathCounts,
    deaths,
    tiers,
    comeback: {
      normal: { eligible: normalEligible, count: normalComebacks, rate: rate(normalComebacks, normalEligible) },
      deathmatch: { eligible: deathmatchEligible, count: deathmatchComebacks, rate: rate(deathmatchComebacks, deathmatchEligible) },
    },
    deathmatches: input.deathmatches.length,
    fastestClear: fastest ? { attempt: fastest.attempt, player: input.names.get(fastest.player) ?? "?" } : null,
    longestDeathmatch: longest,
  };
};

export interface MyMapRecord {
  readonly rounds: number;
  readonly wins: number;
  readonly losses: number;
  readonly draws: number;
  readonly bestPercent: number | null;
}

export const myMapRecord = (playerId: string, rounds: readonly RoundRow[], attempts: readonly AttemptRow[]): MyMapRecord => {
  let count = 0, wins = 0, losses = 0, draws = 0;
  let best: number | null = null;
  const mine = new Set<string>();
  for (const round of rounds) {
    const side = sideOf(round, playerId);
    if (!side) continue;
    mine.add(round.id);
    count += 1;
    if (round.result === "DRAW") draws += 1; else if (round.result === side) wins += 1; else losses += 1;
  }
  for (const attempt of attempts) {
    if (attempt.playerId !== playerId || !mine.has(attempt.roundId)) continue;
    best = Math.max(best ?? 0, attempt.cleared ? 100 : attempt.progress);
  }
  return { rounds: count, wins, losses, draws, bestPercent: count === 0 ? null : round1(best ?? 0) };
};

export { rate as percentOf };
