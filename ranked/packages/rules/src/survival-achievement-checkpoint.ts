import { survivalCurrentRound, survivalRankScores, type SurvivalMatch, type SurvivalRound } from "./survival-match.js";

/** A live, server-owned observation, never supplied by an event DTO. */
export interface SurvivalAchievementCheckpoint {
  at: number;
  observedAt: number;
  status: "CAPTURED" | "MISSED";
  cutoffScore: number | null;
  standings: { participantId: string; rank: number; score: number; atRisk: boolean; rescuedAt: number | null }[];
}
// One existing recovery tick of scheduling slack. The unchanged pre-boundary
// snapshot proves the logical boundary state; slack never admits later scores.
export const SURVIVAL_CHECKPOINT_SLACK_MS = 250;
const elimination = (r: SurvivalRound) => r.phase !== "FINAL" && r.phase !== "TIEBREAK" && r.target < r.players.length;
function standings(r: SurvivalRound) {
  return survivalRankScores(r.players.filter(p => !p.forfeited).map(p => ({
    participantId: p.participantId, score: Math.trunc(p.highestProgress),
  })));
}

/** Called under the match row lock BEFORE any adapters or incoming commands.
 * A late tick/restart records MISSED, not an invented past ranking. A previously
 * mutated post-boundary snapshot also cannot establish the earlier standings. */
export function observeSurvivalAchievementCheckpoint(before: SurvivalMatch, now: number): SurvivalMatch {
  if (!Number.isFinite(now) || now < before.updatedAt) return before;
  const r = survivalCurrentRound(before);
  if (before.state !== "PLAYING" || !elimination(r) || r.endsAt === null || r.startsAt === null || r.achievementCheckpoint) return before;
  const at = r.endsAt - 10_000;
  if (now < at) return before;
  const captured = now < r.endsAt && now <= at + SURVIVAL_CHECKPOINT_SLACK_MS && before.updatedAt <= at;
  const rows = captured ? standings(r) : [];
  const cutoff = rows[Math.min(r.target, rows.length) - 1]?.score ?? null;
  const match = structuredClone(before);
  survivalCurrentRound(match).achievementCheckpoint = {
    at, observedAt: now, status: captured ? "CAPTURED" : "MISSED", cutoffScore: cutoff,
    standings: rows.map(p => ({ ...p, atRisk: cutoff !== null && p.score < cutoff, rescuedAt: null })),
  };
  match.revision++; match.updatedAt = now;
  return match;
}

/** Only accepted live state can establish a recovery. Qualification at the
 * deadline alone cannot fabricate an earlier crossing into the passing group. */
export function observeSurvivalAchievementRecovery(match: SurvivalMatch, now: number): void {
  const r = survivalCurrentRound(match), checkpoint = r.achievementCheckpoint;
  if (match.state !== "PLAYING" || !checkpoint || checkpoint.status !== "CAPTURED" ||
      r.endsAt === null || now < checkpoint.at || now >= r.endsAt) return;
  const rows = standings(r), cutoff = rows[Math.min(r.target, rows.length) - 1]?.score;
  if (cutoff === undefined) return;
  for (const p of checkpoint.standings) {
    if (p.atRisk && p.rescuedAt === null && rows.some(v => v.participantId === p.participantId && v.score >= cutoff)) p.rescuedAt = now;
  }
}

/** Immutable adapter boundary; ordinary ticks allocate no extra match copy. */
export function recoverSurvivalAchievementCheckpoint(before: SurvivalMatch, now: number): SurvivalMatch {
  const r = survivalCurrentRound(before), checkpoint = r.achievementCheckpoint;
  if (before.state !== "PLAYING" || !checkpoint || checkpoint.status !== "CAPTURED" ||
      r.endsAt === null || now < checkpoint.at || now >= r.endsAt) return before;
  const rows = standings(r), cutoff = rows[Math.min(r.target, rows.length) - 1]?.score;
  if (cutoff === undefined || !checkpoint.standings.some(p => p.atRisk && p.rescuedAt === null &&
      rows.some(v => v.participantId === p.participantId && v.score >= cutoff))) return before;
  const match = structuredClone(before);
  observeSurvivalAchievementRecovery(match, now);
  match.revision++; match.updatedAt = now;
  return match;
}
