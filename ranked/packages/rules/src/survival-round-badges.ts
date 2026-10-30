import type {SurvivalMatch} from "./survival-match.js";
import {survivalRecordedDifficulty} from "./survival-maps.js";

export const SURVIVAL_BADGE_GRADES = ["MYTHIC","LEGENDARY","EPIC","FEATURED","RATED","ELIMINATED"] as const;
export type SurvivalBadgeGrade = typeof SURVIVAL_BADGE_GRADES[number];
export interface SurvivalRoundBadge {
  participantId:string; roundKey:string; sourceRoundKey:string; ordinal:number; roundLabel:string;
  levelId:number; difficulty:string; rank:number; survived:boolean; grade:SurvivalBadgeGrade; confirmedAt:string;
}
export function survivalBadgeGrade(rank:number,survived:boolean):SurvivalBadgeGrade {
  if(!survived)return "ELIMINATED";
  return rank===1?"MYTHIC":rank===2?"LEGENDARY":rank===3?"EPIC":rank<=5?"FEATURED":"RATED";
}
/** Presentation aliases for legacy ledger rows are resolved from their actual
 * source round. Award grade/rank/map evidence is never recalculated here. */
export function survivalPresentRoundBadges(match:SurvivalMatch,rows:readonly SurvivalRoundBadge[]):SurvivalRoundBadge[] {
  const rounds=Array.isArray(match.rounds)?match.rounds:[];
  const deathmatches=rounds.filter(r=>r.phase==="TIEBREAK");
  const seen=new Set<string>();
  return rows.flatMap(b=>{
    const identity=`${b.participantId}:${b.sourceRoundKey||b.roundKey}`;if(seen.has(identity))return [];seen.add(identity);
    const source=rounds.find(r=>r.key===b.sourceRoundKey);
    const known=source&&["ROUND_1","ROUND_2","ROUND_3","ROUND_4","FINAL","TIEBREAK"].includes(source.phase)&&Number.isInteger(source.ordinal)&&source.ordinal>0;
    return [{...b,...(known?{roundKey:source.key,roundLabel:source.phase==="FINAL"?"FINAL":source.phase==="TIEBREAK"?`DM${deathmatches.findIndex(r=>r.key===source.key)+1}`:`R${source.ordinal}`}:{})}];
  }).sort((a,b)=>a.ordinal-b.ordinal||a.participantId.localeCompare(b.participantId));
}
/** Resolve only official, completed outcomes, once per actual source round.
 * Advancing tied leaders earn that round's badge before entering a new DM.
 * Missing historical rank/map evidence produces no invented award. */
export function survivalRoundBadges(match:SurvivalMatch):SurvivalRoundBadge[] {
  if(match.state==="CANCELLED"||!Array.isArray(match.rounds)||!Array.isArray(match.players))return [];
  const badges=survivalPresentRoundBadges(match,match.roundBadges??[]);
  const seen=new Set(badges.map(b=>`${b.participantId}:${b.sourceRoundKey||b.roundKey}`));
  const deathmatches=match.rounds.filter(r=>r.phase==="TIEBREAK");
  for(const round of match.rounds){
    if(!["ROUND_1","ROUND_2","ROUND_3","ROUND_4","FINAL","TIEBREAK"].includes(round.phase) || !round.map || !Array.isArray(round.players) || !round.key || !Number.isInteger(round.ordinal) || round.ordinal<1 ||
      !Number.isSafeInteger(round.map.levelId) || round.map.levelId<1 || round.map.levelId>2_147_483_647)continue;
    const confirmed=round.finishedAt!==null&&Number.isFinite(round.finishedAt);
    const difficulty=survivalRecordedDifficulty(round.map);if(!difficulty)continue;
    for(const p of round.players){
      const entrant=match.players.find(e=>e.participantId===p.participantId);if(!entrant)continue;
      if(!confirmed && !(p.forfeited&&entrant.fixedRewardRank&&Number.isFinite(entrant.eliminatedAt)))continue;
      const survived=p.qualified&&!p.forfeited;
      const rank=p.rank??(p.forfeited?entrant.fixedRewardRank:null)??
        (match.state==="FINAL_RESULT" && entrant.eliminationStage===round.ordinal?entrant.finalRank:null)??
        (match.state==="FINAL_RESULT" && match.winnerId===p.participantId?1:null);
      if(rank===null || !Number.isInteger(rank) || rank<1 || rank>round.players.length)continue;
      const roundKey=round.key,key=`${p.participantId}:${roundKey}`;
      if(seen.has(key))continue;
      badges.push({participantId:p.participantId,roundKey,sourceRoundKey:round.key,ordinal:round.ordinal,
        roundLabel:round.phase==="FINAL"?"FINAL":round.phase==="TIEBREAK"?`DM${deathmatches.findIndex(r=>r.key===round.key)+1}`:`R${round.ordinal}`,levelId:round.map.levelId,difficulty:difficulty.id,rank,survived,
        grade:survivalBadgeGrade(rank,survived),confirmedAt:new Date(round.finishedAt??entrant.eliminatedAt!).toISOString()});seen.add(key);
    }
  }
  return survivalPresentRoundBadges(match,badges);
}
