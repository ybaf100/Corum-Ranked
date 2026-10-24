import type { SurvivalMatch, SurvivalRound, SurvivalRoundPlayer } from "./survival-match.js";
import { survivalMilliPercent } from "./survival-match.js";
import { bestPercentOfThree } from "./deathmatch.js";
import type { OneShotAchievementId } from "./achievements.js";

export interface SurvivalAchievementFacts {
  readonly matchId: string; readonly completedAt: number;
  readonly won: boolean; readonly podium: boolean; readonly finalist: boolean;
  readonly survivedRounds: number; readonly soleRoundWins: number;
  readonly roundWins: number; readonly playedRounds: number; readonly perfectRounds: number;
  readonly oneShots: readonly OneShotAchievementId[];
}
export interface SurvivalAchievementMetrics {
  survival_matches: number; survival_wins: number; survival_podiums: number; survival_finals: number;
  survival_round_survives: number; survival_round_wins: number;
  survival_rounds_played: number; survival_perfect_rounds: number;
  current_survival_podium_streak: number; survival_podium_streak: number;
  current_survival_win_streak: number; survival_win_streak: number;
}
export const EMPTY_SURVIVAL_ACHIEVEMENT_METRICS: SurvivalAchievementMetrics = {
  survival_matches: 0, survival_wins: 0, survival_podiums: 0, survival_finals: 0, survival_round_survives: 0, survival_round_wins: 0,
  survival_rounds_played: 0, survival_perfect_rounds: 0,
  current_survival_podium_streak: 0, survival_podium_streak: 0, current_survival_win_streak: 0, survival_win_streak: 0,
};
const score = (r: SurvivalRound, p: SurvivalRoundPlayer): number => r.phase !== "TIEBREAK" ? Math.trunc(p.highestProgress) :
  survivalMilliPercent(bestPercentOfThree(Array.from({ length: 3 }, (_, i) => ({
    progressPercent: survivalMilliPercent(p.attempts[i]?.highestProgress ?? 0) / 1000, cleared: false,
  }))));
const validRound = (r: SurvivalRound): boolean => r.finishedAt !== null && Number.isFinite(r.finishedAt) &&
  r.startsAt !== null && Number.isFinite(r.startsAt) && r.finishedAt >= r.startsAt && r.map !== null;
const placed = (p: SurvivalRoundPlayer): boolean => !p.forfeited && p.rank !== null && Number.isSafeInteger(p.rank) && p.rank > 0;
const validAttempts = (r: SurvivalRound, p: SurvivalRoundPlayer) => p.enteredAt === null ? [] : p.attempts.filter(a =>
  a.endedAt !== null && a.startedAt >= r.startsAt! && a.startedAt <= a.endedAt && a.endedAt <= r.finishedAt!);
const perfect = (r: SurvivalRound, p: SurvivalRoundPlayer): boolean => placed(p) && validAttempts(r, p).some(a => a.highestProgress === 100);

/** Only stored authoritative results of real participants, never client claims. */
export function survivalAchievementFacts(match: SurvivalMatch, playerId: string, debugSource = false): SurvivalAchievementFacts | null {
  if ((match.online !== true && !debugSource) || match.state !== "FINAL_RESULT" || match.finishedAt === null || !Number.isFinite(match.finishedAt) ||
      (match.completionKind && match.completionKind !== "BOTS_ONLY_RESULT" && !(debugSource && ["DEBUG_FORCE", "DEBUG_HUMAN_ELIMINATION"].includes(match.completionKind)))) return null;
  const own = match.players.find(p => p.playerId === playerId && p.kind === "HUMAN");
  if (!own || own.forfeitReason || own.finalRank === null || !Number.isSafeInteger(own.finalRank) || own.finalRank < 1) return null;
  const rounds = match.rounds.filter(validRound);
  const entries = rounds.flatMap(r => { const p = r.players.find(p => p.participantId === own.participantId); return p ? [{ r, p }] : []; });
  const won = own.finalRank === 1 && (match.winnerIds ?? (match.winnerId ? [match.winnerId] : [])).includes(own.participantId);
  const podium = own.finalRank <= 3;
  const sole = ({ r, p }: { r: SurvivalRound; p: SurvivalRoundPlayer }) => placed(p) && p.rank === 1 && r.players.filter(v => placed(v) && v.rank === 1).length === 1;
  const oneShots = new Set<OneShotAchievementId>(["survival_first_match"]);
  if (podium) oneShots.add("survival_first_podium");
  if (won) oneShots.add("survival_first_win");
  for (const { r, p } of entries) {
    if (perfect(r, p)) oneShots.add("survival_round_perfect");
    // A FINAL tie qualifies for a newly created TIEBREAK. A TIEBREAK tie is a
    // separate event, tested with the same 3-decimal scoring as the reducer.
    if ((r.phase === "FINAL" || r.phase === "TIEBREAK") && placed(p) && p.qualified && p.rank === 1) {
      const tied = r.players.filter(v => placed(v) && v.qualified && score(r, v) === score(r, p));
      const next = match.rounds.find(v => v.ordinal === r.ordinal + 1 && v.phase === "TIEBREAK" && v.players.some(v => v.participantId === p.participantId));
      if (tied.length > 1 && next) oneShots.add(r.phase === "FINAL" ? "survival_deathmatch_entry" : "survival_secret_2");
    }
    if (r.phase === "FINAL" || r.phase === "TIEBREAK" || r.target >= r.players.length) continue;
    const boundary = r.cutoffScore === null ? [] : r.players.filter(v => placed(v) && v.qualified && score(r, v) === r.cutoffScore);
    const boundaryRank = boundary.length ? Math.min(...boundary.map(v => v.rank!)) : null;
    if (placed(p) && p.qualified && boundaryRank !== null && p.rank === boundaryRank) oneShots.add("survival_cutoff_escape");
    const checkpoint = r.achievementCheckpoint, evidence = checkpoint?.standings.find(v => v.participantId === p.participantId);
    if (placed(p) && p.qualified && checkpoint?.status === "CAPTURED" && checkpoint.at === r.endsAt! - 10_000 &&
        evidence?.atRisk && evidence.rescuedAt !== null && evidence.rescuedAt >= checkpoint.at && evidence.rescuedAt < r.endsAt!) oneShots.add("survival_secret_1");
  }
  const finalEntries = entries.filter(({ r }) => r.phase === "FINAL" || r.phase === "TIEBREAK");
  const decisive = finalEntries.at(-1);
  if (won && decisive && perfect(decisive.r, decisive.p)) oneShots.add("survival_final_perfect_win");
  if (won && entries.length > 0 && entries.length === rounds.length && entries.every(sole)) oneShots.add("survival_clean_champion");
  const firstFinal = rounds.find(r => r.phase === "FINAL");
  const qualifier = firstFinal ? rounds.filter(r => r.ordinal < firstFinal.ordinal && r.phase !== "FINAL" && r.phase !== "TIEBREAK").at(-1) : undefined;
  // The requested seed is exactly official rank 3, including official ties;
  // being the lowest of two finalists does not satisfy this condition.
  if (won && firstFinal && qualifier) {
    const seed = qualifier.players.find(p => p.participantId === own.participantId);
    if (seed && placed(seed) && seed.qualified && seed.rank === 3) oneShots.add("survival_underdog");
  }
  return {
    matchId: match.id, completedAt: match.finishedAt, won, podium, finalist: finalEntries.length > 0,
    survivedRounds: entries.filter(({ p }) => placed(p) && p.qualified).length,
    soleRoundWins: entries.filter(sole).length,
    roundWins: entries.filter(({ p }) => placed(p) && p.rank === 1).length,
    playedRounds: entries.filter(({ r, p }) => placed(p) && validAttempts(r, p).length > 0).length,
    perfectRounds: entries.filter(({ r, p }) => perfect(r, p)).length,
    oneShots: [...oneShots],
  };
}

/** Recompute in completion order to preserve streaks during late recovery. */
export function summarizeSurvivalAchievements(facts: readonly SurvivalAchievementFacts[]): SurvivalAchievementMetrics {
  const out = { ...EMPTY_SURVIVAL_ACHIEVEMENT_METRICS };
  const unique = new Map(facts.map(f => [f.matchId, f]));
  for (const f of [...unique.values()].sort((a, b) => a.completedAt - b.completedAt || a.matchId.localeCompare(b.matchId))) {
    ++out.survival_matches; out.survival_wins += Number(f.won); out.survival_podiums += Number(f.podium); out.survival_finals += Number(f.finalist);
    out.survival_round_survives += f.survivedRounds; out.survival_round_wins += f.roundWins;
    out.survival_rounds_played += f.playedRounds; out.survival_perfect_rounds += f.perfectRounds;
    out.current_survival_podium_streak = f.podium ? out.current_survival_podium_streak + 1 : 0;
    out.current_survival_win_streak = f.won ? out.current_survival_win_streak + 1 : 0;
    out.survival_podium_streak = Math.max(out.survival_podium_streak, out.current_survival_podium_streak);
    out.survival_win_streak = Math.max(out.survival_win_streak, out.current_survival_win_streak);
  }
  return out;
}
