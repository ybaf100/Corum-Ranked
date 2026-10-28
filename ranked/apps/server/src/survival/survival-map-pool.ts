import {Logger,type OnApplicationBootstrap,type OnApplicationShutdown} from "@nestjs/common";
import {survivalMapAllowed,survivalMapRange,type SurvivalVerifiedMap,type SurvivalDifficultyGroup,type SurvivalMapPhase} from "@corum-ranked/rules";
import type {DatabasePort,SqlExecutor} from "../database/database.port.js";
import type {ServerClock} from "../common/runtime.module.js";
import {SurvivalMapError,type SurvivalMapProvider,type SurvivalMapRequest} from "./survival-map.provider.js";

const TTL_MS=6*60*60_000,TARGET=3,MAX_BUCKETS=32;
export function survivalCandidateBucket(request:SurvivalMapRequest):string {
  const range=survivalMapRange(request.group,request.phase);
  // 16/64 and R3/R4 share a bucket only when every eligibility predicate agrees.
  return JSON.stringify([Math.max(40_000_000,request.minimumLevelId),range.minimumStars,range.maximumStars,range.demons]);
}
interface CandidateRow {bucket_key:string;level_id:string;metadata:SurvivalVerifiedMap;expires_at:Date|string;}
interface Bucket {request:SurvivalMapRequest;ready:number;nextAt:number;failures:number;lastError:SurvivalMapError|null;diagnosticKey:string;}
/** Durable certificates, not geometry blobs. Event-driven work has no SQL idle
 * poll. Backoff performs upstream work only; DB is written on actual progress. */
export class SurvivalMapPool implements SurvivalMapProvider,OnApplicationBootstrap,OnApplicationShutdown {
  private readonly logger=new Logger(SurvivalMapPool.name);
  private readonly buckets=new Map<string,Bucket>();
  private readonly known=new Set<number>();
  private readonly jobs=new Map<string,Promise<void>>();
  private readonly queued=new Map<string,()=>Promise<void>>();
  private running=false;
  private priority:string|null=null;
  private timer:ReturnType<typeof setTimeout>|null=null;
  private stopping=false;
  private initialFill:Promise<void>=Promise.resolve();
  public waitForInitialFill():Promise<void>{return this.initialFill;}
  public constructor(private readonly db:DatabasePort,private readonly source:SurvivalMapProvider,
    private readonly clock:ServerClock,private readonly minimumLevelId:number|(()=>number)=40_000_000){}
  private now(){return this.clock.now().getTime();}
  private valid(map:SurvivalVerifiedMap,request:SurvivalMapRequest):boolean{
    return !!map && survivalMapAllowed(map,request.group,request.phase,request.minimumLevelId) &&
      typeof map.geometrySha256==='string' && /^[a-f0-9]{64}$/.test(map.geometrySha256) &&
      Number.isSafeInteger(map.geometryBytes) && map.geometryBytes>0 && map.geometryBytes<=32*1024*1024 &&
      Number.isSafeInteger(map.levelVersion) && map.levelVersion>0 && Number.isFinite(Date.parse(map.verifiedAt));
  }
  public async onApplicationBootstrap():Promise<void>{
    // Recover leased selections from the durable authoritative adoption history.
    await this.db.transaction(async tx=>{
      await tx.query("SELECT pg_advisory_xact_lock(hashtext('survival-candidate-reserve-v1'))");
      await tx.query(`UPDATE sur_map_candidates c SET status='CONSUMED',reservation_token=NULL,lease_until=NULL
        WHERE status='RESERVED' AND EXISTS(SELECT 1 FROM sur_map_selection_history h WHERE h.match_id=c.match_id
          AND h.round_key=c.round_key AND h.map_generation=c.map_generation AND h.level_id=c.level_id)`);
      await tx.query(`UPDATE sur_map_candidates SET status='READY',reservation_token=NULL,lease_until=NULL
        WHERE status='RESERVED' AND lease_until<=$1`,[this.clock.now().toISOString()]);
    });
    let minimum=40_000_000;try{minimum=typeof this.minimumLevelId==="function"?this.minimumLevelId():this.minimumLevelId;}catch{}
    for(const group of ["RED_BRONZE","SILVER","GOLD"] as SurvivalDifficultyGroup[])
      for(const phase of ["ROUND_1","ROUND_2","ROUND_3","ROUND_4","FINAL","TIEBREAK"] as SurvivalMapPhase[])
        await this.register({group,phase,minimumLevelId:minimum,excludedLevelIds:[]});
    const recent=await this.db.query<{level_id:string}>("SELECT level_id FROM sur_map_selection_history ORDER BY id DESC LIMIT 30");
    for(const row of recent.rows)this.known.add(Number(row.level_id));
    const rows=(await this.db.query<CandidateRow>("SELECT bucket_key,level_id,metadata,expires_at FROM sur_map_candidates WHERE status='READY' AND expires_at>$1",[this.clock.now().toISOString()])).rows;
    for(const row of rows){this.known.add(Number(row.level_id));const bucket=this.buckets.get(row.bucket_key);if(bucket){bucket.ready++;bucket.nextAt=Math.min(bucket.nextAt,new Date(row.expires_at).getTime());}}
    for(const [key,bucket]of this.buckets)if(bucket.ready<TARGET)this.refill(key);
    this.initialFill=Promise.all([...this.jobs.values()]).then(()=>{});
    this.schedule();
  }
  public async onApplicationShutdown():Promise<void>{this.stopping=true;if(this.timer)clearTimeout(this.timer);await Promise.allSettled(this.jobs.values());}
  private async register(request:SurvivalMapRequest):Promise<string>{
    const key=survivalCandidateBucket(request);if(this.buckets.has(key))return key;
    if(this.buckets.size>=MAX_BUCKETS)throw new SurvivalMapError("NO_ELIGIBLE_MAP","Candidate bucket budget exceeded");
    const clean={group:request.group,phase:request.phase,minimumLevelId:request.minimumLevelId,excludedLevelIds:[]};
    this.buckets.set(key,{request:clean,ready:0,nextAt:this.now()+TTL_MS,failures:0,lastError:null,diagnosticKey:""});
    await this.db.query(`INSERT INTO sur_map_candidate_buckets(bucket_key,request) VALUES($1,$2::jsonb)
      ON CONFLICT(bucket_key) DO NOTHING`,[key,JSON.stringify(clean)]);return key;
  }
  private async capacity(tx:SqlExecutor,key:string):Promise<number>{
    await tx.query(`UPDATE sur_map_candidates SET status='COOLDOWN',retry_after=expires_at,invalid_reason='POOL_CAPACITY'
      WHERE bucket_key=$1 AND status='READY' AND level_id IN(SELECT level_id FROM sur_map_candidates
        WHERE bucket_key=$1 AND status='READY' AND expires_at>$2 ORDER BY verified_at,level_id OFFSET 3)`,[key,this.clock.now().toISOString()]);
    const row=(await tx.query<{count:string}>("SELECT count(*)::text AS count FROM sur_map_candidates WHERE bucket_key=$1 AND status='READY' AND expires_at>$2",[key,this.clock.now().toISOString()])).rows[0];
    return Number(row?.count??0);
  }
  private refill(key:string):void{
    if(this.stopping || this.jobs.has(key))return;
    const bucket=this.buckets.get(key)!;
    // One verifier for the process. No per-match/64-player worker fanout.
    let resolve!:()=>void;
    const job=new Promise<void>(done=>{resolve=done;});
    this.jobs.set(key,job);
    this.queued.set(key,async()=>{try{
      let probes=0;
      while(!this.stopping && bucket.ready<TARGET && probes++<12){
        try{
          const map=await this.source.select({...bucket.request,excludedLevelIds:[...this.known]});
          if(!this.valid(map,bucket.request))
            throw new SurvivalMapError("INVALID_DATA","Unverified pool candidate");
          const at=this.now();
          if(this.known.has(map.levelId))throw new SurvivalMapError("DUPLICATE_CANDIDATES","Verifier ignored candidate exclusions");
          const ready=await this.db.transaction(async tx=>{
          await tx.query("SELECT pg_advisory_xact_lock(hashtext('survival-candidate-reserve-v1'))");
          await tx.query(`INSERT INTO sur_map_candidates(bucket_key,level_id,metadata,status,verified_at,expires_at)
            VALUES($1,$2,$3::jsonb,'READY',$4,$5) ON CONFLICT(bucket_key,level_id) DO UPDATE
            SET metadata=EXCLUDED.metadata,status='READY',verified_at=EXCLUDED.verified_at,expires_at=EXCLUDED.expires_at,
              invalid_reason=NULL,retry_after=NULL WHERE sur_map_candidates.status<>'RESERVED' RETURNING level_id`,
            [key,map.levelId,JSON.stringify(map),new Date(at).toISOString(),new Date(at+TTL_MS).toISOString()]);
          if(bucket.diagnosticKey)await tx.query("UPDATE sur_map_candidate_buckets SET request=request-'lastFailure',updated_at=$2 WHERE bucket_key=$1",[key,this.clock.now().toISOString()]);
          return this.capacity(tx,key);});
          this.known.add(map.levelId);bucket.ready=ready;
          while(this.known.size>4096)this.known.delete(this.known.values().next().value!);
          // Bound disposable metadata; never prune match history or player files.
          await this.db.query(`DELETE FROM sur_map_candidates WHERE bucket_key=$1 AND status IN ('CONSUMED','EXPIRED','INVALID','COOLDOWN')
            AND level_id IN(SELECT level_id FROM sur_map_candidates WHERE bucket_key=$1 AND status IN ('CONSUMED','EXPIRED','INVALID','COOLDOWN')
              ORDER BY verified_at DESC OFFSET 64)`,[key]);
          bucket.failures=0;bucket.lastError=null;bucket.diagnosticKey="";bucket.nextAt=at+TTL_MS;
          this.logger.log(`candidate READY bucket=${key} level=${map.levelId} source=${map.sourceSearchType??"unknown"} page=${map.sourcePage??-1} band=${map.sourceIdBand??"unknown"} ready=${bucket.ready}`);
        }catch(error){
          bucket.failures++;bucket.lastError=error instanceof SurvivalMapError?error:new SurvivalMapError("API_ERROR",String(error));
          bucket.nextAt=this.now()+Math.max(error instanceof SurvivalMapError?error.retryAfterMs:2_000,Math.min(TTL_MS,30_000*2**Math.min(9,bucket.failures-1)));
          const cause=bucket.lastError;
          const diagnosticKey=JSON.stringify([cause.code,cause.http?.status??null,cause.http?.endpoint??null,cause.http?.edgeErrorCode??null]);
          if(bucket.diagnosticKey!==diagnosticKey){
            bucket.diagnosticKey=diagnosticKey;
            // Persist an actual failure transition once, never every retry or
            // changing retry delay. No token, full response body or GD data.
            const failure={code:cause.code,message:cause.message.slice(0,512),http:cause.http,
              observedAt:this.clock.now().toISOString(),retryAfter:new Date(bucket.nextAt).toISOString()};
            await this.db.query("UPDATE sur_map_candidate_buckets SET request=jsonb_set(request,'{lastFailure}',$2::jsonb),updated_at=$3 WHERE bucket_key=$1",[key,JSON.stringify(failure),this.clock.now().toISOString()])
              .catch(()=>this.logger.warn(`candidate failure diagnostics unavailable bucket=${key}`));
          }
          this.logger.warn(`candidate refill deferred bucket=${key} retryMs=${bucket.nextAt-this.now()} reason=${String(error)}`);break;
        }
      }
    }finally{this.jobs.delete(key);resolve();this.schedule();}});
    void this.drain();
  }
  private async drain():Promise<void>{
    if(this.running)return;this.running=true;
    try{while(this.queued.size){const key=this.priority && this.queued.has(this.priority)?this.priority:this.queued.keys().next().value!;
      this.priority=null;const work=this.queued.get(key)!;this.queued.delete(key);await work();}}
    finally{this.running=false;}
  }
  private schedule():void{
    if(this.timer)clearTimeout(this.timer);if(this.stopping)return;
    const due=Math.min(...[...this.buckets.values()].map(b=>b.nextAt));
    this.timer=setTimeout(()=>{this.timer=null;void this.refreshDue().catch(e=>this.logger.error(String(e)));},Math.max(1_000,due-this.now()));this.timer.unref();
  }
  private async refreshDue():Promise<void>{
    const now=this.now();
    const expired=[...this.buckets].filter(([,b])=>b.nextAt<=now && !this.jobs.has(survivalCandidateBucket(b.request)));
    // At a genuine certificate TTL boundary only; not on external API backoff.
    for(const [key,bucket]of expired){
      if(bucket.failures===0){
        const result=await this.db.query<{level_id:string;previous_status:string}>(`WITH stale AS (
          SELECT level_id,status FROM sur_map_candidates WHERE bucket_key=$1 AND
            ((status='READY' AND expires_at<=$2) OR (status IN ('INVALID','COOLDOWN') AND retry_after<=$2)) FOR UPDATE)
          UPDATE sur_map_candidates c SET status='EXPIRED' FROM stale s WHERE c.bucket_key=$1 AND c.level_id=s.level_id
          RETURNING c.level_id,s.status AS previous_status`,[key,this.clock.now().toISOString()]);
        bucket.ready=Math.max(0,bucket.ready-result.rows.filter(r=>r.previous_status==='READY').length);for(const r of result.rows)this.known.delete(Number(r.level_id));
      }
      bucket.nextAt=now+TTL_MS;if(bucket.ready<TARGET)this.refill(key);
    }
    this.schedule();
  }
  public async select(request:SurvivalMapRequest):Promise<SurvivalVerifiedMap>{
    const key=await this.register(request),reservation=request.reservation;
    if(!reservation)throw new SurvivalMapError("INVALID_METADATA","Candidate reservation context required");
    const take=()=>this.db.transaction(async tx=>{
      await tx.query("SELECT pg_advisory_xact_lock(hashtext('survival-candidate-reserve-v1'))");
      await tx.query(`UPDATE sur_map_candidates SET status='READY',reservation_token=NULL,lease_until=NULL WHERE status='RESERVED' AND lease_until<=$1`,[this.clock.now().toISOString()]);
      const row=(await tx.query<CandidateRow>(`SELECT c.* FROM sur_map_candidates c WHERE c.bucket_key=$1 AND c.status='READY' AND c.expires_at>$2
        AND NOT(c.level_id=ANY($3::bigint[]))
        AND NOT EXISTS(SELECT 1 FROM (SELECT level_id FROM sur_map_selection_history ORDER BY id DESC LIMIT 30) h WHERE h.level_id=c.level_id)
        AND NOT EXISTS(SELECT 1 FROM sur_map_candidates r WHERE r.level_id=c.level_id AND r.status='RESERVED')
        AND NOT EXISTS(SELECT 1 FROM sur_resource_quarantine q WHERE q.retry_after>$2 AND
          ((q.resource_kind='MAP' AND q.resource_id=c.level_id) OR (q.resource_kind='SONG' AND c.metadata->'songIds' @> to_jsonb(ARRAY[q.resource_id])) OR
           (q.resource_kind='SFX' AND c.metadata->'sfxIds' @> to_jsonb(ARRAY[q.resource_id]))))
        ORDER BY c.last_used_at NULLS FIRST,c.verified_at,c.level_id LIMIT 1 FOR UPDATE OF c`,[key,this.clock.now().toISOString(),request.excludedLevelIds])).rows[0];
      if(!row)return null;
      if(!this.valid(row.metadata,request) || row.metadata.levelId!==Number(row.level_id)){
        await tx.query("UPDATE sur_map_candidates SET status='INVALID',invalid_reason='CACHE_METADATA_INVALID' WHERE bucket_key=$1 AND level_id=$2",[key,row.level_id]);
        return null;
      }
      await tx.query(`UPDATE sur_map_candidates SET status='RESERVED',reservation_token=$3,match_id=$4,round_key=$5,map_generation=$6,lease_until=$7
        WHERE bucket_key=$1 AND level_id=$2`,[key,row.level_id,reservation.token,reservation.matchId,reservation.roundKey,reservation.mapGeneration,new Date(this.now()+120_000).toISOString()]);
      return row.metadata;
    });
    let map=await take();
    if(!map){this.buckets.get(key)!.ready=0;this.priority=key;this.refill(key);await this.jobs.get(key);map=await take();}
    if(!map){const error=this.buckets.get(key)!.lastError;
      throw new SurvivalMapError(error?.code??"NO_ELIGIBLE_MAP",`No READY verified candidate; recent thirty remain excluded${error?": "+error.message:""}`,error?.failures??[],error?.http??null,Math.max(30_000,error?.retryAfterMs??0));}
    const bucket=this.buckets.get(key)!;bucket.ready=Math.max(0,bucket.ready-1);if(bucket.ready<2)this.refill(key);
    return map;
  }
  public async confirm(tx:SqlExecutor,request:SurvivalMapRequest,map:SurvivalVerifiedMap):Promise<void>{
    const r=request.reservation!;
    const result=await tx.query(`UPDATE sur_map_candidates SET status='CONSUMED',last_used_at=$4,reservation_token=NULL,lease_until=NULL
      WHERE bucket_key=$1 AND level_id=$2 AND status='RESERVED' AND reservation_token=$3 RETURNING level_id`,
      [survivalCandidateBucket(request),map.levelId,r.token,this.clock.now().toISOString()]);
    if(!result.rows.length)throw new SurvivalMapError("DUPLICATE_CANDIDATES","Candidate reservation expired or changed");
  }
  public async discardPrefetch(tx:SqlExecutor,matchId:string,roundKey:string,generation:number,levelId:number):Promise<void>{
    await tx.query(`UPDATE sur_map_candidates SET status='COOLDOWN',invalid_reason='PREFETCH_PHASE_CHANGED',retry_after=expires_at
      WHERE match_id=$1 AND round_key=$2 AND map_generation=$3 AND level_id=$4 AND status='CONSUMED'`,[matchId,roundKey,generation,levelId]);
  }
  public async release(request:SurvivalMapRequest):Promise<void>{
    if(!request.reservation)return;
    await this.db.transaction(async tx=>{
      await tx.query("SELECT pg_advisory_xact_lock(hashtext('survival-candidate-reserve-v1'))");
      const rows=(await tx.query<{bucket_key:string}>(`UPDATE sur_map_candidates SET status='READY',reservation_token=NULL,lease_until=NULL
        WHERE reservation_token=$1 AND status='RESERVED' RETURNING bucket_key`,[request.reservation!.token])).rows;
      for(const row of rows){const bucket=this.buckets.get(row.bucket_key);const ready=await this.capacity(tx,row.bucket_key);if(bucket)bucket.ready=ready;}
    });
  }
}
