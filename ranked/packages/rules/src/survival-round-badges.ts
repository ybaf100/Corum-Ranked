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
/** Resolve only official, completed outcomes. FINAL and every subsequent
 * TIEBREAK share one logical result; tied qualifiers await their deciding round.
 * Missing historical rank/map evidence produces no invented award. */
export function survivalRoundBadges(match:SurvivalMatch):SurvivalRoundBadge[] {
  if(match.state==="CANCELLED")return [];
  const badges=[...(match.roundBadges??[])];
  const seen=new Set(badges.map(b=>`${b.participantId}:${b.roundKey}`));
  for(const round of match.rounds){
    if(!round.map)continue;
    const confirmed=round.finishedAt!==null&&Number.isFinite(round.finishedAt);
    const difficulty=survivalRecordedDifficulty(round.map);if(!difficulty)continue;
    const final=round.phase==="FINAL"||round.phase==="TIEBREAK";
    for(const p of round.players){
      const entrant=match.players.find(e=>e.participantId===p.participantId);if(!entrant)continue;
      if(!confirmed && !(p.forfeited&&entrant.fixedRewardRank&&Number.isFinite(entrant.eliminatedAt)))continue;
      const survived=p.qualified&&!p.forfeited;
      if(final && survived && !(match.state==="FINAL_RESULT" && match.winnerId===p.participantId))continue;
      const rank=p.rank??(p.forfeited?entrant.fixedRewardRank:null)??
        (match.state==="FINAL_RESULT" && entrant.eliminationStage===round.ordinal?entrant.finalRank:null)??
        (match.state==="FINAL_RESULT" && match.winnerId===p.participantId?1:null);
      if(rank===null || !Number.isInteger(rank) || rank<1 || rank>round.players.length)continue;
      const roundKey=final?"FINAL":round.key,key=`${p.participantId}:${roundKey}`;
      if(seen.has(key))continue;
      // A winner tied an earlier Final: use the LAST deciding round's map.
      if(final && survived && match.rounds.some(r=>r.ordinal>round.ordinal && r.finishedAt!==null && r.players.some(v=>v.participantId===p.participantId)))continue;
      badges.push({participantId:p.participantId,roundKey,sourceRoundKey:round.key,ordinal:round.ordinal,
        roundLabel:final?"FINAL":`R${round.ordinal}`,levelId:round.map.levelId,difficulty:difficulty.id,rank,survived,
        grade:survivalBadgeGrade(rank,survived),confirmedAt:new Date(round.finishedAt??entrant.eliminatedAt!).toISOString()});seen.add(key);
    }
  }
  return badges;
}
