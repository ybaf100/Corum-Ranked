import {randomInt} from "node:crypto";
import {Logger,type OnApplicationBootstrap,type OnApplicationShutdown} from "@nestjs/common";
import {survivalMapAllowed,survivalMapRange,type SurvivalVerifiedMap,type SurvivalDifficultyGroup,type SurvivalMapPhase} from "@corum-ranked/rules";
import type {DatabasePort,SqlExecutor} from "../database/database.port.js";
import type {ServerClock} from "../common/runtime.module.js";
import {SurvivalMapError,type SurvivalMapProvider,type SurvivalMapRequest} from "./survival-map.provider.js";

const TTL_MS=6*60*60_000,TARGET=3,ANTICIPATED_TARGET=5,ANTICIPATION_MS=20*60_000,MAX_ANTICIPATED=6,MAX_BUCKETS=32,RECENT_WINDOW=30;
/** Unbiased index in [0,n). Tests inject a deterministic picker. */
export type SurvivalCandidatePicker=(count:number)=>number;
const cryptoPicker:SurvivalCandidatePicker=count=>randomInt(count);
/** Coarse ID strata used only to measure catalog skew; never a quota. */
export function survivalIdBandOf(levelId:number):string {return levelId<70_000_000?"40m-70m":levelId<100_000_000?"70m-100m":"100m+";}
export function survivalCandidateBucket(request:SurvivalMapRequest):string {
  const range=survivalMapRange(request.group,request.phase);
  // 16/64 and R3/R4 share a bucket only when every eligibility predicate agrees.
  return JSON.stringify([Math.max(40_000_000,request.minimumLevelId),range.minimumStars,range.maximumStars,range.demons]);
}
interface CandidateRow {bucket_key:string;level_id:string;metadata:SurvivalVerifiedMap;expires_at:Date|string;}
interface Bucket {request:SurvivalMapRequest;ready:number;nextAt:number;failures:number;lastError:SurvivalMapError|null;diagnosticKey:string;anticipatedUntil:number;}
/** Durable certificates, not geometry blobs. Event-driven work has no SQL idle
 * poll. Backoff performs upstream work only; DB is written on actual progress. */
export class SurvivalMapPool implements SurvivalMapProvider,OnApplicationBootstrap,OnApplicationShutdown {
  private readonly logger=new Logger(SurvivalMapPool.name);
  private readonly buckets=new Map<string,Bucket>();
  // Exclusions handed to the verifier: only candidates that are currently
  // READY/RESERVED plus the global recent-30 selection window. Consumed,
  // expired or cooled-down maps leave this set, so an old map is excluded only
  // while it is actually in the recent window (the former ever-growing cache
  // excluded every map ever verified until 4096 entries).
  private readonly held=new Set<number>();
  private recent:number[]=[];
  private readonly selectedBands=new Map<string,number>();
  private readonly jobs=new Map<string,Promise<void>>();
  private readonly queued=new Map<string,()=>Promise<void>>();
  private running=false;
  private priority:string|null=null;
  private timer:ReturnType<typeof setTimeout>|null=null;
  private stopping=false;
  private initialFill:Promise<void>=Promise.resolve();
  public waitForInitialFill():Promise<void>{return this.initialFill;}
  public constructor(private readonly db:DatabasePort,private readonly source:SurvivalMapProvider,
    private readonly clock:ServerClock,private readonly minimumLevelId:number|(()=>number)=40_000_000,
    private readonly pick:SurvivalCandidatePicker=cryptoPicker){}
  private now(){return this.clock.now().getTime();}
  private exclusions():number[]{return [...new Set([...this.held,...this.recent])];}
  private target(bucket:Bucket):number{return bucket.anticipatedUntil>this.now()?ANTICIPATED_TARGET:TARGET;}
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
    const recent=await this.db.query<{level_id:string}>(`SELECT level_id FROM sur_map_selection_history ORDER BY id DESC LIMIT ${RECENT_WINDOW}`);
    this.recent=recent.rows.map(row=>Number(row.level_id)).reverse();
    const rows=(await this.db.query<CandidateRow>("SELECT bucket_key,level_id,metadata,expires_at FROM sur_map_candidates WHERE status='READY' AND expires_at>$1",[this.clock.now().toISOString()])).rows;
    for(const row of rows){this.held.add(Number(row.level_id));const bucket=this.buckets.get(row.bucket_key);if(bucket){bucket.ready++;bucket.nextAt=Math.min(bucket.nextAt,new Date(row.expires_at).getTime());}}
    for(const row of (await this.db.query<{level_id:string}>("SELECT level_id FROM sur_map_candidates WHERE status='RESERVED'")).rows)this.held.add(Number(row.level_id));
    for(const [key,bucket]of this.buckets)if(bucket.ready<TARGET)this.refill(key);
    this.initialFill=Promise.all([...this.jobs.values()]).then(()=>{});
    this.schedule();
  }
  public async onApplicationShutdown():Promise<void>{this.stopping=true;if(this.timer)clearTimeout(this.timer);await Promise.allSettled(this.jobs.values());}
  private async register(request:SurvivalMapRequest):Promise<string>{
    const key=survivalCandidateBucket(request);if(this.buckets.has(key))return key;
    if(this.buckets.size>=MAX_BUCKETS)throw new SurvivalMapError("NO_ELIGIBLE_MAP","Candidate bucket budget exceeded");
    const clean={group:request.group,phase:request.phase,minimumLevelId:request.minimumLevelId,excludedLevelIds:[]};
    // Persist before publishing the in-memory bucket: a failed INSERT must not
    // leave a key that every later refill trusts but PostgreSQL never stored.
    await this.ensureBucketRow(this.db,key,clean);
    if(!this.buckets.has(key))this.buckets.set(key,{request:clean,ready:0,nextAt:this.now()+TTL_MS,failures:0,lastError:null,diagnosticKey:"",anticipatedUntil:0});
    return key;
  }
  /** The bucket row is a disposable cache parent (Admin reset-all truncates
   * every sur_* table while this process keeps running). Every candidate
   * write re-asserts it in the same transaction, so a vanished parent row can
   * never turn into a permanent sur_map_candidates_bucket_key_fkey failure. */
  private async ensureBucketRow(executor:SqlExecutor,key:string,request:SurvivalMapRequest):Promise<void>{
    await executor.query(`INSERT INTO sur_map_candidate_buckets(bucket_key,request) VALUES($1,$2::jsonb)
      ON CONFLICT(bucket_key) DO NOTHING`,[key,JSON.stringify(request)]);
  }
  private async capacity(tx:SqlExecutor,key:string):Promise<number>{
    const bucket=this.buckets.get(key),limit=bucket?this.target(bucket):TARGET;
    const cooled=await tx.query<{level_id:string}>(`UPDATE sur_map_candidates SET status='COOLDOWN',retry_after=expires_at,invalid_reason='POOL_CAPACITY'
      WHERE bucket_key=$1 AND status='READY' AND level_id IN(SELECT level_id FROM sur_map_candidates
        WHERE bucket_key=$1 AND status='READY' AND expires_at>$2 ORDER BY verified_at,level_id OFFSET $3) RETURNING level_id`,[key,this.clock.now().toISOString(),limit]);
    for(const row of cooled.rows)this.held.delete(Number(row.level_id));
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
      while(!this.stopping && bucket.ready<this.target(bucket) && probes++<12){
        try{
          const excluded=this.exclusions();
          const map=await this.source.select({...bucket.request,excludedLevelIds:excluded});
          if(!this.valid(map,bucket.request))
            throw new SurvivalMapError("INVALID_DATA","Unverified pool candidate");
          const at=this.now();
          if(excluded.includes(map.levelId))throw new SurvivalMapError("DUPLICATE_CANDIDATES","Verifier ignored candidate exclusions");
          const ready=await this.db.transaction(async tx=>{
          await tx.query("SELECT pg_advisory_xact_lock(hashtext('survival-candidate-reserve-v1'))");
          await this.ensureBucketRow(tx,key,bucket.request);
          await tx.query(`INSERT INTO sur_map_candidates(bucket_key,level_id,metadata,status,verified_at,expires_at)
            VALUES($1,$2,$3::jsonb,'READY',$4,$5) ON CONFLICT(bucket_key,level_id) DO UPDATE
            SET metadata=EXCLUDED.metadata,status='READY',verified_at=EXCLUDED.verified_at,expires_at=EXCLUDED.expires_at,
              invalid_reason=NULL,retry_after=NULL WHERE sur_map_candidates.status<>'RESERVED' RETURNING level_id`,
            [key,map.levelId,JSON.stringify(map),new Date(at).toISOString(),new Date(at+TTL_MS).toISOString()]);
          if(bucket.diagnosticKey)await tx.query("UPDATE sur_map_candidate_buckets SET request=request-'lastFailure',updated_at=$2 WHERE bucket_key=$1",[key,this.clock.now().toISOString()]);
          return this.capacity(tx,key);});
          this.held.add(map.levelId);bucket.ready=ready;
          // Bound disposable metadata; never prune match history or player files.
          await this.db.query(`DELETE FROM sur_map_candidates WHERE bucket_key=$1 AND status IN ('CONSUMED','EXPIRED','INVALID','COOLDOWN')
            AND level_id IN(SELECT level_id FROM sur_map_candidates WHERE bucket_key=$1 AND status IN ('CONSUMED','EXPIRED','INVALID','COOLDOWN')
              ORDER BY verified_at DESC OFFSET 64)`,[key]);
          bucket.failures=0;bucket.lastError=null;bucket.diagnosticKey="";bucket.nextAt=at+TTL_MS;
          this.logger.log(`candidate READY bucket=${key} level=${map.levelId} source=${map.sourceSearchType??"unknown"} page=${map.sourcePage??-1} band=${map.sourceIdBand??survivalIdBandOf(map.levelId)} ready=${bucket.ready}/${this.target(bucket)}`);
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
    // An empty bucket a match is waiting on first, then anticipated buckets of
    // running matches, then idle warming. One verifier for the whole process.
    try{while(this.queued.size){const anticipated=[...this.queued.keys()].find(k=>(this.buckets.get(k)?.anticipatedUntil??0)>this.now());
      const key=this.priority && this.queued.has(this.priority)?this.priority:anticipated??this.queued.keys().next().value!;
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
        bucket.ready=Math.max(0,bucket.ready-result.rows.filter(r=>r.previous_status==='READY').length);for(const r of result.rows)this.held.delete(Number(r.level_id));
      }
      bucket.nextAt=now+TTL_MS;if(bucket.ready<this.target(bucket))this.refill(key);
    }
    this.schedule();
  }
  public async select(request:SurvivalMapRequest):Promise<SurvivalVerifiedMap>{
    const key=await this.register(request),reservation=request.reservation;
    if(!reservation)throw new SurvivalMapError("INVALID_METADATA","Candidate reservation context required");
    const take=()=>this.db.transaction(async tx=>{
      await tx.query("SELECT pg_advisory_xact_lock(hashtext('survival-candidate-reserve-v1'))");
      await tx.query(`UPDATE sur_map_candidates SET status='READY',reservation_token=NULL,lease_until=NULL WHERE status='RESERVED' AND lease_until<=$1`,[this.clock.now().toISOString()]);
      const rows=[...(await tx.query<CandidateRow>(`SELECT c.* FROM sur_map_candidates c WHERE c.bucket_key=$1 AND c.status='READY' AND c.expires_at>$2
        AND NOT(c.level_id=ANY($3::bigint[]))
        AND NOT EXISTS(SELECT 1 FROM (SELECT level_id FROM sur_map_selection_history ORDER BY id DESC LIMIT ${RECENT_WINDOW}) h WHERE h.level_id=c.level_id)
        AND NOT EXISTS(SELECT 1 FROM sur_map_candidates r WHERE r.level_id=c.level_id AND r.status='RESERVED')
        AND NOT EXISTS(SELECT 1 FROM sur_resource_quarantine q WHERE q.retry_after>$2 AND
          ((q.resource_kind='MAP' AND q.resource_id=c.level_id) OR (q.resource_kind='SONG' AND c.metadata->'songIds' @> to_jsonb(ARRAY[q.resource_id])) OR
           (q.resource_kind='SFX' AND c.metadata->'sfxIds' @> to_jsonb(ARRAY[q.resource_id]))))
        ORDER BY c.level_id FOR UPDATE OF c`,[key,this.clock.now().toISOString(),request.excludedLevelIds])).rows];
      // Uniform draw: each of the N eligible READY candidates has probability
      // 1/N (crypto.randomInt). A failed certificate is invalidated and the
      // draw repeats among the remaining eligible candidates only. The global
      // advisory lock keeps lookup + draw + reservation atomic across matches.
      while(rows.length){
        const index=this.pick(rows.length);
        if(!Number.isSafeInteger(index) || index<0 || index>=rows.length)throw new SurvivalMapError("INVALID_METADATA","Candidate picker out of range");
        const [row]=rows.splice(index,1);
        if(!this.valid(row!.metadata,request) || row!.metadata.levelId!==Number(row!.level_id)){
          await tx.query("UPDATE sur_map_candidates SET status='INVALID',invalid_reason='CACHE_METADATA_INVALID' WHERE bucket_key=$1 AND level_id=$2",[key,row!.level_id]);
          this.held.delete(Number(row!.level_id));continue;
        }
        await tx.query(`UPDATE sur_map_candidates SET status='RESERVED',reservation_token=$3,match_id=$4,round_key=$5,map_generation=$6,lease_until=$7
          WHERE bucket_key=$1 AND level_id=$2`,[key,row!.level_id,reservation.token,reservation.matchId,reservation.roundKey,reservation.mapGeneration,new Date(this.now()+120_000).toISOString()]);
        const band=survivalIdBandOf(row!.metadata.levelId);this.selectedBands.set(band,(this.selectedBands.get(band)??0)+1);
        this.logger.log(`candidate DRAWN bucket=${key} level=${row!.metadata.levelId} band=${band} eligible=${rows.length+1} selectedBands=${JSON.stringify(Object.fromEntries(this.selectedBands))}`);
        return row!.metadata;
      }
      return null;
    });
    let map=await take();
    if(!map){this.buckets.get(key)!.ready=0;this.priority=key;this.refill(key);await this.jobs.get(key);map=await take();}
    if(!map){const error=this.buckets.get(key)!.lastError;
      throw new SurvivalMapError(error?.code??"NO_ELIGIBLE_MAP",`No READY verified candidate; recent thirty remain excluded${error?": "+error.message:""}`,error?.failures??[],error?.http??null,Math.max(30_000,error?.retryAfterMs??0));}
    const bucket=this.buckets.get(key)!;bucket.ready=Math.max(0,bucket.ready-1);if(bucket.ready<Math.min(2,this.target(bucket)) || bucket.ready<this.target(bucket)-1)this.refill(key);
    return map;
  }
  /** A running match will likely need this bucket next: keep up to five READY
   * candidates there for a while (bounded number of buckets), refilled ahead
   * of idle warming. Other buckets keep the default three. */
  public async anticipate(request:Omit<SurvivalMapRequest,"reservation"|"excludedLevelIds">):Promise<void>{
    const key=await this.register({...request,excludedLevelIds:[]}),bucket=this.buckets.get(key)!,now=this.now();
    bucket.anticipatedUntil=now+ANTICIPATION_MS;
    const active=[...this.buckets.values()].filter(b=>b.anticipatedUntil>now).sort((a,b)=>b.anticipatedUntil-a.anticipatedUntil);
    for(const stale of active.slice(MAX_ANTICIPATED))stale.anticipatedUntil=0;
    if(bucket.ready<this.target(bucket))this.refill(key);
  }
  public async confirm(tx:SqlExecutor,request:SurvivalMapRequest,map:SurvivalVerifiedMap):Promise<void>{
    const r=request.reservation!;
    const result=await tx.query(`UPDATE sur_map_candidates SET status='CONSUMED',last_used_at=$4,reservation_token=NULL,lease_until=NULL
      WHERE bucket_key=$1 AND level_id=$2 AND status='RESERVED' AND reservation_token=$3 RETURNING level_id`,
      [survivalCandidateBucket(request),map.levelId,r.token,this.clock.now().toISOString()]);
    if(!result.rows.length)throw new SurvivalMapError("DUPLICATE_CANDIDATES","Candidate reservation expired or changed");
    // Leaves the held set; it is excluded only while inside the recent window.
    this.held.delete(map.levelId);this.recent=[...this.recent.filter(id=>id!==map.levelId),map.levelId].slice(-RECENT_WINDOW);
  }
  public async discardPrefetch(tx:SqlExecutor,matchId:string,roundKey:string,generation:number,levelId:number):Promise<void>{
    this.held.delete(levelId);
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
