import {type SurvivalMatch,survivalMilliPercent} from '@corum-ranked/rules';
/** Own official state only. No file bytes, uploads, reporting or scoring writes. */
export function survivalReplayReceipt(match:SurvivalMatch,participantId:string,attemptId:string){
 for(const round of match.rounds){
  const player=round.players.find(p=>p.participantId===participantId);
  const attempt=player?.attempts.find(a=>a.id===attemptId);if(!attempt||!player)continue;
  const scoring=(p:number)=>round.phase==='TIEBREAK'?survivalMilliPercent(p):Math.trunc(p);
  return {accepted:true,matchId:match.id,participantId,attemptId,attemptOrdinal:attempt.sequence,
   roundKey:round.key,roundNumber:round.ordinal,phase:round.phase,mapGeneration:round.mapGeneration,
   levelId:round.map?.levelId??null,mapVersion:round.map?.levelVersion??null,geometryHash:round.map?.geometrySha256??null,
   startedAt:attempt.startedAt,endedAt:attempt.endedAt,ended:attempt.endedAt!==null,
   progress:attempt.highestProgress,roundBest:player.highestProgress,cleared:attempt.endedAt!==null&&attempt.highestProgress===100,
   score:scoring(attempt.highestProgress),roundBestScore:scoring(player.highestProgress),debug:!match.online};
 }
 return {accepted:false,matchId:match.id,participantId,attemptId};
}
