import { observeSurvivalAchievementCheckpoint, observeSurvivalAchievementRecovery, type SurvivalAchievementCheckpoint } from "./survival-achievement-checkpoint.js";
import { survivalOnlineTargets,survivalOnlineRewardRank } from "./survival-online.js";
import { bestPercentOfThree } from "./deathmatch.js";
import { RankedDomainError } from "./errors.js";
import { survivalPlacementLp, survivalTierForLp, type SurvivalConfig } from "./survival.js";
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
  /** Real human tier at admission; bots derive their icon from the LP snapshot. */
  displayedTier?: string;
  gdAccountId?: string;
}
export interface SurvivalMatchPlayer extends SurvivalEntrant {
  eliminationStage: number | null;
  eliminatedAt?: number;
  eliminationScore: number;
  forfeitReason: string | null;
  finalRank: number | null;
  fixedRewardRank?:number;
}
export interface SurvivalAttempt {
  id: string;
  sequence: number;
  startedAt: number;
  endedAt: number | null;
  highestProgress: number;
  currentProgress?: number;
}
export interface SurvivalRoundPlayer {
  participantId: string;
  assetsReadyAt: number | null;
  preparationFailure: string | null;
  /** Connected download/entry delay; distinct from transport absence. */
  entryDeferred?:boolean;
  enteredAt: number | null;
  highestProgress: number;
  attempts: SurvivalAttempt[];
  rank: number | null;
  qualified: boolean;
  forfeited: boolean;
  /** Absent online participants keep 0%, may return, and do not block native readiness. */
  absent?:boolean;
}
export interface SurvivalRound {
  key: string;
  ordinal: number;
  phase: SurvivalMapPhase;
  target: number;
  mapGeneration: number;
  preparationRerolls?:number;
  map: SurvivalVerifiedMap | null;
  selectedMaps: SurvivalVerifiedMap[];
  preparationStartedAt: number | null;
  entryRequestedAt: number | null;
  firstEnteredAt: number | null;
  startsAt: number | null;
  endsAt: number | null;
  finishedAt: number | null;
  cutoffScore: number | null;
  achievementCheckpoint?: SurvivalAchievementCheckpoint;
  players: SurvivalRoundPlayer[];
}
export interface SurvivalMatch {
  id: string;
  online?:boolean;
  variableSize?:boolean;
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
  winnerIds?: string[];
  cancellationReason: string | null;
  completionKind?: "DEBUG_FORCE" | "DEBUG_HUMAN_ELIMINATION" | "BOTS_ONLY_RESULT";
  /** Debug adapter's durable presentation barrier; ordinary M3 matches remain automatic. */
  presentationControlled?: boolean;
  presentationGate?: "INTRO" | "ROUND_RESULT" | null;
  players: SurvivalMatchPlayer[];
  rounds: SurvivalRound[];
  nextMap?: {roundKey:string;phase:SurvivalMapPhase;mapGeneration:number;map:SurvivalVerifiedMap};
  transitions: { sequence: number; state: SurvivalMatchState; roundKey: string; at: number; reason: string }[];
}
export interface SurvivalRoundContext { roundKey: string; mapGeneration: number; levelId: number; }

/** All ready clients stay in the selection room before native entry opens. */
export const SURVIVAL_READY_HOLD_MS = 5_000;
export const SURVIVAL_INTRO_MS = 6_000;
export const SURVIVAL_RESULT_MS = 5_000;
export const survivalNextPhase=(phase:SurvivalMapPhase,capacity=16):SurvivalMapPhase=>phase==="ROUND_1"?"ROUND_2":phase==="ROUND_2"?"ROUND_3":phase==="ROUND_3"?(capacity===64?"ROUND_4":"FINAL"):phase==="ROUND_4"?"FINAL":"TIEBREAK";
export function survivalEntryOpensAt(round: SurvivalRound): number | null {
  return round.entryRequestedAt === null ? null : round.entryRequestedAt + SURVIVAL_READY_HOLD_MS;
}
export function survivalEntryDeadlineAt(round: SurvivalRound): number | null {
  const opens = survivalEntryOpensAt(round);
  return opens === null ? null : round.firstEnteredAt === null ? opens + 5_000 : round.firstEnteredAt + 10_000;
}
export type SurvivalCommand =
  | { type: "TICK" }
  | {type:"ABSENT";roundKey:string;mapGeneration:number;participantIds:readonly string[]}
  | { type:"PRESENTATION_COMPLETE";roundKey:string;mapGeneration:number;participantId:string }
  | { type: "NEXT_MAP_SELECTED"; roundKey:string;mapGeneration:number;map:SurvivalVerifiedMap }
  | { type: "MAP_SELECTED"; roundKey: string; mapGeneration: number; map: SurvivalVerifiedMap }
  | ({ type: "ASSETS_READY"; participantId: string; certificate: SurvivalVerifiedMap } & SurvivalRoundContext)
  | ({ type: "PREPARATION_FAILED"; participantId: string; reason: string; audioFailure?:{category:string;resourceKind:"SONG"|"SFX";resourceId:number;nativeCode:number} } & SurvivalRoundContext)
  | ({ type: "ENTERED"; participantId: string } & SurvivalRoundContext)
  | ({ type: "ATTEMPT_START"; participantId: string; attemptId: string } & SurvivalRoundContext)
  | ({ type: "PROGRESS" | "ATTEMPT_END"; participantId: string; attemptId: string; progress: number; currentProgress?: number } & SurvivalRoundContext)
  // Dedicated, authenticated debug route only; never accepted by the public event DTO.
  | ({ type: "DEBUG_FORCE_CLEAR"; participantId: string; attemptId: string } & SurvivalRoundContext)
  | ({ type: "DEBUG_END_ROUND"; participantId: string } & SurvivalRoundContext)
  // This command is for the verified server provider, never an untrusted client report.
  | { type:"MAP_SUPPLY_FAILED";roundKey:string;mapGeneration:number }
  | { type: "MAP_INVALIDATED"; roundKey: string; mapGeneration: number; reason: string }
  // Future session adapters establish responsibility; the engine only records
  // their trusted decision. No public forfeit route is exposed in M3.
  | { type: "FORFEIT"; roundKey: string; mapGeneration: number; participantIds: readonly string[]; reason: string };

function conflict(message: string): never { throw new RankedDomainError("INVALID_MATCH_TRANSITION", message); }
export const survivalCurrentRound = (match: SurvivalMatch): SurvivalRound => match.rounds[match.rounds.length - 1]!;
const terminal = (match: SurvivalMatch): boolean => match.state === "FINAL_RESULT" || match.state === "CANCELLED";
const activePlayers = (round: SurvivalRound) => round.players.filter(player => !player.forfeited);
const entryPlayers = (round: SurvivalRound) => activePlayers(round).filter(player=>!player.absent&&!player.entryDeferred);

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
  if (round.phase !== "TIEBREAK") return Math.trunc(player.highestProgress);
  const inputs = Array.from({length:3}, (_,i) => ({ progressPercent:
    survivalMilliPercent(player.attempts[i]?.highestProgress ?? 0) / 1000, cleared:false }));
  return survivalMilliPercent(bestPercentOfThree(inputs));
}
function startAttempt(match: SurvivalMatch, round: SurvivalRound, player: SurvivalRoundPlayer, id: string, now: number): void {
  if (player.attempts.some(a => a.id === id)) return;
  if (match.state !== "PLAYING" || (match.online && (player.absent || player.entryDeferred || player.assetsReadyAt === null || player.enteredAt === null)) ||
    !id || id.length > 80 || player.attempts.some(a => a.endedAt === null) ||
    (round.phase === "TIEBREAK" && player.attempts.length >= 3)) conflict("Attempt cannot start");
  player.attempts.push({id,sequence:player.attempts.length+1,startedAt:now,endedAt:null,highestProgress:0});
}
function recordAttempt(match: SurvivalMatch, round: SurvivalRound, player: SurvivalRoundPlayer,
  id: string, progress: number, ended: boolean, now: number, currentProgress=progress): void {
  if (match.state !== "PLAYING" || !Number.isFinite(progress) || progress < 0 || progress > 100 || !Number.isFinite(currentProgress) || currentProgress<0 || currentProgress>100) conflict("Invalid or late progress");
  const attempt = player.attempts.find(a => a.id === id);
  if (!attempt) conflict("Unknown Survival attempt");
  if (attempt.endedAt !== null) return;
  attempt.currentProgress = currentProgress;
  attempt.highestProgress = Math.max(attempt.highestProgress, progress);
  player.highestProgress = Math.max(player.highestProgress, attempt.highestProgress);
  if (ended) attempt.endedAt = now;
  if (round.phase === "TIEBREAK" && activePlayers(round).every(p => p.attempts.length === 3 && p.attempts.every(a => a.endedAt !== null)))
    finishRound(match, round, now);
}
function assertSelectedMap(match:SurvivalMatch,phase:SurvivalMapPhase,map:SurvivalVerifiedMap):void {
  if(!survivalMapAllowed(map,match.difficultyGroup,phase,match.config.minimumLevelId) ||
    !/^[a-f0-9]{64}$/.test(map.geometrySha256) || map.geometryBytes<1 ||
    !Number.isSafeInteger(map.levelVersion) || map.levelVersion<1 || !Number.isFinite(Date.parse(map.verifiedAt)) ||
    match.rounds.some(r=>r.selectedMaps.some(m=>m.levelId===map.levelId)))conflict("Unverified, ineligible or reused Survival map");
}
/** Only the server provider uses this target. Visual candidates never enter it. */
export function survivalMapSelectionTarget(match:SurvivalMatch){
  const round=survivalCurrentRound(match);
  if(match.state==="MAP_SELECTION" && !round.map)return {roundKey:round.key,mapGeneration:round.mapGeneration,phase:round.phase,prefetch:false};
  if(match.state==="ROUND_RESULT" && !match.nextMap && round.finishedAt!==null && round.players.filter(p=>p.qualified&&!p.forfeited).length>1)
    return {roundKey:`stage-${round.ordinal+1}`,mapGeneration:1,phase:survivalNextPhase(round.phase,match.config.matchPlayerCount),prefetch:true};
  return null;
}
function transition(match: SurvivalMatch, state: SurvivalMatchState, at: number, reason: string): void {
  match.state = state;
  match.transitions.push({sequence:match.transitions.length + 1, state,
    roundKey:survivalCurrentRound(match).key, at, reason});
}
function addRound(match: SurvivalMatch, phase: SurvivalMapPhase, participants: readonly string[], at: number): void {
  const ordinal = match.rounds.length + 1;
  const targets=(match.online || match.variableSize)?survivalOnlineTargets(match.players.length,match.config.matchPlayerCount):[match.config.round1Qualifiers,match.config.round2Qualifiers,match.config.round3Qualifiers,match.config.round4Qualifiers??0];
  const target=phase==="ROUND_1"?targets[0]!:phase==="ROUND_2"?targets[1]!:phase==="ROUND_3"?targets[2]!:phase==="ROUND_4"?targets[3]!:1;
  match.rounds.push({ key: `stage-${ordinal}`, ordinal, phase, target, mapGeneration:1, map:null, selectedMaps:[],
    preparationStartedAt:null, entryRequestedAt:null, firstEnteredAt:null, startsAt:null, endsAt:null, finishedAt:null,
    cutoffScore:null, players:participants.map(participantId => ({participantId,assetsReadyAt:null,
      preparationFailure:null,enteredAt:null,highestProgress:0,attempts:[],rank:null,qualified:false,forfeited:false})) });
  transition(match,"MAP_SELECTION",at,"NEW_ROUND");
  const selected=match.nextMap,current=survivalCurrentRound(match);
  if(selected && selected.roundKey===current.key && selected.phase===phase && selected.mapGeneration===current.mapGeneration){
    current.map=structuredClone(selected.map);current.selectedMaps.push(structuredClone(selected.map));
    current.preparationStartedAt=at;delete match.nextMap;transition(match,"PREPARING",at,"PREFETCHED_MAP_ADOPTED");
  }
}
export function createSurvivalMatch(id: string, entrants: readonly SurvivalEntrant[], config: SurvivalConfig,
  configGeneration: string, now: number,online=false,variableSize=false): SurvivalMatch {
  const errors = validateSurvivalEngineConfig(config);
  if (errors.length) throw new RankedDomainError("INVALID_CONFIG",errors.join("; "));
  if ((!online && !variableSize && entrants.length!==config.matchPlayerCount) || ((online || variableSize) && (entrants.length<2||entrants.length>config.matchPlayerCount)) || new Set(entrants.map(p => p.participantId)).size !== entrants.length ||
    entrants.some(p => !p.participantId || p.participantId.length > 80 || !Number.isSafeInteger(p.lp) || p.lp < 0 ||
      !["HUMAN","BOT"].includes(p.kind) || (p.kind === "HUMAN" && !p.playerId)) ||
    new Set(entrants.filter(p => p.playerId).map(p => p.playerId)).size !== entrants.filter(p => p.playerId).length ||
    !configGeneration.trim() || !Number.isFinite(now)) conflict("Survival requires distinct valid entrants within its captured field size");
  const averageLp = entrants.reduce((sum,p) => sum + p.lp,0) / entrants.length;
  const match: SurvivalMatch = { id,revision:1,state:"MAP_SELECTION",configGeneration,config:structuredClone(config),averageLp,
    difficultyGroup:survivalDifficultyForAverage(averageLp,config.tierBands),createdAt:now,updatedAt:now,finishedAt:null,
    winnerId:null,cancellationReason:null,players:entrants.map(p => ({...p,eliminationStage:null,eliminationScore:0,
      forfeitReason:null,finalRank:null})),rounds:[],transitions:[] };
  if(online)match.online=true;
  if(variableSize)match.variableSize=true;
  addRound(match,(online || variableSize) && entrants.length<=3?"FINAL":(online || variableSize) && entrants.length<8?(config.matchPlayerCount===64?"ROUND_4":"ROUND_3"):"ROUND_1",entrants.map(p => p.participantId),now);
  return match;
}
function eliminate(match: SurvivalMatch, round: SurvivalRound, player: SurvivalRoundPlayer, score: number, reason: string | null, at:number): void {
  const entrant = match.players.find(p => p.participantId === player.participantId)!;
  if (entrant.eliminationStage !== null) return;
  entrant.eliminationStage = round.ordinal;
  entrant.eliminatedAt=at;
  entrant.eliminationScore = round.phase === "TIEBREAK" ? score / 1000 : score;
  entrant.forfeitReason = reason;
  player.qualified = false;
}
/** Trusted presence adapter only: absence is not native entry/asset evidence. */
export function survivalMarkAbsent(before:SurvivalMatch,ids:readonly string[],now:number):SurvivalMatch {
  if(!before.online || !["PREPARING","ENTERING"].includes(before.state))return before;
  const match=structuredClone(before),round=survivalCurrentRound(match);let changed=false;
  for(const id of ids){const p=round.players.find(p=>p.participantId===id);if(p&&!p.forfeited&&!p.absent){p.absent=true;changed=true;}}
  if(!Number.isFinite(now)||now<before.updatedAt)conflict("Invalid presence clock");
  if(!changed)return before;match.revision++;return match;
}
/** Immediate surrender has the fixed floor of its round. Other elimination
 * ranks become immutable once the round closes, regardless of later winners. */
export function survivalStabilizeOnlineRanks(match:SurvivalMatch):void {
  if(!match.online && !match.presentationControlled)return;
  const alive=match.players.filter(p=>p.eliminationStage===null).length;
  const ordered=match.players.filter(p=>p.eliminationStage!==null).sort((a,b)=>
    b.eliminationStage!-a.eliminationStage! || Number(!!a.fixedRewardRank)-Number(!!b.fixedRewardRank) ||
    b.eliminationScore-a.eliminationScore || a.participantId.localeCompare(b.participantId));
  let rank=alive;
  ordered.forEach((p,i)=>{
    const previous=ordered[i-1];
    if(!i || p.eliminationStage!==previous!.eliminationStage || p.eliminationScore!==previous!.eliminationScore || !!p.fixedRewardRank!==!!previous!.fixedRewardRank)rank=alive+i+1;
    const round=match.rounds.find(r=>r.ordinal===p.eliminationStage)!;
    if(p.fixedRewardRank)p.finalRank=p.fixedRewardRank;
    else if(round.finishedAt!==null || match.state==="FINAL_RESULT")p.finalRank=rank;
  });
}
function finishMatch(match: SurvivalMatch, winnerId: string | null, at: number, reason: string): void {
  match.winnerId = winnerId; match.finishedAt = at;
  if(match.presentationControlled)match.presentationGate=null;
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
  if(match.online){survivalStabilizeOnlineRanks(match);match.players.find(p=>p.participantId===winnerId)!.finalRank=1;}
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
  if(match.online){
    // The shared deadline still opens entry for ready players. A download is
    // not a surrender: delayed clients keep zero until certified late entry.
    for(const player of activePlayers(round).filter(p=>!ready(p)))player.entryDeferred=true;
    return;
  }
  for (const player of entryPlayers(round).filter(p => !ready(p))) {
    player.forfeited=true; eliminate(match,round,player,roundScore(round,player),reason,at);
  }
  survivorsOrEnd(match,round,at);
}
function reroll(match: SurvivalMatch, round: SurvivalRound, at: number, reason: string): void {
  round.preparationRerolls=(round.preparationRerolls??0)+1;
  if(round.preparationRerolls>3){finishMatch(match,null,at,"MAP_SUPPLY_UNAVAILABLE");return;}
  round.mapGeneration++; round.map=null; round.preparationStartedAt=null; round.entryRequestedAt=null;
  round.firstEnteredAt=null; round.startsAt=null; round.endsAt=null;
  for (const player of round.players) {player.assetsReadyAt=null; player.preparationFailure=null; player.enteredAt=null;player.entryDeferred=false;}
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
    if (!player.qualified) eliminate(match,round,player,item.score,null,at);
  }
  const qualified=round.players.filter(p => p.qualified && !p.forfeited);
  if (qualified.length === 1) { finishMatch(match,qualified[0]!.participantId,at,"SINGLE_WINNER"); return; }
  if (!qualified.length) { finishMatch(match,null,at,"NO_SURVIVORS"); return; }
  survivalStabilizeOnlineRanks(match);
  transition(match,"ROUND_RESULT",at,"CUTOFF_TIES_ADVANCE");
  if(match.presentationControlled)match.presentationGate="ROUND_RESULT";
}
/** Timer boundaries are evaluated before accepting any command, including late
 * packets and duplicate reports. >= deadline is closed; preparation is not play time. */
function advanceTimers(match: SurvivalMatch, now: number): void {
  const round=survivalCurrentRound(match);
  if(match.presentationGate==="INTRO" && now>=match.createdAt+SURVIVAL_INTRO_MS)match.presentationGate=null;
  if(match.presentationGate==="ROUND_RESULT" && round.finishedAt!==null && now>=round.finishedAt+SURVIVAL_RESULT_MS)match.presentationGate=null;
  if(match.state==="MAP_SELECTION" && round.map && match.presentationGate!=="INTRO"){
    round.preparationStartedAt=now;transition(match,"PREPARING",now,"PREFETCHED_INTRO_MAP_READY");
  }
  if(match.online && match.state==="PREPARING" && entryPlayers(round).length>0 && entryPlayers(round).every(p=>p.assetsReadyAt!==null)){
    round.entryRequestedAt=now;transition(match,"ENTERING",now,"PRESENT_ASSETS_BARRIER_COMPLETE");
  }
  if(match.online && match.state==="ENTERING" && entryPlayers(round).length>0 && entryPlayers(round).every(p=>p.enteredAt!==null)){
    round.startsAt=now+3000;round.endsAt=round.startsAt+180000;transition(match,"COUNTDOWN",now,"PRESENT_ENTRY_BARRIER_COMPLETE");
  }
  if (match.state === "PREPARING" && round.preparationStartedAt !== null && now >= round.preparationStartedAt + 15_000) {
    const ready=(match.online?activePlayers(round):entryPlayers(round)).filter(p => p.assetsReadyAt !== null);
    if (!ready.length && entryPlayers(round).length) {
      // A fixed server-owned grace applies only when nobody can start yet.
      // One slow client never extends the ready participants' fifteen seconds.
      if(match.online && now<round.preparationStartedAt+60_000 && !activePlayers(round).every(p=>p.preparationFailure!==null))return;
      reroll(match,round,now,"NO_ASSETS_READY"); return;
    }
    forfeitUnready(match,round,p => p.assetsReadyAt !== null,now,"ASSET_PREPARATION_TIMEOUT");
    if (!terminal(match)) {round.entryRequestedAt=now; transition(match,"ENTERING",now,"ASSETS_BARRIER_COMPLETE");}
  }
  if(match.online && match.state==="ENTERING" && entryPlayers(round).length===0){
    round.startsAt=now+3000;round.endsAt=round.startsAt+180000;transition(match,"COUNTDOWN",now,"ABSENT_PLAYERS_SCORE_ZERO");
  }
  if (match.state === "ENTERING" && round.entryRequestedAt !== null) {
    if (round.firstEnteredAt === null && now >= survivalEntryDeadlineAt(round)!) {reroll(match,round,now,"NO_NATIVE_ENTRY"); return;}
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
  const match=structuredClone(observeSurvivalAchievementCheckpoint(before,now));
  if (terminal(match)) return match;
  const enteredState=match.state;
  advanceTimers(match,now);
  const round=survivalCurrentRound(match);
  if (command.type === "TICK") {
    // Keep ROUND_RESULT durably visible until a subsequent tick; never create a
    // second next-round row from an old completion command.
    if (match.state === "ROUND_RESULT" && enteredState === "ROUND_RESULT" && match.presentationGate!=="ROUND_RESULT") {
      const phase=survivalNextPhase(round.phase,match.config.matchPlayerCount);
      addRound(match,phase,round.players.filter(p => p.qualified && !p.forfeited).map(p => p.participantId),now);
    }
  } else if (!terminal(match) && (match.state === enteredState || (enteredState === "COUNTDOWN" && match.state === "PLAYING"))) {
    if (command.roundKey !== round.key || command.mapGeneration !== round.mapGeneration) conflict("SURVIVAL_CONTEXT_CHANGED");
    if(command.type==="NEXT_MAP_SELECTED") {
      const phase=survivalNextPhase(round.phase,match.config.matchPlayerCount);
      if(match.state!=="ROUND_RESULT" || round.finishedAt===null || match.nextMap || round.players.filter(p=>p.qualified&&!p.forfeited).length<2)
        conflict("No confirmed next Survival round");
      assertSelectedMap(match,phase,command.map);
      match.nextMap={roundKey:`stage-${round.ordinal+1}`,phase,mapGeneration:1,map:structuredClone(command.map)};
    } else if(command.type==="ABSENT") {
      if(!match.online || !["PREPARING","ENTERING"].includes(match.state))conflict("Invalid absence phase");
      for(const id of command.participantIds){const p=round.players.find(p=>p.participantId===id);if(!p||p.forfeited)conflict("Invalid absent participant");p.absent=true;}
    } else if(command.type==="PRESENTATION_COMPLETE") {
      if(!match.presentationControlled || !match.players.some(p=>p.participantId===command.participantId && p.kind==="HUMAN" && p.eliminationStage===null))
        conflict("Invalid presentation owner");
      if(match.presentationGate==="INTRO" && match.state==="MAP_SELECTION" && now>=match.createdAt+SURVIVAL_INTRO_MS)match.presentationGate=null;
      else if(match.presentationGate==="ROUND_RESULT" && match.state==="ROUND_RESULT" && round.finishedAt!==null && now>=round.finishedAt+SURVIVAL_RESULT_MS)
        match.presentationGate=null;
      else if(match.presentationGate!==null)conflict("Presentation barrier is not complete");
    } else if (command.type === "MAP_SELECTED") {
      if (match.state !== "MAP_SELECTION" || round.map) conflict("Map has already been selected");
      assertSelectedMap(match,round.phase,command.map);
      round.map=structuredClone(command.map); round.selectedMaps.push(structuredClone(command.map));
      if(match.presentationGate!=="INTRO"){round.preparationStartedAt=now; transition(match,"PREPARING",now,"VERIFIED_MAP_SELECTED");}
    } else if(command.type==="MAP_SUPPLY_FAILED"){
      if(match.state!=="MAP_SELECTION")conflict("Map supply cancellation is pre-play only");
      finishMatch(match,null,now,"MAP_SUPPLY_UNAVAILABLE");
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
        eliminate(match,round,player!,roundScore(round,player!),command.reason,now);
        if((match.online && command.reason==="ONLINE_SURRENDER") || (match.presentationControlled && command.reason==="DEBUG_HUMAN_SURRENDER")){
          const entrant=match.players.find(p=>p.participantId===player!.participantId)!;
          entrant.fixedRewardRank=round.players.length;entrant.finalRank=round.players.length;
        }
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
          const late=match.online && (player.absent||player.entryDeferred) && ["ENTERING","COUNTDOWN","PLAYING"].includes(match.state);
          if (match.state !== "PREPARING" && !late) conflict("Assets are not being prepared");
          const {verifiedAt: _receivedAt,...certificate}=command.certificate;
          const {verifiedAt: _verifiedAt,...expected}=round.map;
          if (Object.keys(expected).some(key => JSON.stringify(certificate[key as keyof typeof certificate]) !== JSON.stringify(expected[key as keyof typeof expected]))) conflict("Client map certificate mismatch");
          player.assetsReadyAt=now; player.preparationFailure=null;
          if (match.state==="PREPARING" && entryPlayers(round).every(p => p.assetsReadyAt !== null)) {
            round.entryRequestedAt=now; transition(match,"ENTERING",now,"ASSETS_BARRIER_COMPLETE");
          }
          break;
        }
        case "PREPARATION_FAILED":
          if (!["PREPARING","ENTERING"].includes(match.state) || player.assetsReadyAt !== null) conflict("Preparation already finished");
          player.preparationFailure=command.reason;
          if (activePlayers(round).every(p => p.preparationFailure !== null)) reroll(match,round,now,"ALL_PREPARATIONS_FAILED");
          break;
        case "ENTERED":
          if (player.enteredAt !== null) break;
          if ((match.state !== "ENTERING" && !(match.online && (player.absent||player.entryDeferred) && ["COUNTDOWN","PLAYING"].includes(match.state))) || player.assetsReadyAt === null) conflict("Native entry is not invited");
          if (match.state === "ENTERING" && (survivalEntryOpensAt(round) === null || now < survivalEntryOpensAt(round)!)) conflict("Native entry is waiting for the ready countdown");
          player.enteredAt=now;player.absent=false;player.entryDeferred=false; round.firstEnteredAt ??= now;
          if (match.state==="ENTERING" && entryPlayers(round).every(p => p.enteredAt !== null)) {
            round.startsAt=now+3_000; round.endsAt=round.startsAt+180_000;
            transition(match,"COUNTDOWN",now,"ALL_NATIVE_SCENES_ENTERED");
          }
          break;
        case "ATTEMPT_START":
          startAttempt(match,round,player,command.attemptId,now);
          break;
        case "PROGRESS": case "ATTEMPT_END": {
          recordAttempt(match,round,player,command.attemptId,command.progress,command.type==="ATTEMPT_END",now,command.currentProgress);
          break;
        }
        case "DEBUG_END_ROUND": {
          if(match.online || !match.presentationControlled || match.state!=="PLAYING" ||
            match.players.find(p=>p.participantId===player.participantId)?.kind!=="HUMAN" ||
            player.assetsReadyAt===null || player.enteredAt===null)conflict("Debug round end is not allowed");
          finishRound(match,round,now);break;
        }
        case "DEBUG_FORCE_CLEAR": {
          if (match.online || !match.presentationControlled || round.phase === "TIEBREAK" || match.state !== "PLAYING" ||
            match.players.find(p=>p.participantId===player.participantId)?.kind !== "HUMAN" ||
            player.assetsReadyAt === null || player.enteredAt === null) conflict("Debug Force Clear is not allowed in this round");
          const open = player.attempts.find(a=>a.endedAt===null);
          if (!open && player.attempts.some(a=>a.endedAt!==null && a.highestProgress===100)) conflict("Round is already cleared");
          if (!open) startAttempt(match,round,player,command.attemptId,now);
          recordAttempt(match,round,player,open?.id??command.attemptId,100,true,now);
          break;
        }
      }
    }
  }
  observeSurvivalAchievementRecovery(match,now);
  // A timer changing the context makes the incoming packet read-only, so the
  // authoritative timeout commits instead of rolling back behind a 409 error.
  if (JSON.stringify(match) !== JSON.stringify(before)) {match.revision++; match.updatedAt=now;}
  return match;
}

export interface SurvivalBotFrame {participantId:string;attemptId:string;kind:"START"|"PROGRESS"|"END";at:number;progress:number;}
/** Trusted server simulation port, deliberately absent from HTTP event DTOs.
 * Clone once for the whole field and use the same attempt/scoring functions as
 * humans. No simulator may edit another entrant or bypass the round context. */
export function applySurvivalBotFrames(before:SurvivalMatch,context:SurvivalRoundContext,frames:readonly SurvivalBotFrame[],sampledAt:number):SurvivalMatch {
  const round=survivalCurrentRound(before);
  if(before.state!=="PLAYING"||context.roundKey!==round.key||context.mapGeneration!==round.mapGeneration||context.levelId!==round.map?.levelId)return before;
  const match=structuredClone(observeSurvivalAchievementCheckpoint(before,sampledAt)),current=survivalCurrentRound(match);
  for(const f of frames){
    if(match.state!=="PLAYING")break;
    const player=current.players.find(p=>p.participantId===f.participantId);
    if(!player||player.forfeited||!match.players.some(p=>p.participantId===f.participantId&&p.kind==="BOT")||
      f.at<current.startsAt!||f.at>=current.endsAt!||f.at>sampledAt||!Number.isFinite(f.at))conflict("Invalid bot simulation frame");
    if(f.kind==="START")startAttempt(match,current,player,f.attemptId,f.at);
    else recordAttempt(match,current,player,f.attemptId,f.progress,f.kind==="END",f.at,f.progress);
  }
  observeSurvivalAchievementRecovery(match,sampledAt);
  if(JSON.stringify(match)===JSON.stringify(before))return before;
  match.revision++;match.updatedAt=Math.max(before.updatedAt,sampledAt);return match;
}

export function survivalSettlementInputs(match: SurvivalMatch) {
  if (!match.online && !match.presentationControlled && match.state !== "FINAL_RESULT") return [];
  return match.players.filter(p=>p.finalRank!==null && (match.state!=="CANCELLED" || p.fixedRewardRank!==undefined)).map(player => {
    const rank=player.finalRank!;
    const tiedPlayers=match.players.filter(p => p.finalRank === rank).length;
    const last=player.fixedRewardRank??Math.min(match.players.length,rank+tiedPlayers-1);
    const rewardRank=(match.online || match.variableSize || match.config.matchPlayerCount===64)?survivalOnlineRewardRank(last,match.players.length):last;
    return {participantId:player.participantId,playerId:player.playerId,rank,tiedPlayers,
      occupiedLastRank:last,delta:survivalPlacementLp(rewardRank,1,match.config.placementLp),
      eventKey:`survival:${match.id}:result:${player.participantId}`,configGeneration:match.configGeneration};
  }).sort((a,b) => a.rank-b.rank || a.participantId.localeCompare(b.participantId));
}
export function survivalLiveView(match: SurvivalMatch, now: number, participantId: string | null = null) {
  const round=survivalCurrentRound(match);
  const completed=match.rounds.slice().reverse().find(r=>r.finishedAt!==null);
  // Survives the next tick/round, so a client missing the brief ROUND_RESULT
  // state can still present the authoritative eliminations exactly once.
  const completedRound=completed?{key:completed.key,ordinal:completed.ordinal,phase:completed.phase,
    mapGeneration:completed.mapGeneration,finishedAt:new Date(completed.finishedAt!).toISOString(),
    players:completed.players.map(p=>{
      const entrant=match.players.find(v=>v.participantId===p.participantId)!;
      return {participantId:p.participantId,name:entrant.name,kind:entrant.kind,gdAccountId:entrant.gdAccountId??null,
        displayedTier:match.config.tierBands.find(b=>b.tier===entrant.displayedTier)?.group??entrant.displayedTier??survivalTierForLp(entrant.lp,match.config.tierBands).group,rank:p.rank??entrant.finalRank,
        score:completed.phase==="TIEBREAK"?roundScore(completed,p)/1000:roundScore(completed,p),
        qualified:p.qualified&&!p.forfeited,forfeited:p.forfeited};
    })}:null;
  const ranked=survivalRankScores(activePlayers(round).map(p => ({participantId:p.participantId,
    score:roundScore(round,p),highestProgress:p.highestProgress,
    attemptsUsed:p.attempts.length,attemptsFinished:p.attempts.filter(a => a.endedAt !== null).length,
    waitingForOthers:round.phase === "TIEBREAK" && p.attempts.length === 3 && p.attempts.every(a => a.endedAt !== null)})));
  const cutoff=ranked[Math.min(round.target,ranked.length)-1]?.score ?? null;
  const standings=ranked.map(p => {
    const player=round.players.find(v=>v.participantId===p.participantId)!;
    const active=player.attempts.find(a=>a.endedAt===null);
    const score=round.phase === "TIEBREAK" ? p.score/1000 : p.score;
    return {...p,score,bestProgress:round.phase==="TIEBREAK"?score:player.highestProgress,
      currentProgress:active?survivalMilliPercent(active.currentProgress??active.highestProgress)/1000:0,
      attemptId:active?.id??null,
      attemptActive:active!==undefined,
      cleared:player.attempts.some(a=>a.endedAt!==null && a.highestProgress===100),
      eliminated:match.players.find(v=>v.participantId===p.participantId)!.eliminationStage!==null,
      qualified:player.qualified,passing:cutoff !== null && p.score >= cutoff};
  });
  // Current-round forfeits remain visible on the dashboard. They do not become
  // HUD standing rows or influence qualification, cutoff or ranking calculations.
  const roundPlayers=round.players.map(p=>{
    const standing=standings.find(v=>v.participantId===p.participantId);
    if(standing)return standing;
    const score=round.phase==="TIEBREAK"?roundScore(round,p)/1000:roundScore(round,p);
    return {participantId:p.participantId,rank:p.rank??match.players.find(v=>v.participantId===p.participantId)!.finalRank??round.players.length,
      score,highestProgress:p.highestProgress,bestProgress:score,currentProgress:0,attemptActive:false,
      cleared:p.attempts.some(a=>a.endedAt!==null&&a.highestProgress===100),eliminated:true,qualified:false,
      attemptsUsed:p.attempts.length,attemptsFinished:p.attempts.filter(a=>a.endedAt!==null).length,
      waitingForOthers:false,passing:false};
  }).sort((a,b)=>a.rank-b.rank||a.participantId.localeCompare(b.participantId));
  return {matchId:match.id,revision:match.revision,state:match.state,phase:round.phase,roundKey:round.key,
    selectionOpensAt:new Date(match.createdAt+SURVIVAL_INTRO_MS).toISOString(),nextMap:match.nextMap??null,
    presentationGate:match.presentationGate??null,online:match.online??false,roundNumber:round.phase==="TIEBREAK"?(match.config.matchPlayerCount===64?5:4):round.ordinal,matchPlayerCount:match.config.matchPlayerCount,playerCount:match.players.length,roundPlayerCount:round.players.length,
    mapGeneration:round.mapGeneration,map:round.map,averageLp:match.averageLp,difficultyGroup:match.difficultyGroup,
    configGeneration:match.configGeneration,serverNow:new Date(now).toISOString(),
    preparationWarningAt:round.preparationStartedAt === null ? null : new Date(round.preparationStartedAt+10_000).toISOString(),
    preparationDeadlineAt:round.preparationStartedAt === null ? null : new Date(round.preparationStartedAt+15_000).toISOString(),
    entryRequestedAt:round.entryRequestedAt === null ? null : new Date(round.entryRequestedAt).toISOString(),
    entryOpensAt:survivalEntryOpensAt(round) === null ? null : new Date(survivalEntryOpensAt(round)!).toISOString(),
    entryDeadlineAt:survivalEntryDeadlineAt(round) === null ? null : new Date(survivalEntryDeadlineAt(round)!).toISOString(),
    startsAt:round.startsAt === null ? null : new Date(round.startsAt).toISOString(),
    endsAt:round.endsAt === null ? null : new Date(round.endsAt).toISOString(),
    remainingSeconds:match.state === "PLAYING" && round.endsAt !== null ? Math.max(0,(round.endsAt-now)/1000) : 0,
    standings,roundPlayers,completedRound,top3:standings.slice(0,3),myRank:standings.find(p => p.participantId === participantId)?.rank ?? null,
    cutoffScore:round.phase === "TIEBREAK" && cutoff !== null ? cutoff/1000 : cutoff,
    targetQualifiers:Math.min(round.target,standings.length),finalFirstScore:["FINAL","TIEBREAK"].includes(round.phase) ? standings[0]?.score ?? null : null,
    preparation:round.players.map(p => ({participantId:p.participantId,assetsReady:p.assetsReadyAt !== null,
      entered:p.enteredAt !== null,absent:p.absent??false,entryDeferred:p.entryDeferred??false,
      preparationStatus:p.forfeited?"FORFEITED":p.enteredAt!==null?"ENTERED":p.absent?"ABSENT":p.assetsReadyAt!==null?"ASSETS_READY":p.preparationFailure?"PREPARATION_FAILED":"DOWNLOADING",failed:p.preparationFailure,forfeited:p.forfeited})),
    participants:match.players.map(p => ({participantId:p.participantId,name:p.name,kind:p.kind,gdAccountId:p.gdAccountId??null,lpSnapshot:p.lp,
      displayedTier:match.config.tierBands.find(b=>b.tier===p.displayedTier)?.group??p.displayedTier??survivalTierForLp(p.lp,match.config.tierBands).group,
      eliminationStage:p.eliminationStage,eliminatedAt:p.eliminatedAt===undefined?null:new Date(p.eliminatedAt).toISOString(),finalRank:p.finalRank,forfeitReason:p.forfeitReason})),
    winnerId:match.winnerId,winnerIds:match.winnerIds??(match.winnerId?[match.winnerId]:[]),cancellationReason:match.cancellationReason,settlementInputs:survivalSettlementInputs(match)};
}
