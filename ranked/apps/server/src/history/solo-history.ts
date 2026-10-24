import type { DatabasePort } from '../database/database.port.js';
const object=(v:unknown):Record<string,any>=>{if(typeof v==='string'){try{return JSON.parse(v);}catch{return {};}}return v&&typeof v==='object'?v as Record<string,any>:{};};
export const recordedLevelId=(v:unknown):number|null=>{const n=typeof v==='string'&&/^\d+$/.test(v)?Number(v):v;return typeof n==='number'&&Number.isSafeInteger(n)&&n>0&&n<=2147483647?n:null;};
function presentationMetadata(map:Record<string,any>){
 const metadata=object(map.metadata);
 const length=map.length??metadata.lengthCategory;
 return {length:typeof length==='string'?length:typeof length==='number'?['TINY','SHORT','MEDIUM','LONG','XL','PLATFORMER'][length]??null:null,
  stars:typeof map.stars==='number'?map.stars:typeof metadata.stars==='number'?metadata.stars:null,
  featureScore:typeof metadata.featureScore==='number'?metadata.featureScore:null,epic:typeof metadata.epic==='number'?metadata.epic:null};
}
interface Match {id:string;player_a_id:string;player_b_id:string;selected_round_maps_snapshot?:unknown;}
interface Row {id:string;match_id:string;[key:string]:any;}
interface Stats {round_id:string;player_id:string;best:number|null;cleared:boolean;attempts:number;}
/** Four bounded queries for the entire 30-match page; no N+1 attempt lookups. */
export async function soloHistoryDetails(db:DatabasePort,matches:readonly Match[]){
 const ids=matches.map(m=>m.id);const out=new Map<string,{rounds:any[];deathmatches:any[]}>();
 if(!ids.length)return out;
 const [rounds,dms,normal,duels]=await Promise.all([
  db.query<Row>('SELECT * FROM ranked_rounds WHERE match_id=ANY($1::uuid[]) ORDER BY match_id,round_number',[ids]),
  db.query<Row>('SELECT * FROM ranked_deathmatches WHERE match_id=ANY($1::uuid[]) ORDER BY match_id,sequence',[ids]),
  db.query<Stats>(`SELECT a.round_id,a.player_id,MAX(a.progress_percent)::float AS best,BOOL_OR(a.cleared) AS cleared,COUNT(*)::int AS attempts
   FROM ranked_attempts a JOIN ranked_rounds r ON r.id=a.round_id WHERE r.match_id=ANY($1::uuid[]) AND a.valid AND a.ended_at IS NOT NULL GROUP BY a.round_id,a.player_id`,[ids]),
  db.query<Stats>(`SELECT a.deathmatch_id AS round_id,a.player_id,MAX(a.progress_percent)::float AS best,BOOL_OR(a.cleared) AS cleared,COUNT(*)::int AS attempts
   FROM ranked_deathmatch_attempts a JOIN ranked_deathmatches d ON d.id=a.deathmatch_id WHERE d.match_id=ANY($1::uuid[]) AND a.valid AND a.ended_at IS NOT NULL GROUP BY a.deathmatch_id,a.player_id`,[ids]),
 ]);
 const index=(rows:readonly Stats[])=>new Map(rows.map(r=>[r.round_id+':'+r.player_id,r]));const ns=index(normal.rows),ds=index(duels.rows);
 for(const m of matches){
  const stats=(r:Row,source:Map<string,Stats>)=>{const a=source.get(r.id+':'+m.player_a_id),b=source.get(r.id+':'+m.player_b_id);return {bestProgressA:a?.best==null?null:Number(a.best),bestProgressB:b?.best==null?null:Number(b.best),clearedA:a?.cleared??false,clearedB:b?.cleared??false,attemptsA:Number(a?.attempts??0),attemptsB:Number(b?.attempts??0)};};
  const maps=Array.isArray(m.selected_round_maps_snapshot)?m.selected_round_maps_snapshot:object(m.selected_round_maps_snapshot);
  const ownDms=dms.rows.filter(r=>r.match_id===m.id);
  out.set(m.id,{
   rounds:rounds.rows.filter(r=>r.match_id===m.id).map(r=>{const map=Array.isArray(maps)?object(maps[Number(r.round_number)-1]):{};return {
    roundNumber:Number(r.round_number),mapTitle:r.title,levelId:recordedLevelId(r.playable_level_id),difficulty:r.difficulty,creator:r.creator,
    ...presentationMetadata(map),
    completed:r.settled_at!==null,result:r.result,scoreA:Number(r.score_a??0),scoreB:Number(r.score_b??0),clearsA:Number(r.clears_a??0),clearsB:Number(r.clears_b??0),...stats(r,ns),
   };}),
   deathmatches:ownDms.map((r,i)=>{const map=object(r.map_snapshot);const winnerSide=r.winner_id===m.player_a_id?'A':r.winner_id===m.player_b_id?'B':null;return {
    sequence:Number(r.sequence),mapTitle:map.title??'',levelId:recordedLevelId(map.playableLevelId),difficulty:map.difficulty??null,creator:map.creator??null,...presentationMetadata(map),
    scoreA:r.score_a===null?null:Number(r.score_a),scoreB:r.score_b===null?null:Number(r.score_b),winnerSide,completed:r.finished_at!==null,
    finishedAt:r.finished_at?new Date(r.finished_at).toISOString():null,nextDeathmatchSequence:r.finished_at&&!winnerSide&&ownDms[i+1]?Number(ownDms[i+1]!.sequence):null,...stats(r,ds),
   };}),
  });
 }
 return out;
}
