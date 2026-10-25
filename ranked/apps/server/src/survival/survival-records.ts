import type {SqlExecutor} from '../database/database.port.js';
export interface SurvivalRecordSummary{completedMatches:number;bestRank:number|null;wins:number;losses:number;winStreak:number}
/** Derived from immutable match outcomes; positive LP for a podium is not a win. */
export async function readSurvivalRecords(db:SqlExecutor,players:readonly string[]){
 const ids=[...new Set(players)];const records:Record<string,SurvivalRecordSummary>={};if(!ids.length)return records;
 for(const id of ids)records[id]={completedMatches:0,bestRank:null,wins:0,losses:0,winStreak:0};
 const {rows}=await db.query<{player_id:string;count:number;best:number;wins:number;streak:number}>(`WITH outcomes AS (
  SELECT p.player_id,p.match_id,p.final_rank,COALESCE(o.settled_at,d.settled_at,m.finished_at) AS completed_at
  FROM sur_match_players p JOIN sur_matches m ON m.id=p.match_id JOIN sur_profiles sp ON sp.player_id=p.player_id
  LEFT JOIN sur_debug_matches d ON d.match_id=m.id LEFT JOIN sur_online_players o ON o.match_id=m.id AND o.player_id=p.player_id
  WHERE p.player_id=ANY($1::uuid[]) AND m.state='FINAL_RESULT' AND p.final_rank>0
   AND COALESCE(o.settled_at,d.settled_at,m.finished_at)>sp.history_visible_since
   AND (d.match_id IS NULL OR d.settled_at IS NOT NULL)
   AND (o.player_id IS NULL OR o.settled_at IS NOT NULL)
 ), ordered AS (
  SELECT *,COUNT(*) FILTER(WHERE final_rank<>1) OVER(PARTITION BY player_id ORDER BY completed_at DESC,match_id DESC ROWS UNBOUNDED PRECEDING) AS losses_since
  FROM outcomes
 ) SELECT player_id,COUNT(*)::int AS count,MIN(final_rank)::int AS best,
  COUNT(*) FILTER(WHERE final_rank=1)::int AS wins,
  COUNT(*) FILTER(WHERE final_rank=1 AND losses_since=0)::int AS streak FROM ordered GROUP BY player_id`,[ids]);
 for(const row of rows)records[row.player_id]={completedMatches:Number(row.count),bestRank:row.best===null?null:Number(row.best),wins:Number(row.wins),losses:Number(row.count)-Number(row.wins),winStreak:Number(row.streak)};
 return records;
}
