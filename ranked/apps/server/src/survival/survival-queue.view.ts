import type { DatabasePort } from "../database/database.port.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { sessionAliveSince } from "../session/session-presence.policy.js";
import type { SurvivalConfigService } from "../config/survival-config.service.js";
export async function survivalQueueView(db:DatabasePort,session:RankedSessionContext,now:Date,config:SurvivalConfigService){
  const own=(await db.query<{status:string;matched_match_id:string|null;wait_started_at:Date|string}>("SELECT status,matched_match_id,wait_started_at FROM sur_queue_entries WHERE player_id=$1 AND session_id=$2",[session.playerId,session.sessionId])).rows[0];
  const count=(await db.query<{n:number}>(`SELECT COUNT(*)::int AS n FROM sur_queue_entries q JOIN ranked_sessions s ON s.id=q.session_id
    WHERE q.status='QUEUED' AND q.last_heartbeat_at>$1 AND s.revoked_at IS NULL AND s.expires_at>$2 AND s.last_heartbeat_at>$1`,[sessionAliveSince(now),now.toISOString()])).rows[0]!.n;
  const wait=own?.status==="QUEUED"?Math.max(0,(now.getTime()-new Date(own.wait_started_at).getTime())/1000):0;
  return {status:own?.status??"LEFT",matchId:own?.matched_match_id??null,serverNow:now.toISOString(),enabled:config.getStatus().enabled,
    players:Math.min(16,Number(count)),totalWaiting:Number(count),maximumPlayers:16,minimumPlayers:8,shortMinimumPlayers:2,waitingSeconds:wait,
    waitingSince:own?.wait_started_at?new Date(own.wait_started_at).toISOString():null,remainingSeconds:Math.max(0,180-wait)};
}
