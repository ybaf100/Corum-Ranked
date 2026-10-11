import { bestPercentOfThree } from "./deathmatch.js";
import { RankedDomainError } from "./errors.js";
import { survivalPlacementLp, type SurvivalConfig } from "./survival.js";
import { survivalDifficultyForAverage, survivalMapAllowed, validateSurvivalEngineConfig,
  type SurvivalDifficultyGroup, type SurvivalMapPhase, type SurvivalVerifiedMap } from "./survival-maps.js";

export type SurvivalMatchState = "MAP_SELECTION" | "PREPARING" | "ENTERING" | "COUNTDOWN" |
  "PLAYING" | "ROUND_RESULT" | "FINAL_RESULT" | "CANCELLED";
export interface SurvivalEntrant {
  participantId: string;
  playerId: string | null;
  name: string;
  kind: "HUMAN" | "BOT";
  lp: number;
}
export interface SurvivalMatchPlayer extends SurvivalEntrant {
  eliminationStage: number | null;
  eliminationScore: number;
  forfeitReason: string | null;
  finalRank: number | null;
}
export interface SurvivalAttempt {
  id: string;
  sequence: number;
  startedAt: number;
  endedAt: number | null;
  highestProgress: number;
}
export interface SurvivalRoundPlayer {
  participantId: string;
  assetsReadyAt: number | null;
  preparationFailure: string | null;
  enteredAt: number | null;
  highestProgress: number;
  attempts: SurvivalAttempt[];
  rank: number | null;
  qualified: boolean;
  forfeited: boolean;
}
export interface SurvivalRound {
  key: string;
  ordinal: number;
  phase: SurvivalMapPhase;
  target: number;
  mapGeneration: number;
  map: SurvivalVerifiedMap | null;
  selectedMaps: SurvivalVerifiedMap[];
  preparationStartedAt: number | null;
  entryRequestedAt: number | null;
  firstEnteredAt: number | null;
  startsAt: number | null;
  endsAt: number | null;
  finishedAt: number | null;
  cutoffScore: number | null;
  players: SurvivalRoundPlayer[];
}
export interface SurvivalMatch {
  id: string;
  revision: number;
  state: SurvivalMatchState;
  configGeneration: string;
  config: SurvivalConfig;
  averageLp: number;
  difficultyGroup: SurvivalDifficultyGroup;
  createdAt: number;
  updatedAt: number;
  finishedAt: number | null;
  winnerId: string | null;
  cancellationReason: string | null;
  players: SurvivalMatchPlayer[];
  rounds: SurvivalRound[];
  transitions: { sequence: number; state: SurvivalMatchState; roundKey: string; at: number; reason: string }[];
}
export interface SurvivalRoundContext { roundKey: string; mapGeneration: number; levelId: number; }
export type SurvivalCommand =
  | { type: "TICK" }
  | { type: "MAP_SELECTED"; roundKey: string; mapGeneration: number; map: SurvivalVerifiedMap }
  | ({ type: "ASSETS_READY"; participantId: string; certificate: SurvivalVerifiedMap } & SurvivalRoundContext)
  | ({ type: "PREPARATION_FAILED"; participantId: string; reason: string } & SurvivalRoundContext)
  | ({ type: "ENTERED"; participantId: string } & SurvivalRoundContext)
  | ({ type: "ATTEMPT_START"; participantId: string; attemptId: string } & SurvivalRoundContext)
  | ({ type: "PROGRESS" | "ATTEMPT_END"; participantId: string; attemptId: string; progress: number } & SurvivalRoundContext)
  // This command is for the verified server provider, never an untrusted client report.
  | { type: "MAP_INVALIDATED"; roundKey: string; mapGeneration: number; reason: string }
  // Future session adapters establish responsibility; the engine only records
  // their trusted decision. No public forfeit route is exposed in M3.
  | { type: "FORFEIT"; roundKey: string; mapGeneration: number; participantIds: readonly string[]; reason: string };

function conflict(message: string): never { throw new RankedDomainError("INVALID_MATCH_TRANSITION", message); }
export const survivalCurrentRound = (match: SurvivalMatch): SurvivalRound => match.rounds[match.rounds.length - 1]!;
const terminal = (match: SurvivalMatch): boolean => match.state === "FINAL_RESULT" || match.state === "CANCELLED";
const activePlayers = (round: SurvivalRound) => round.players.filter(player => !player.forfeited);

/** Raw percentages for normal rounds; fixed integer milli-percent for Death Match.
 * Decimal shifting avoids the 1.005-style binary multiplication rounding error. */
export function survivalMilliPercent(value: number): number {
  if (!Number.isFinite(value) || value < 0 || value > 100) conflict("Progress must be between 0 and 100");
  const [digits, exponent = "0"] = String(value).split("e");
  return Math.round(Number(`${digits}e${Number(exponent) + 3}`));
}
export function survivalRankScores<T extends { participantId: string; score: number }>(scores: readonly T[]) {
  const sorted = [...scores].sort((a,b) => b.score - a.score || a.participantId.localeCompare(b.participantId));
  let rank = 0;
  return sorted.map((item,index) => {
    if (!index || item.score !== sorted[index - 1]!.score) rank = index + 1;
    return { ...item, rank };
  });
}
function roundScore(round: SurvivalRound, player: SurvivalRoundPlayer): number {
  if (round.phase !== "TIEBREAK") return player.highestProgress;
  const inputs = Array.from({length:3}, (_,i) => ({ progressPercent:
    survivalMilliPercent(player.attempts[i]?.highestProgress ?? 0) / 1000, cleared:false }));
  return survivalMilliPercent(bestPercentOfThree(inputs));
}
function transition(match: SurvivalMatch, state: SurvivalMatchState, at: number, reason: string): void {
  match.state = state;
  match.transitions.push({sequence:match.transitions.length + 1, state,
    roundKey:survivalCurrentRound(match).key, at, reason});
}
function addRound(match: SurvivalMatch, phase: SurvivalMapPhase, participants: readonly string[], at: number): void {
  const ordinal = match.rounds.length + 1;
  const target = phase === "ROUND_1" ? match.config.round1Qualifiers : phase === "ROUND_2" ? match.config.round2Qualifiers :
    phase === "ROUND_3" ? match.config.round3Qualifiers : 1;
  match.rounds.push({ key: `stage-${ordinal}`, ordinal, phase, target, mapGeneration:1, map:null, selectedMaps:[],
    preparationStartedAt:null, entryRequestedAt:null, firstEnteredAt:null, startsAt:null, endsAt:null, finishedAt:null,
    cutoffScore:null, players:participants.map(participantId => ({participantId,assetsReadyAt:null,
      preparationFailure:null,enteredAt:null,highestProgress:0,attempts:[],rank:null,qualified:false,forfeited:false})) });
  transition(match,"MAP_SELECTION",at,"NEW_ROUND");
}
export function createSurvivalMatch(id: string, entrants: readonly SurvivalEntrant[], config: SurvivalConfig,
  configGeneration: string, now: number): SurvivalMatch {
  const errors = validateSurvivalEngineConfig(config);
  if (errors.length) throw new RankedDomainError("INVALID_CONFIG",errors.join("; "));
  if (entrants.length !== 16 || new Set(entrants.map(p => p.participantId)).size !== 16 ||
    entrants.some(p => !p.participantId || p.participantId.length > 80 || !Number.isSafeInteger(p.lp) || p.lp < 0 ||
      !["HUMAN","BOT"].includes(p.kind) || (p.kind === "HUMAN" && !p.playerId)) ||
    new Set(entrants.filter(p => p.playerId).map(p => p.playerId)).size !== entrants.filter(p => p.playerId).length ||
    !configGeneration.trim() || !Number.isFinite(now)) conflict("M3 requires exactly 16 distinct valid entrants");
  const averageLp = entrants.reduce((sum,p) => sum + p.lp,0) / entrants.length;
  const match: SurvivalMatch = { id,revision:1,state:"MAP_SELECTION",configGeneration,config:structuredClone(config),averageLp,
    difficultyGroup:survivalDifficultyForAverage(averageLp,config.tierBands),createdAt:now,updatedAt:now,finishedAt:null,
    winnerId:null,cancellationReason:null,players:entrants.map(p => ({...p,eliminationStage:null,eliminationScore:0,
      forfeitReason:null,finalRank:null})),rounds:[],transitions:[] };
  addRound(match,"ROUND_1",entrants.map(p => p.participantId),now);
  return match;
}
function eliminate(match: SurvivalMatch, round: SurvivalRound, player: SurvivalRoundPlayer, score: number, reason: string | null): void {
  const entrant = match.players.find(p => p.participantId === player.participantId)!;
  if (entrant.eliminationStage !== null) return;
  entrant.eliminationStage = round.ordinal;
  entrant.eliminationScore = round.phase === "TIEBREAK" ? score / 1000 : score;
  entrant.forfeitReason = reason;
  player.qualified = false;
}
function finishMatch(match: SurvivalMatch, winnerId: string | null, at: number, reason: string): void {
  match.winnerId = winnerId; match.finishedAt = at;
  if (!winnerId) { match.cancellationReason=reason; transition(match,"CANCELLED",at,reason); return; }
  const ordered = [...match.players].sort((a,b) => {
    if (a.participantId === winnerId) return -1;
    if (b.participantId === winnerId) return 1;
    return (b.eliminationStage ?? 0) - (a.eliminationStage ?? 0) || b.eliminationScore - a.eliminationScore ||
      a.participantId.localeCompare(b.participantId);
  });
  let rank = 1;
  for (const [i,player] of ordered.entries()) {
    const previous = ordered[i - 1];
    if (i && (previous?.participantId === winnerId || player.eliminationStage !== previous?.eliminationStage ||
      player.eliminationScore !== previous?.eliminationScore)) rank=i+1;
    player.finalRank=rank;
  }
  transition(match,"FINAL_RESULT",at,reason);
}
function survivorsOrEnd(match: SurvivalMatch, round: SurvivalRound, at: number): boolean {
  const remaining=activePlayers(round);
  if (remaining.length > 1) return false;
  round.finishedAt=at;
  for (const player of remaining) player.qualified=true;
  finishMatch(match,remaining[0]?.participantId ?? null,at,"PREPARATION_FORFEITS");
  return true;
}
function forfeitUnready(match: SurvivalMatch, round: SurvivalRound, ready: (p: SurvivalRoundPlayer) => boolean,
  at: number, reason: string): void {
  for (const player of activePlayers(round).filter(p => !ready(p))) {
    player.forfeited=true; eliminate(match,round,player,roundScore(round,player),reason);
  }
  survivorsOrEnd(match,round,at);
}
function reroll(match: SurvivalMatch, round: SurvivalRound, at: number, reason: string): void {
  round.mapGeneration++; round.map=null; round.preparationStartedAt=null; round.entryRequestedAt=null;
  round.firstEnteredAt=null; round.startsAt=null; round.endsAt=null;
  for (const player of round.players) {player.assetsReadyAt=null; player.preparationFailure=null; player.enteredAt=null;}
  transition(match,"MAP_SELECTION",at,reason);
}
function finishRound(match: SurvivalMatch, round: SurvivalRound, at: number): void {
  if (round.finishedAt !== null) return;
  for (const player of round.players) for (const attempt of player.attempts) if (attempt.endedAt === null) attempt.endedAt=at;
  const ranked = survivalRankScores(activePlayers(round).map(p => ({participantId:p.participantId,score:roundScore(round,p)})));
  const cutoff = ranked[Math.min(round.target,ranked.length)-1]?.score ?? null;
  round.cutoffScore=cutoff; round.finishedAt=at;
  for (const item of ranked) {
    const player=round.players.find(p => p.participantId === item.participantId)!;
    player.rank=item.rank; player.qualified=cutoff !== null && item.score >= cutoff;
    if (!player.qualified) eliminate(match,round,player,item.score,null);
  }
  const qualified=round.players.filter(p => p.qualified && !p.forfeited);
  if (qualified.length === 1) { finishMatch(match,qualified[0]!.participantId,at,"SINGLE_WINNER"); return; }
  if (!qualified.length) { finishMatch(match,null,at,"NO_SURVIVORS"); return; }
  transition(match,"ROUND_RESULT",at,"CUTOFF_TIES_ADVANCE");
}
/** Timer boundaries are evaluated before accepting any command, including late
 * packets and duplicate reports. >= deadline is closed; preparation is not play time. */
function advanceTimers(match: SurvivalMatch, now: number): void {
  const round=survivalCurrentRound(match);
  if (match.state === "PREPARING" && round.preparationStartedAt !== null && now >= round.preparationStartedAt + 15_000) {
    const ready=activePlayers(round).filter(p => p.assetsReadyAt !== null);
    if (!ready.length) {reroll(match,round,now,"NO_ASSETS_READY"); return;}
    forfeitUnready(match,round,p => p.assetsReadyAt !== null,now,"ASSET_PREPARATION_TIMEOUT");
    if (!terminal(match)) {round.entryRequestedAt=now; transition(match,"ENTERING",now,"ASSETS_BARRIER_COMPLETE");}
  }
  if (match.state === "ENTERING" && round.entryRequestedAt !== null) {
    if (round.firstEnteredAt === null && now >= round.entryRequestedAt+5_000) {reroll(match,round,now,"NO_NATIVE_ENTRY"); return;}
    if (round.firstEnteredAt !== null && now >= round.firstEnteredAt+10_000) {
      forfeitUnready(match,round,p => p.enteredAt !== null,now,"NATIVE_ENTRY_TIMEOUT");
      if (!terminal(match)) {round.startsAt=now+3_000; round.endsAt=round.startsAt+180_000;
        transition(match,"COUNTDOWN",now,"ENTRY_BARRIER_COMPLETE");}
    }
  }
  if (match.state === "COUNTDOWN" && round.startsAt !== null && now >= round.startsAt) transition(match,"PLAYING",round.startsAt,"COUNTDOWN_COMPLETE");
  if (match.state === "PLAYING" && round.endsAt !== null && now >= round.endsAt) finishRound(match,round,round.endsAt);
}
export function reduceSurvivalMatch(before: SurvivalMatch, command: SurvivalCommand, now: number): SurvivalMatch {
  if (!Number.isFinite(now) || now < before.updatedAt) conflict("Server clock moved backwards");
  const match=structuredClone(before);
  if (terminal(match)) return match;
  const enteredState=match.state;
  advanceTimers(match,now);
  const round=survivalCurrentRound(match);
  if (command.type === "TICK") {
    // Keep ROUND_RESULT durably visible until a subsequent tick; never create a
    // second next-round row from an old completion command.
    if (match.state === "ROUND_RESULT" && enteredState === "ROUND_RESULT") {
      const phase: SurvivalMapPhase = round.phase === "ROUND_1" ? "ROUND_2" : round.phase === "ROUND_2" ? "ROUND_3" :
        round.phase === "ROUND_3" ? "FINAL" : "TIEBREAK";
      addRound(match,phase,round.players.filter(p => p.qualified && !p.forfeited).map(p => p.participantId),now);
    }
  } else if (!terminal(match) && (match.state === enteredState || (enteredState === "COUNTDOWN" && match.state === "PLAYING"))) {
    if (command.roundKey !== round.key || command.mapGeneration !== round.mapGeneration) conflict("SURVIVAL_CONTEXT_CHANGED");
    if (command.type === "MAP_SELECTED") {
      if (match.state !== "MAP_SELECTION") conflict("Map has already been selected");
      if (!survivalMapAllowed(command.map,match.difficultyGroup,round.phase,match.config.minimumLevelId) ||
        !/^[a-f0-9]{64}$/.test(command.map.geometrySha256) || command.map.geometryBytes < 1 ||
        !Number.isSafeInteger(command.map.levelVersion) || command.map.levelVersion < 1 ||
        !Number.isFinite(Date.parse(command.map.verifiedAt)) ||
        match.rounds.some(r => r.selectedMaps.some(m => m.levelId === command.map.levelId))) conflict("Unverified, ineligible or reused Survival map");
      round.map=structuredClone(command.map); round.selectedMaps.push(structuredClone(command.map));
      round.preparationStartedAt=now; transition(match,"PREPARING",now,"VERIFIED_MAP_SELECTED");
    } else if (command.type === "MAP_INVALIDATED") {
      if (!["PREPARING","ENTERING"].includes(match.state)) conflict("Started rounds cannot change map");
      reroll(match,round,now,command.reason);
    } else if (command.type === "FORFEIT") {
      const players=command.participantIds.map(id => round.players.find(p => p.participantId===id));
      if (!players.length || new Set(command.participantIds).size!==players.length ||
        players.some(player => !player || player.forfeited || player.qualified) || match.state==="ROUND_RESULT") conflict("Player cannot forfeit this round");
      if (!command.reason.trim()) conflict("Forfeit responsibility is required");
      for(const player of players) {
        player!.forfeited=true;
        for(const attempt of player!.attempts)if(attempt.endedAt===null)attempt.endedAt=now;
        eliminate(match,round,player!,roundScore(round,player!),command.reason);
      }
      if (!survivorsOrEnd(match,round,now)) {
        const remaining=activePlayers(round);
        if(match.state==="PREPARING" && remaining.every(p => p.assetsReadyAt!==null)) {
          round.entryRequestedAt=now;transition(match,"ENTERING",now,"ASSETS_BARRIER_COMPLETE");
        } else if(match.state==="ENTERING" && remaining.every(p => p.enteredAt!==null)) {
          round.startsAt=now+3_000;round.endsAt=round.startsAt+180_000;
          transition(match,"COUNTDOWN",now,"ENTRY_BARRIER_COMPLETE");
        } else if(match.state==="PLAYING" && round.phase==="TIEBREAK" && remaining.every(p => p.attempts.length===3 && p.attempts.every(a => a.endedAt!==null))) finishRound(match,round,now);
      }
    } else {
      if (!round.map || command.levelId !== round.map.levelId) conflict("SURVIVAL_MAP_CHANGED");
      const player=round.players.find(p => p.participantId === command.participantId);
      if (!player || player.forfeited) conflict("Player is not active in this round");
      switch (command.type) {
        case "ASSETS_READY": {
          if (player.assetsReadyAt !== null) break;
          if (match.state !== "PREPARING") conflict("Assets are not being prepared");
          const {verifiedAt: _receivedAt,...certificate}=command.certificate;
          const {verifiedAt: _verifiedAt,...expected}=round.map;
          if (Object.keys(expected).some(key => certificate[key as keyof typeof certificate] !== expected[key as keyof typeof expected])) conflict("Client map certificate mismatch");
          player.assetsReadyAt=now; player.preparationFailure=null;
          if (activePlayers(round).every(p => p.assetsReadyAt !== null)) {
            round.entryRequestedAt=now; transition(match,"ENTERING",now,"ASSETS_BARRIER_COMPLETE");
          }
          break;
        }
        case "PREPARATION_FAILED":
          if (match.state !== "PREPARING" || player.assetsReadyAt !== null) conflict("Preparation already finished");
          player.preparationFailure=command.reason;
          if (activePlayers(round).every(p => p.preparationFailure !== null)) reroll(match,round,now,"ALL_PREPARATIONS_FAILED");
          break;
        case "ENTERED":
          if (player.enteredAt !== null) break;
          if (match.state !== "ENTERING" || player.assetsReadyAt === null) conflict("Native entry is not invited");
          player.enteredAt=now; round.firstEnteredAt ??= now;
          if (activePlayers(round).every(p => p.enteredAt !== null)) {
            round.startsAt=now+3_000; round.endsAt=round.startsAt+180_000;
            transition(match,"COUNTDOWN",now,"ALL_NATIVE_SCENES_ENTERED");
          }
          break;
        case "ATTEMPT_START":
          if (player.attempts.some(a => a.id === command.attemptId)) break;
          if (match.state !== "PLAYING" || !command.attemptId || command.attemptId.length > 80 ||
            player.attempts.some(a => a.endedAt === null) || (round.phase === "TIEBREAK" && player.attempts.length >= 3)) conflict("Attempt cannot start");
          player.attempts.push({id:command.attemptId,sequence:player.attempts.length+1,startedAt:now,endedAt:null,highestProgress:0});
          break;
        case "PROGRESS": case "ATTEMPT_END": {
          if (match.state !== "PLAYING" || !Number.isFinite(command.progress) || command.progress < 0 || command.progress > 100) conflict("Invalid or late progress");
          const attempt=player.attempts.find(a => a.id === command.attemptId);
          if (!attempt) conflict("Unknown Survival attempt");
          if (attempt.endedAt !== null) break;
          attempt.highestProgress=Math.max(attempt.highestProgress,command.progress);
          player.highestProgress=Math.max(player.highestProgress,attempt.highestProgress);
          if (command.type === "ATTEMPT_END") attempt.endedAt=now;
          if (round.phase === "TIEBREAK" && activePlayers(round).every(p => p.attempts.length === 3 && p.attempts.every(a => a.endedAt !== null))) finishRound(match,round,now);
          break;
        }
      }
    }
  }
  // A timer changing the context makes the incoming packet read-only, so the
  // authoritative timeout commits instead of rolling back behind a 409 error.
  if (JSON.stringify(match) !== JSON.stringify(before)) {match.revision++; match.updatedAt=now;}
  return match;
}

export function survivalSettlementInputs(match: SurvivalMatch) {
  if (match.state !== "FINAL_RESULT") return [];
  return match.players.map(player => {
    const rank=player.finalRank!;
    const tiedPlayers=match.players.filter(p => p.finalRank === rank).length;
    return {participantId:player.participantId,playerId:player.playerId,rank,tiedPlayers,
      occupiedLastRank:rank+tiedPlayers-1,delta:survivalPlacementLp(rank,tiedPlayers,match.config.placementLp),
      eventKey:`survival:${match.id}:result:${player.participantId}`,configGeneration:match.configGeneration};
  }).sort((a,b) => a.rank-b.rank || a.participantId.localeCompare(b.participantId));
}
export function survivalLiveView(match: SurvivalMatch, now: number, participantId: string | null = null) {
  const round=survivalCurrentRound(match);
  const ranked=survivalRankScores(activePlayers(round).map(p => ({participantId:p.participantId,
    score:roundScore(round,p),highestProgress:p.highestProgress,
    attemptsUsed:p.attempts.length,attemptsFinished:p.attempts.filter(a => a.endedAt !== null).length,
    waitingForOthers:round.phase === "TIEBREAK" && p.attempts.length === 3 && p.attempts.every(a => a.endedAt !== null)})));
  const cutoff=ranked[Math.min(round.target,ranked.length)-1]?.score ?? null;
  const standings=ranked.map(p => ({...p,score:round.phase === "TIEBREAK" ? p.score/1000 : p.score,
    passing:cutoff !== null && p.score >= cutoff}));
  return {matchId:match.id,revision:match.revision,state:match.state,phase:round.phase,roundKey:round.key,
    mapGeneration:round.mapGeneration,map:round.map,averageLp:match.averageLp,difficultyGroup:match.difficultyGroup,
    configGeneration:match.configGeneration,serverNow:new Date(now).toISOString(),
    preparationWarningAt:round.preparationStartedAt === null ? null : new Date(round.preparationStartedAt+10_000).toISOString(),
    preparationDeadlineAt:round.preparationStartedAt === null ? null : new Date(round.preparationStartedAt+15_000).toISOString(),
    entryRequestedAt:round.entryRequestedAt === null ? null : new Date(round.entryRequestedAt).toISOString(),
    entryDeadlineAt:round.entryRequestedAt === null ? null : new Date(round.firstEnteredAt === null ? round.entryRequestedAt+5_000 : round.firstEnteredAt+10_000).toISOString(),
    startsAt:round.startsAt === null ? null : new Date(round.startsAt).toISOString(),
    endsAt:round.endsAt === null ? null : new Date(round.endsAt).toISOString(),
    remainingSeconds:match.state === "PLAYING" && round.endsAt !== null ? Math.max(0,(round.endsAt-now)/1000) : 0,
    standings,top3:standings.slice(0,3),myRank:standings.find(p => p.participantId === participantId)?.rank ?? null,
    cutoffScore:round.phase === "TIEBREAK" && cutoff !== null ? cutoff/1000 : cutoff,
    targetQualifiers:Math.min(round.target,standings.length),finalFirstScore:["FINAL","TIEBREAK"].includes(round.phase) ? standings[0]?.score ?? null : null,
    preparation:round.players.map(p => ({participantId:p.participantId,assetsReady:p.assetsReadyAt !== null,
      entered:p.enteredAt !== null,failed:p.preparationFailure,forfeited:p.forfeited})),
    participants:match.players.map(p => ({participantId:p.participantId,name:p.name,kind:p.kind,lpSnapshot:p.lp,
      eliminationStage:p.eliminationStage,finalRank:p.finalRank,forfeitReason:p.forfeitReason})),
    winnerId:match.winnerId,cancellationReason:match.cancellationReason,settlementInputs:survivalSettlementInputs(match)};
}
