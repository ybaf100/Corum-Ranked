import { reduceSurvivalMatch,survivalBotPlan,survivalBotTiebreak,survivalCurrentRound,type SurvivalCommand,type SurvivalMatch } from "@corum-ranked/rules";

/** Same reducer, seeded records and durable caller-owned cursors for M4 and M5. */
export function driveSurvivalBots(before:SurvivalMatch,seed:number,previous:Record<string,number>,now:number) {
  let match=before;const round=survivalCurrentRound(match);
  const humans=round.players.filter(p=>!p.forfeited && !p.absent && match.players.some(e=>e.participantId===p.participantId && e.kind==="HUMAN"));
  const bots=match.players.filter(p=>p.kind==="BOT");
  const active=bots.filter(p=>round.players.some(r=>r.participantId===p.participantId && !r.forfeited));
  const context={roundKey:round.key,mapGeneration:round.mapGeneration,levelId:round.map?.levelId??0};
  // Native readiness never comes from bots. Their own simulated resources can
  // be certified before a due timer is adjudicated after a delayed worker tick.
  const resourceAt=match.state==="PREPARING"&&round.preparationStartedAt!==null?Math.min(now,round.preparationStartedAt+14999):
    match.state==="ENTERING"&&round.entryRequestedAt!==null?Math.min(now,round.firstEnteredAt===null?round.entryRequestedAt+4999:round.firstEnteredAt+9999):now;
  const apply=(command:SurvivalCommand,at=now)=>{match=reduceSurvivalMatch(match,command,Math.max(match.updatedAt,at));};
  if(humans.length && match.state==="PREPARING" && humans.every(p=>p.preparationFailure!==null && p.assetsReadyAt===null)) {
    for(const p of active)apply({type:"PREPARATION_FAILED",...context,participantId:p.participantId,reason:"HUMAN_PREPARATION_FAILED"},resourceAt);
  } else if(match.state==="PREPARING" && (match.online || humans.some(p=>p.assetsReadyAt!==null))) {
    for(const p of active)apply({type:"ASSETS_READY",...context,participantId:p.participantId,certificate:round.map!},resourceAt);
  }
  if(match.state==="ENTERING" && (humans.some(p=>p.enteredAt!==null) || (match.online && !humans.length))) {
    for(const p of active)apply({type:"ENTERED",...context,participantId:p.participantId},resourceAt);
  }
  const cursor={...previous};
  if(match.state==="PLAYING" && round.startsAt!==null && round.endsAt!==null) {
    const at=Math.min(now,round.endsAt-1);
    for(const p of active){
      const index=bots.findIndex(b=>b.participantId===p.participantId),key=`${round.key}:${round.mapGeneration}:${p.participantId}`;
      const used=cursor[key]??-1;
      if(round.phase==="TIEBREAK") {
        for(const [i,progress] of survivalBotTiebreak(seed,`${round.key}:${round.mapGeneration}`,index).entries()) {
          if(i<=used)continue;const attemptId=`bot-dm-${i+1}`;
          apply({type:"ATTEMPT_START",...context,participantId:p.participantId,attemptId},at);
          apply({type:"ATTEMPT_END",...context,participantId:p.participantId,attemptId,progress},at);cursor[key]=i;
        }
      } else for(const e of survivalBotPlan(seed,`${round.key}:${round.mapGeneration}`,index).filter(e=>e.offsetMs<=now-round.startsAt! && e.index>used)) {
        if(used<0 && e.index===0)apply({type:"ATTEMPT_START",...context,participantId:p.participantId,attemptId:"bot-normal"},at);
        apply({type:"PROGRESS",...context,participantId:p.participantId,attemptId:"bot-normal",progress:e.best},at);cursor[key]=e.index;
      }
    }
  }
  return {match,cursor};
}
