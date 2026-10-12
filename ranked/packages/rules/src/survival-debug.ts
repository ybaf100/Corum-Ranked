import { RankedDomainError } from "./errors.js";
import { SeededRandom } from "./random.js";
import type { SurvivalConfig } from "./survival.js";
import { survivalDifficultyForAverage, type SurvivalDifficultyGroup } from "./survival-maps.js";
import { survivalCurrentRound, survivalMilliPercent, survivalRankScores, type SurvivalMatch } from "./survival-match.js";

/** Each draw is a function of the persisted seed, round and bot identity.
 * Replaying an event after a process restart cannot draw a different score. */
export function survivalDebugSeed(seed: number, key: string): number {
  let value = (seed >>> 0) ^ 2166136261;
  for (const character of key) value = Math.imul(value ^ character.charCodeAt(0), 16777619);
  return value >>> 0;
}
export interface SurvivalBotEvent { index: number; offsetMs: number; best: number; changed: boolean; }
// Box–Muller draws an independent N(50, 20²) candidate. Clamping happens
// before integer rounding; a new candidate never replaces a better record.
function normalBotProgress(random: SeededRandom): number {
  const u = 1 - random.next(), v = random.next();
  const normal = Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v);
  return Math.round(Math.max(0, Math.min(100, 50 + 20 * normal)));
}
export function survivalBotPlan(seed: number, roundKey: string, botIndex: number): readonly SurvivalBotEvent[] {
  if (!Number.isInteger(botIndex) || botIndex < 0 || botIndex >= 15)
    throw new RankedDomainError("INVALID_CONFIG", "Survival has exactly 15 simulated bots");
  const random = new SeededRandom(survivalDebugSeed(seed, `${roundKey}:score:${botIndex}`));
  let best = normalBotProgress(random);
  const events: SurvivalBotEvent[] = [{ index: 0, offsetMs: 0, best, changed: true }];
  for (const [index, seconds] of [20, 50, 80, 110, 140].entries()) {
    const changed = random.next() < 0.4;
    if (changed) best = Math.max(best, normalBotProgress(random));
    events.push({ index: index + 1, offsetMs: seconds * 1000, best, changed });
  }
  return events;
}
export function survivalBotTiebreak(seed: number, roundKey: string, botIndex: number): readonly number[] {
  const random = new SeededRandom(survivalDebugSeed(seed, `${roundKey}:deathmatch:${botIndex}`));
  return Array.from({ length: 3 }, () => Math.floor(random.next() * 100001) / 1000);
}

export function survivalDebugLp(seed: number, humanLp: number, group: SurvivalDifficultyGroup,
  config: SurvivalConfig) {
  if (!Number.isSafeInteger(humanLp) || humanLp < 0)
    throw new RankedDomainError("INVALID_CONFIG", "Invalid human Survival LP");
  const lower = group === "RED_BRONZE" ? 0 : config.tierBands.find(b => b.group === group)?.minimumLp;
  const upper = group === "RED_BRONZE" ? config.tierBands.find(b => b.group === "SILVER")?.minimumLp
    : group === "SILVER" ? config.tierBands.find(b => b.group === "GOLD")?.minimumLp : null;
  if (lower === undefined || upper === undefined || (upper !== null && upper <= lower))
    throw new RankedDomainError("INVALID_CONFIG", "Missing Survival difficulty boundaries");
  const target = upper === null ? lower + 150 : Math.floor((lower + upper) / 2);
  const virtualHuman = upper !== null && humanLp > upper * 16 - 1;
  const humanSnapshotLp = virtualHuman ? target : humanLp;
  const total = Math.max(target * 16, humanSnapshotLp);
  const budget = total - humanSnapshotLp;
  const random = new SeededRandom(survivalDebugSeed(seed, "bot-lp"));
  const weights = Array.from({ length: 15 }, () => 0.25 + random.next());
  const sum = weights.reduce((a, b) => a + b, 0);
  const bots = weights.map(w => Math.floor(budget * w / sum));
  let remainder = budget - bots.reduce((a, b) => a + b, 0);
  for (let i = 0; remainder > 0; i++, remainder--) bots[i % 15]!++;
  const averageLp = (humanSnapshotLp + bots.reduce((a, b) => a + b, 0)) / 16;
  if (survivalDifficultyForAverage(averageLp, config.tierBands) !== group)
    throw new RankedDomainError("INVALID_CONFIG", "Could not construct the requested debug average tier");
  return { humanSnapshotLp, virtualHuman, bots, averageLp };
}

/** Explicit password-protected development operation. It never changes the
 * public engine's round adjudication or accepts a client-supplied LP delta. */
export function forceSurvivalDebugRank(before: SurvivalMatch, humanId: string, rank: number, now: number): SurvivalMatch {
  if (!Number.isInteger(rank) || rank < 1 || rank > 16 || before.players.length !== 16 ||
      !before.players.some(p => p.participantId === humanId && p.kind === "HUMAN"))
    throw new RankedDomainError("INVALID_MATCH_TRANSITION", "Invalid Force rank");
  if (["FINAL_RESULT", "CANCELLED"].includes(before.state)) return before;
  const match = structuredClone(before), round = survivalCurrentRound(match);
  const scores = survivalRankScores(match.players.filter(p => p.participantId !== humanId).map(p => ({participantId:p.participantId,
    score: round.players.find(r => r.participantId === p.participantId)?.highestProgress ?? p.eliminationScore})));
  const order = scores.map(p => p.participantId); order.splice(rank - 1, 0, humanId);
  order.forEach((id, index) => { const p = match.players.find(p => p.participantId === id)!;
    p.finalRank=index+1; p.eliminationStage=index ? round.ordinal : null; });
  match.winnerId=order[0]!; match.finishedAt=now; match.updatedAt=now; match.revision++;
  match.state="FINAL_RESULT"; match.completionKind="DEBUG_FORCE"; round.finishedAt=now;
  if(match.presentationControlled)match.presentationGate=null;
  for (const p of round.players) {
    p.rank=match.players.find(v => v.participantId===p.participantId)!.finalRank;
    p.qualified=p.participantId===match.winnerId;
    for (const a of p.attempts) a.endedAt ??= now;
  }
  match.transitions.push({sequence:match.transitions.length+1,state:match.state,roundKey:round.key,at:now,reason:"DEBUG_FORCE_RANK"});
  return match;
}

/** User-confirmed debug shortcut: once the real player is eliminated (including
 * surrender), stop the remaining game and rank its current stage by recorded
 * best progress. Earlier eliminated stages stay below this stage. Bot first
 * place ties are retained; no bot-only rounds or Death Matches are generated. */
export function finishSurvivalDebugOnElimination(before:SurvivalMatch,humanId:string,now:number):SurvivalMatch {
  const human=before.players.find(p=>p.participantId===humanId);
  if(!human || human.eliminationStage===null || ["FINAL_RESULT","CANCELLED"].includes(before.state))return before;
  const match=structuredClone(before),round=survivalCurrentRound(match);
  for(const p of round.players) {
    const entrant=match.players.find(v=>v.participantId===p.participantId)!;
    entrant.eliminationStage=round.ordinal;
    entrant.eliminationScore=round.phase==="TIEBREAK"?survivalMilliPercent(p.highestProgress)/1000:p.highestProgress;
    for(const a of p.attempts)a.endedAt??=now;
  }
  const ordered=[...match.players].sort((a,b)=>(b.eliminationStage??0)-(a.eliminationStage??0) ||
    b.eliminationScore-a.eliminationScore || a.participantId.localeCompare(b.participantId));
  let rank=1;
  ordered.forEach((p,i)=>{
    if(i && (p.eliminationStage!==ordered[i-1]!.eliminationStage || p.eliminationScore!==ordered[i-1]!.eliminationScore))rank=i+1;
    p.finalRank=rank;
  });
  match.winnerIds=ordered.filter(p=>p.finalRank===1).map(p=>p.participantId);
  match.winnerId=match.winnerIds[0]!;
  match.state="FINAL_RESULT";match.completionKind="DEBUG_HUMAN_ELIMINATION";
  if(match.presentationControlled)match.presentationGate=null;
  match.finishedAt=now;match.updatedAt=now;match.revision++;round.finishedAt??=now;
  match.transitions.push({sequence:match.transitions.length+1,state:match.state,roundKey:round.key,at:now,reason:human.forfeitReason??"DEBUG_HUMAN_ELIMINATED"});
  return match;
}
