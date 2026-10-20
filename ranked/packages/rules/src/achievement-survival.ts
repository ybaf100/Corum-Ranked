import type {SurvivalMatch,SurvivalRound,SurvivalRoundPlayer} from "./survival-match.js";
import {bestPercentOfThree} from "./deathmatch.js";
import type {OneShotAchievementId} from "./achievements.js";

export interface SurvivalAchievementFacts {
  readonly matchId:string; readonly completedAt:number;
  readonly won:boolean; readonly podium:boolean; readonly finalist:boolean;
  readonly survivedRounds:number; readonly soleRoundWins:number;
  readonly oneShots:readonly OneShotAchievementId[];
}
export interface SurvivalAchievementMetrics {
  survival_matches:number; survival_wins:number; survival_podiums:number; survival_finals:number;
  survival_round_survives:number; survival_round_wins:number;
  current_survival_podium_streak:number; survival_podium_streak:number;
  current_survival_win_streak:number; survival_win_streak:number;
}
export const EMPTY_SURVIVAL_ACHIEVEMENT_METRICS:SurvivalAchievementMetrics={
 survival_matches:0,survival_wins:0,survival_podiums:0,survival_finals:0,survival_round_survives:0,survival_round_wins:0,
 current_survival_podium_streak:0,survival_podium_streak:0,current_survival_win_streak:0,survival_win_streak:0,
};
const score=(r:SurvivalRound,p:SurvivalRoundPlayer):number=>r.phase!=="TIEBREAK"?Math.trunc(p.highestProgress):
 bestPercentOfThree(Array.from({length:3},(_,i)=>({progressPercent:Math.round((p.attempts[i]?.highestProgress??0)*1000)/1000,cleared:false})));
const validRound=(r:SurvivalRound):boolean=>r.finishedAt!==null && Number.isFinite(r.finishedAt) && Number.isFinite(r.startsAt) && r.startsAt!==null && r.map!==null;
const placed=(p:SurvivalRoundPlayer):boolean=>!p.forfeited && p.rank!==null && Number.isSafeInteger(p.rank) && p.rank>0;
const perfect=(r:SurvivalRound,p:SurvivalRoundPlayer):boolean=>placed(p) && p.enteredAt!==null &&
 p.attempts.some(a=>a.endedAt!==null && a.startedAt>=r.startsAt! && a.highestProgress===100);

/** Input is the stored server snapshot, never UI standings, LP edits or a client event. */
export function survivalAchievementFacts(match:SurvivalMatch,playerId:string):SurvivalAchievementFacts|null {
 if(match.online!==true || match.state!=="FINAL_RESULT" || match.finishedAt===null || !Number.isFinite(match.finishedAt) ||
    (match.completionKind && match.completionKind!=="BOTS_ONLY_RESULT"))return null;
 const own=match.players.find(p=>p.playerId===playerId && p.kind==="HUMAN");
 if(!own || own.finalRank===null || !Number.isSafeInteger(own.finalRank) || own.finalRank<1)return null;
 const rounds=match.rounds.filter(validRound);
 const entries=rounds.flatMap(r=>{const p=r.players.find(p=>p.participantId===own.participantId);return p?[{r,p}]:[];});
 const won=own.finalRank===1 && (match.winnerIds??(match.winnerId?[match.winnerId]:[])).includes(own.participantId);
 const podium=own.finalRank<=3;
 const sole=({r,p}:{r:SurvivalRound;p:SurvivalRoundPlayer})=>placed(p)&&p.rank===1&&r.players.filter(v=>placed(v)&&v.rank===1).length===1;
 const oneShots=new Set<OneShotAchievementId>(["survival_first_match"]);
 if(podium)oneShots.add("survival_first_podium");if(won)oneShots.add("survival_first_win");
 let allAbove=true,eliminationRounds=0;
 for(const {r,p} of entries){
  if(perfect(r,p))oneShots.add("survival_round_perfect");
  if(r.phase==="FINAL" || r.phase==="TIEBREAK" || r.target>=r.players.length)continue;
  ++eliminationRounds;
  const boundary=r.cutoffScore===null?[]:r.players.filter(v=>placed(v)&&v.qualified&&score(r,v)===r.cutoffScore);
  const boundaryRank=boundary.length?Math.min(...boundary.map(v=>v.rank!)):null;
  if(placed(p)&&p.qualified&&boundaryRank!==null&&p.rank===boundaryRank)oneShots.add("survival_cutoff_escape");
  if(!placed(p)||!p.qualified||boundaryRank===null||p.rank!>=boundaryRank)allAbove=false;
 }
 const finalEntries=entries.filter(({r})=>r.phase==="FINAL"||r.phase==="TIEBREAK");
 const decisive=finalEntries.at(-1);
 if(won&&decisive){
  if(perfect(decisive.r,decisive.p))oneShots.add("survival_final_perfect_win");
  const scores=decisive.r.players.filter(placed).map(p=>score(decisive.r,p)).sort((a,b)=>b-a);
  const difference=scores.length>=2?Math.round((scores[0]!-scores[1]!)*1000)/1000:0;
  if(sole(decisive)&&difference>0&&difference<1)oneShots.add("survival_final_photo_finish");
 }
 if(won&&entries.length>0&&entries.length===rounds.length&&entries.every(sole))oneShots.add("survival_clean_champion");
 if(won&&eliminationRounds>0&&allAbove)oneShots.add("survival_never_below_cutoff");
 const firstFinal=rounds.find(r=>r.phase==="FINAL");
 const qualifier=firstFinal?rounds.filter(r=>r.ordinal<firstFinal.ordinal&&r.phase!=="FINAL"&&r.phase!=="TIEBREAK").at(-1):undefined;
 if(won&&firstFinal&&qualifier){
  const finalists=new Set(firstFinal.players.map(p=>p.participantId));
  const seeds=qualifier.players.filter(p=>placed(p)&&p.qualified&&finalists.has(p.participantId));
  if(seeds.length===finalists.size&&seeds.length>1){const last=Math.max(...seeds.map(p=>p.rank!));const lowest=seeds.filter(p=>p.rank===last);
   if(lowest.length===1&&lowest[0]!.participantId===own.participantId)oneShots.add("survival_last_seed_champion");}
 }
 return {matchId:match.id,completedAt:match.finishedAt,won,podium,finalist:finalEntries.length>0,
  survivedRounds:entries.filter(({p})=>placed(p)&&p.qualified).length,soleRoundWins:entries.filter(sole).length,oneShots:[...oneShots]};
}

/** Sort immutable, deduplicated facts in completion order, including late recovery. */
export function summarizeSurvivalAchievements(facts:readonly SurvivalAchievementFacts[]):SurvivalAchievementMetrics {
 const out={...EMPTY_SURVIVAL_ACHIEVEMENT_METRICS};
 const unique=new Map(facts.map(f=>[f.matchId,f]));
 for(const f of [...unique.values()].sort((a,b)=>a.completedAt-b.completedAt||a.matchId.localeCompare(b.matchId))){
  ++out.survival_matches;out.survival_wins+=Number(f.won);out.survival_podiums+=Number(f.podium);out.survival_finals+=Number(f.finalist);
  out.survival_round_survives+=f.survivedRounds;out.survival_round_wins+=f.soleRoundWins;
  out.current_survival_podium_streak=f.podium?out.current_survival_podium_streak+1:0;
  out.current_survival_win_streak=f.won?out.current_survival_win_streak+1:0;
  out.survival_podium_streak=Math.max(out.survival_podium_streak,out.current_survival_podium_streak);
  out.survival_win_streak=Math.max(out.survival_win_streak,out.current_survival_win_streak);
 }
 return out;
}
