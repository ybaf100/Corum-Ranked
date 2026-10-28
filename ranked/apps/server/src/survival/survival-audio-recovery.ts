import type {SqlExecutor} from "../database/database.port.js";
import {reduceSurvivalMatch,survivalCurrentRound,type SurvivalMatch} from "@corum-ranked/rules";
export const AUDIO_FAILURE_CATEGORIES=["AUDIO_UNAVAILABLE_PERMANENT","AUDIO_DOWNLOAD_TRANSIENT","AUDIO_DOWNLOAD_TIMEOUT","AUDIO_METADATA_UNAVAILABLE","AUDIO_CACHE_INVALID","AUDIO_STORAGE_FAILURE","AUDIO_UNKNOWN_ERROR"] as const;
export interface SurvivalAudioFailure {category:typeof AUDIO_FAILURE_CATEGORIES[number];resourceKind:"SONG"|"SFX";resourceId:number;nativeCode:number;}
/** Caller holds the match row and has revalidated the authenticated account.
 * Native errors are evidence, never authority. Two stable independent accounts
 * must agree on the same current dependency; session rotation adds no vote. */
export async function reportSurvivalAudioFailure(tx:SqlExecutor,match:SurvivalMatch,playerId:string,
  report:SurvivalAudioFailure,now:number):Promise<SurvivalMatch>{
  const round=survivalCurrentRound(match);
  const entrant=match.players.find(p=>p.playerId===playerId),participant=round.players.find(p=>p.participantId===entrant?.participantId);
  if(!match.online || !["PREPARING","ENTERING"].includes(match.state) || !round.map || !entrant || entrant.kind!=="HUMAN" ||
    entrant.eliminationStage!==null || !participant || participant.forfeited || participant.assetsReadyAt!==null)return match;
  await tx.query(`INSERT INTO sur_audio_failure_reports(match_id,round_key,map_generation,level_id,player_id,resource_kind,resource_id,category,native_code,reported_at)
    SELECT $1,$2,$3,$4,$5,$6,$7,$8,$9,$10 WHERE
      (SELECT count(*) FROM sur_audio_failure_reports WHERE match_id=$1 AND round_key=$2 AND map_generation=$3 AND player_id=$5)<32
      OR EXISTS(SELECT 1 FROM sur_audio_failure_reports WHERE match_id=$1 AND round_key=$2 AND map_generation=$3 AND player_id=$5 AND resource_kind=$6 AND resource_id=$7)
    ON CONFLICT(match_id,round_key,map_generation,player_id,resource_kind,resource_id)
    DO UPDATE SET category=EXCLUDED.category,native_code=EXCLUDED.native_code,reported_at=EXCLUDED.reported_at`,
    [match.id,round.key,round.mapGeneration,round.map.levelId,playerId,report.resourceKind,report.resourceId,report.category,report.nativeCode,new Date(now).toISOString()]);
  if(report.category!=="AUDIO_UNAVAILABLE_PERMANENT" || report.nativeCode!==2)return match;
  const votes=await tx.query<{count:string}>(`SELECT count(DISTINCT r.player_id)::text AS count FROM sur_audio_failure_reports r
    JOIN sur_match_players p ON p.match_id=r.match_id AND p.player_id=r.player_id
    WHERE r.match_id=$1 AND r.round_key=$2 AND r.map_generation=$3 AND r.level_id=$4
      AND r.resource_kind=$5 AND r.resource_id=$6 AND r.category='AUDIO_UNAVAILABLE_PERMANENT' AND r.native_code=2
      AND r.reported_at>$7 AND p.player_kind='HUMAN' AND p.status='ACTIVE'`,
    [match.id,round.key,round.mapGeneration,round.map.levelId,report.resourceKind,report.resourceId,new Date(now-120_000).toISOString()]);
  if(Number(votes.rows[0]?.count??0)<2)return match;
  const until=new Date(now+60*60_000).toISOString(),at=new Date(now).toISOString();
  const quarantine=async(kind:string,id:number)=>{
    await tx.query(`INSERT INTO sur_resource_quarantine(resource_kind,resource_id,reason,verified_at,retry_after)
      VALUES($1,$2,'INDEPENDENT_NATIVE_NOT_ALLOWED_REPORTS',$3,$4) ON CONFLICT(resource_kind,resource_id)
      DO UPDATE SET reason=EXCLUDED.reason,verified_at=EXCLUDED.verified_at,retry_after=EXCLUDED.retry_after`,[kind,id,at,until]);
  };
  await quarantine("MAP",round.map.levelId);
  // Only a server-extracted dependency may exclude other maps. Unknown resource
  // IDs can at most quarantine this context's map after independent corroboration.
  const known=report.resourceKind==="SONG"?round.map.songIds:round.map.sfxIds;
  if(known?.includes(report.resourceId))await quarantine(report.resourceKind,report.resourceId);
  await tx.query(`UPDATE sur_map_candidates SET status='INVALID',invalid_reason='COMMON_AUDIO_UNAVAILABLE',retry_after=$2
    WHERE level_id=$1 AND status<>'CONSUMED'`,[round.map.levelId,until]);
  const next=reduceSurvivalMatch(match,{type:"MAP_INVALIDATED",roundKey:round.key,mapGeneration:round.mapGeneration,reason:"COMMON_AUDIO_UNAVAILABLE"},now);
  if(next.nextMap && (next.nextMap.map.levelId===round.map.levelId ||
    (report.resourceKind==="SONG"?next.nextMap.map.songIds:next.nextMap.map.sfxIds)?.includes(report.resourceId)))delete next.nextMap;
  return next;
}
