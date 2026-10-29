import {Inject,Injectable,type OnApplicationBootstrap} from "@nestjs/common";
import {SURVIVAL_BADGE_GRADES,survivalCollectibleDifficulties,survivalRoundBadges,type SurvivalMatch} from "@corum-ranked/rules";
import {DATABASE,type DatabasePort,type SqlExecutor} from "../database/database.port.js";

/** Transactional result projection; clients cannot submit grades or increments. */
export async function persistSurvivalBadges(tx:SqlExecutor,match:SurvivalMatch):Promise<void>{
  const rows=survivalRoundBadges(match).map(b=>({...b,playerId:match.players.find(p=>p.participantId===b.participantId&&p.kind==="HUMAN")?.playerId??null}));
  if(!rows.length)return;
  await tx.query(`INSERT INTO sur_round_badges(match_id,participant_id,player_id,logical_round_key,source_round_key,ordinal,round_label,
    level_id,difficulty,official_rank,survived,grade,confirmed_at)
    SELECT $1,x."participantId",x."playerId",x."roundKey",x."sourceRoundKey",x.ordinal,x."roundLabel",x."levelId",x.difficulty,x.rank,x.survived,x.grade,x."confirmedAt"
    FROM jsonb_to_recordset($2::jsonb) AS x("participantId" text,"playerId" uuid,"roundKey" text,"sourceRoundKey" text,ordinal integer,
      "roundLabel" text,"levelId" bigint,difficulty text,rank integer,survived boolean,grade text,"confirmedAt" timestamptz)
    ON CONFLICT DO NOTHING`,[match.id,JSON.stringify(rows)]);
}
@Injectable()
export class SurvivalBadgesService implements OnApplicationBootstrap {
  public constructor(@Inject(DATABASE)private readonly db:DatabasePort){}
  public async onApplicationBootstrap(){
    // Only confirmed historical results. Missing rank/map evidence is skipped.
    // Existing snapshots/attempts are never rewritten; replaying this is safe.
    let after="00000000-0000-0000-0000-000000000000";
    for(;;){
      const page=await this.db.query<{id:string;snapshot:SurvivalMatch}>("SELECT id,snapshot FROM sur_matches WHERE state='FINAL_RESULT' AND id>$1 ORDER BY id LIMIT 100",[after]);
      for(const row of page.rows)await this.db.transaction(tx=>persistSurvivalBadges(tx,row.snapshot));
      if(page.rows.length<100)break;after=page.rows.at(-1)!.id;
    }
  }
  public async collection(playerId:string){
    const rows=await this.db.query<{difficulty:string;grade:string;count:string}>(`SELECT b.difficulty,b.grade,count(*)::text AS count FROM sur_round_badges b
      JOIN sur_matches m ON m.id=b.match_id WHERE b.player_id=$1 AND m.state<>'CANCELLED' GROUP BY b.difficulty,b.grade`,[playerId]);
    const counts=(difficulty?:string)=>Object.fromEntries(SURVIVAL_BADGE_GRADES.map(grade=>[grade,rows.rows.filter(r=>r.grade===grade&&(!difficulty||r.difficulty===difficulty)).reduce((s,r)=>s+Number(r.count),0)]));
    const difficulties=survivalCollectibleDifficulties().map(d=>({...d,counts:counts(d.id)}));
    const eliminatedByDifficulty=rows.rows.filter(r=>r.grade==="ELIMINATED").map(r=>({difficulty:r.difficulty,count:Number(r.count)}));
    return {difficulties,totals:counts(),eliminatedByDifficulty,grades:SURVIVAL_BADGE_GRADES};
  }
}
