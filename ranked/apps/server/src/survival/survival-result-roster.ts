import {survivalSettlementInputs,survivalTierForLp,type SurvivalMatch} from "@corum-ranked/rules";
export interface SurvivalPaidResult {profileAfter:{tier:string;lp:number};actualDelta:number;}
/** A tier becomes an after-tier only from an accepted ledger result. Bots have
 * no GD/profile account; their virtual rating is explicitly computed here. */
export function survivalResultRoster(match:SurvivalMatch,paid:ReadonlyMap<string,SurvivalPaidResult>){
 const inputs=survivalSettlementInputs(match);
 return match.players.map(p=>{
  const input=inputs.find(i=>i.participantId===p.participantId),settled=p.playerId?paid.get(p.playerId):undefined;
  const virtualTier=p.kind==="BOT"&&input?survivalTierForLp(Math.max(0,p.lp+input.delta),match.config.tierBands).group:null;
  const tierAfter=settled?.profileAfter.tier??virtualTier;
  const current=match.rounds.slice().reverse().flatMap(r=>r.players.filter(v=>v.participantId===p.participantId))[0];
  const active=current?.attempts.find(a=>a.endedAt===null);
  return {participantId:p.participantId,playerId:p.playerId,rank:p.finalRank,tiedPlayers:input?.tiedPlayers??null,name:p.name,kind:p.kind,gdAccountId:p.gdAccountId??null,
   lpAfter:settled?.profileAfter.lp??(p.kind==="BOT"&&input?Math.max(0,p.lp+input.delta):null),
   displayedTier:tierAfter??p.displayedTier??survivalTierForLp(p.lp,match.config.tierBands).group,tierAfter,
   settled:!!settled||virtualTier!==null,virtualRating:p.kind==="BOT",delta:settled?.actualDelta??input?.delta??null,
   bestProgress:current?.highestProgress??0,currentProgress:active?.currentProgress??active?.highestProgress??0,
   attemptId:active?.id??null,attemptActive:!!active,eliminated:p.eliminationStage!==null};
 });
}
