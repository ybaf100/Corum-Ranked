import { reduceSurvivalMatch,simulateSurvivalBots,applySurvivalBotFrames,survivalCurrentRound,survivalEntryOpensAt,survivalEntryDeadlineAt,type SurvivalBotDriverState,type SurvivalCommand,type SurvivalMatch } from "@corum-ranked/rules";

/** Shared NORMAL simulation; driver state and authoritative attempts commit atomically. */
export function driveSurvivalBots(before:SurvivalMatch,seed:number,previous:SurvivalBotDriverState,now:number,humanLp?:readonly number[]) {
  let match=before;const round=survivalCurrentRound(match);
  const humans=round.players.filter(p=>!p.forfeited && !p.absent && match.players.some(e=>e.participantId===p.participantId && e.kind==="HUMAN"));
  const bots=match.players.filter(p=>p.kind==="BOT");
  const active=bots.filter(p=>round.players.some(r=>r.participantId===p.participantId && !r.forfeited));
  const context={roundKey:round.key,mapGeneration:round.mapGeneration,levelId:round.map?.levelId??0};
  // Native readiness never comes from bots. Their own simulated resources can
  // be certified before a due timer is adjudicated after a delayed worker tick.
  const resourceAt=match.state==="PREPARING"&&round.preparationStartedAt!==null?Math.min(now,round.preparationStartedAt+14999):
    match.state==="ENTERING"&&round.entryRequestedAt!==null?Math.min(now,survivalEntryDeadlineAt(round)!-1):now;
  const apply=(command:SurvivalCommand,at=now)=>{match=reduceSurvivalMatch(match,command,Math.max(match.updatedAt,at));};
  if(humans.length && match.state==="PREPARING" && humans.every(p=>p.preparationFailure!==null && p.assetsReadyAt===null)) {
    for(const p of active)apply({type:"PREPARATION_FAILED",...context,participantId:p.participantId,reason:"HUMAN_PREPARATION_FAILED"},resourceAt);
  } else if(match.state==="PREPARING" && (!humans.length || match.online || humans.some(p=>p.assetsReadyAt!==null))) {
    for(const p of active)apply({type:"ASSETS_READY",...context,participantId:p.participantId,certificate:round.map!},resourceAt);
  }
  if(match.state==="ENTERING" && now>=(survivalEntryOpensAt(survivalCurrentRound(match))??Infinity) && (humans.some(p=>p.enteredAt!==null) || !humans.length)) {
    for(const p of active)apply({type:"ENTERED",...context,participantId:p.participantId},resourceAt);
  }
  // Catch up before the engine adjudicates the authoritative deadline.
  const current=survivalCurrentRound(match);
  if(match.state==="COUNTDOWN" && current.startsAt!==null && now>=current.startsAt)
    apply({type:"TICK"},Math.min(now,current.endsAt!-1));
  if(match.state!=="PLAYING" || current.startsAt===null || current.endsAt===null || !active.length)
    return {match,cursor:previous};
  const {state,frames}=simulateSurvivalBots(match,seed,previous,now,humanLp);
  match=applySurvivalBotFrames(match,context,frames,Math.min(now,current.endsAt-1));
  return {match,cursor:state};
}
