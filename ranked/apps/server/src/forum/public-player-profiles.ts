import {readSurvivalRecords} from "../survival/survival-records.js";
import {publicAchievementTitle} from '../achievement/public-title.js';
import type {SqlExecutor} from '../database/database.port.js';
/** Explicit UI reads only. No account creation, rating writes or provider refresh. */
export async function readPublicPlayerProfiles(db:SqlExecutor,accounts:readonly string[]){
 const ids=[...new Set(accounts.filter(id=>/^[1-9]\d{0,9}$/.test(id)&&Number(id)<=2147483647))];
 const profiles:Record<string,any>={};if(!ids.length)return profiles;
 const {rows}=await db.query<any>(`SELECT p.id AS player_id,p.gd_account_id::text AS account,p.gd_username,
  rp.displayed_tier AS solo_tier,rp.visible_ranked_score AS solo_lp,
  rp.promotion_source_tier,rp.promotion_target_tier,rp.promotion_games_required,rp.promotion_wins_required,rp.promotion_results,
  sp.displayed_tier AS survival_tier,sp.lp AS survival_lp,
  CASE WHEN EXISTS(SELECT 1 FROM ranked_achievement_unlocks u WHERE u.player_id=p.id
   AND u.achievement_id=rp.equipped_achievement_id AND u.achievement_level=rp.equipped_achievement_level)
   THEN rp.equipped_achievement_id END AS title_id,rp.equipped_achievement_level AS title_level
  FROM ranked_players p LEFT JOIN ranked_profiles rp ON rp.player_id=p.id
  LEFT JOIN sur_profiles sp ON sp.player_id=p.id
  WHERE p.gd_account_id=ANY($1::bigint[]) AND NOT p.is_bot`,[ids]);
 const records=await readSurvivalRecords(db,rows.filter(r=>r.survival_tier).map(r=>r.player_id));
 for(const row of rows){
  let marks:unknown=row.promotion_results;if(typeof marks==='string'){try{marks=JSON.parse(marks);}catch{marks=[];}}
  const promotion=row.promotion_source_tier&&row.promotion_target_tier&&row.promotion_games_required!=null&&row.promotion_wins_required!=null?{sourceTier:row.promotion_source_tier,targetTier:row.promotion_target_tier,
   gamesRequired:Number(row.promotion_games_required),winsRequired:Number(row.promotion_wins_required),results:Array.isArray(marks)?marks.filter(m=>m==='WIN'||m==='LOSS'):[]}:null;
  profiles[String(row.account)]={gdAccountId:String(row.account),gdUsername:row.gd_username,
   solo:row.solo_tier?{tier:row.solo_tier,lp:row.solo_lp===null?null:Number(row.solo_lp),promotion}:null,
   survival:row.survival_tier?{tier:row.survival_tier,lp:row.survival_lp==null?null:Number(row.survival_lp),promotion:null,recordSummary:records[row.player_id]}:null,
   equippedTitle:publicAchievementTitle(row.title_id??null,row.title_level===null||row.title_level===undefined?null:Number(row.title_level))};
 }
 return profiles;
}
