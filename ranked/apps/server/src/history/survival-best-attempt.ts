import {survivalMilliPercent,type SurvivalRound} from '@corum-ranked/rules';
/** Shared by History and the owner receipt. Ties retain the existing first
 * ended attempt; a real Clear takes priority over a rounded near-Clear. */
export function survivalBestEndedAttempt(round:SurvivalRound,participantId:string){
 const attempts=round.players.find(p=>p.participantId===participantId)?.attempts.filter(a=>a.endedAt!==null)??[];
 const score=(v:number)=>round.phase==='TIEBREAK'?survivalMilliPercent(v):Math.trunc(v);
 return attempts.reduce<typeof attempts[number]|undefined>((best,a)=>!best||score(a.highestProgress)>score(best.highestProgress)||(a.highestProgress===100&&best.highestProgress!==100)?a:best,undefined);
}
