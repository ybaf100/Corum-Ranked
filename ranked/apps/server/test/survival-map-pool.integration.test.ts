import "reflect-metadata";
import {readFile,readdir} from "node:fs/promises";
import {fileURLToPath} from "node:url";
import {PGlite} from "@electric-sql/pglite";
import {beforeAll,afterAll,beforeEach,describe,it,expect,vi} from "vitest";
import {survivalMapRange,type SurvivalVerifiedMap} from "@corum-ranked/rules";
import {PgliteDatabase} from "./pglite-database.js";
import {SurvivalMapPool,survivalCandidateBucket} from "../src/survival/survival-map-pool.js";
import {SurvivalMapError,type SurvivalMapRequest} from "../src/survival/survival-map.provider.js";
let pg:PGlite,db:PgliteDatabase,now:number,sequence:number;
const pools:SurvivalMapPool[]=[];
const request:SurvivalMapRequest={group:"RED_BRONZE",phase:"ROUND_1",minimumLevelId:40000000,excludedLevelIds:[],
 reservation:{matchId:"00000000-0000-4000-8000-000000000001",roundKey:"stage-1",mapGeneration:1,token:"00000000-0000-4000-8000-000000000002"}};
const clock={now:()=>new Date(now)};
const source={select:vi.fn(async(r:SurvivalMapRequest):Promise<SurvivalVerifiedMap>=>{
 const range=survivalMapRange(r.group,r.phase);return {levelId:++sequence,levelVersion:1,stars:range.minimumStars??10,
 demon:range.minimumStars===null,demonDifficulty:range.minimumStars===null?range.demons[0]!:null,automatic:false,platformer:false,
 geometrySha256:"a".repeat(64),geometryBytes:32,verifiedAt:clock.now().toISOString(),songIds:[123],sfxIds:[],sourceSearchType:"2",sourcePage:123};
})};
const defaultSelect=source.select.getMockImplementation()!;
const pool=()=>{const p=new SurvivalMapPool(db,source,clock);pools.push(p);return p;};
async function idle(p:SurvivalMapPool){await Promise.all((p as unknown as {jobs:Map<string,Promise<void>>}).jobs.values());}
async function count(status:string){return Number((await db.query<{count:string}>("SELECT count(*)::text AS count FROM sur_map_candidates WHERE status=$1",[status])).rows[0]!.count);}
beforeAll(async()=>{pg=new PGlite();db=new PgliteDatabase(pg);const directory=fileURLToPath(new URL("../../../migrations/",import.meta.url));
 for(const f of (await readdir(directory)).filter(n=>n.endsWith(".sql")).sort())await pg.exec(await readFile(`${directory}/${f}`,"utf8"));},60000);
afterAll(async()=>{for(const p of pools)await p.onApplicationShutdown();await pg.close();});
beforeEach(async()=>{for(const p of pools)await p.onApplicationShutdown();pools.length=0;
 await db.query("TRUNCATE sur_map_candidates,sur_map_candidate_buckets,sur_resource_quarantine");now=Date.parse("2026-10-28T08:00:00Z");sequence=50000000;source.select.mockReset().mockImplementation(defaultSelect);});

describe("durable verified Survival candidate pool",()=>{
 it("warms three per exact eligibility bucket, aliases identical phases/formats, and performs no idle SQL polling",async()=>{
   const p=pool();await p.onApplicationBootstrap();await idle(p);
   const rows=(await db.query<{bucket_key:string;count:string}>("SELECT bucket_key,count(*)::text AS count FROM sur_map_candidates WHERE status='READY' GROUP BY bucket_key")).rows;
   expect(rows.length).toBeGreaterThan(6);expect(rows.every(r=>Number(r.count)===3)).toBe(true);
   expect(survivalCandidateBucket({...request,phase:"ROUND_3"})).toBe(survivalCandidateBucket({...request,phase:"ROUND_4"}));
   expect(survivalCandidateBucket(request)).not.toBe(survivalCandidateBucket({...request,minimumLevelId:60000000}));
   const query=vi.spyOn(db,"query"),fetches=source.select.mock.calls.length;
   await new Promise(resolve=>setTimeout(resolve,50));expect(query).not.toHaveBeenCalled();expect(source.select).toHaveBeenCalledTimes(fetches);query.mockRestore();
 });
 it("reserves atomically across concurrent matches and confirms only the exact fenced token",async()=>{
   const p=pool();await p.onApplicationBootstrap();await idle(p);
   const other={...request,reservation:{...request.reservation!,matchId:"00000000-0000-4000-8000-000000000003",token:"00000000-0000-4000-8000-000000000004"}};
   const [a,b]=await Promise.all([p.select(request),p.select(other)]);expect(a.levelId).not.toBe(b.levelId);
   expect(await count("RESERVED")).toBe(2);
   await expect(db.transaction(tx=>p.confirm(tx,other,a))).rejects.toThrow("reservation expired or changed");
   await db.transaction(tx=>p.confirm(tx,request,a));await p.release(other);expect(await count("RESERVED")).toBe(0);expect(await count("CONSUMED")).toBe(1);
 });
 it("bounds READY capacity after repeated cancellation/release races with background refill",async()=>{
   const p=pool();await p.onApplicationBootstrap();await idle(p);const key=survivalCandidateBucket(request);
   for(let i=0;i<8;i++){
     const other={...request,reservation:{...request.reservation!,token:"00000000-0000-4000-8000-000000000004"}};
     await p.select(request);await p.select(other);
     await Promise.all([p.release(request),p.release(other)]);await idle(p);
     const count=Number((await db.query<{count:string}>("SELECT count(*)::text AS count FROM sur_map_candidates WHERE bucket_key=$1 AND status='READY'",[key])).rows[0]!.count);
     expect(count).toBeLessThanOrEqual(3);expect(count).toBeGreaterThanOrEqual(2);
   }
 });
 it("rejects a corrupt persisted certificate and continues with verified candidates",async()=>{
   const p=pool();await p.onApplicationBootstrap();await idle(p);const key=survivalCandidateBucket(request);
   const row=(await db.query<{level_id:string}>("SELECT level_id FROM sur_map_candidates WHERE bucket_key=$1 ORDER BY level_id LIMIT 1",[key])).rows[0]!;
   await db.query("UPDATE sur_map_candidates SET metadata=jsonb_set(metadata,'{geometrySha256}','\"broken\"') WHERE bucket_key=$1 AND level_id=$2",[key,row.level_id]);
   const map=await p.select(request);expect(map.levelId).not.toBe(Number(row.level_id));expect(map.geometrySha256).toMatch(/^[a-f0-9]{64}$/);
   expect((await db.query("SELECT * FROM sur_map_candidates WHERE bucket_key=$1 AND level_id=$2 AND status='INVALID'",[key,row.level_id])).rows).toHaveLength(1);
 });
 it("serves durable verified candidates after process restart during an upstream outage",async()=>{
   const a=pool();await a.onApplicationBootstrap();await idle(a);await a.onApplicationShutdown();
   source.select.mockRejectedValue(new SurvivalMapError("API_ERROR","upstream unavailable",[],null,30000));
   const b=pool();await b.onApplicationBootstrap();await idle(b);
   const map=await b.select(request);expect(map.geometrySha256).toBe("a".repeat(64));expect(map.sourcePage).toBe(123);
 });
 it("excludes caller IDs, expired certificates, and quarantined songs without relaxing constraints",async()=>{
   const p=pool();await p.onApplicationBootstrap();await idle(p);
   const first=await p.select(request);await p.release(request);
   const second=await p.select({...request,excludedLevelIds:[first.levelId]});expect(second.levelId).not.toBe(first.levelId);await p.release(request);
   await db.query("INSERT INTO sur_resource_quarantine(resource_kind,resource_id,reason,verified_at,retry_after) VALUES('SONG',123,'test',$1,$2)",[clock.now().toISOString(),new Date(now+3600000).toISOString()]);
   source.select.mockRejectedValue(new SurvivalMapError("API_ERROR","offline"));
   await expect(p.select(request)).rejects.toMatchObject({code:"API_ERROR"});expect(await count("RESERVED")).toBe(0);
   now+=7*3600000;await expect(p.select(request)).rejects.toMatchObject({code:"API_ERROR"});
 });
 it("releases only expired leases and blocks duplicate confirmation",async()=>{
   const p=pool();await p.onApplicationBootstrap();await idle(p);
   const a=await p.select(request);await db.transaction(tx=>p.confirm(tx,request,a));
   await expect(db.transaction(tx=>p.confirm(tx,request,a))).rejects.toThrow();
   const b=await p.select(request);now+=120001;
   const c=await p.select({...request,reservation:{...request.reservation!,token:"00000000-0000-4000-8000-000000000009"}});
   expect(c.levelId).toBe(b.levelId);await expect(db.transaction(tx=>p.confirm(tx,request,b))).rejects.toThrow();
 });
 it("bounds failure retries and does not query PostgreSQL on external backoff",async()=>{
   source.select.mockRejectedValue(new SurvivalMapError("API_ERROR","HTTP 429",[],null,120000));
   const p=pool();await p.onApplicationBootstrap();await idle(p);expect(await count("READY")).toBe(0);
   const failures=(await db.query<{failure:{code:string}}>("SELECT request->'lastFailure' AS failure FROM sur_map_candidate_buckets")).rows;
   expect(failures.length).toBeGreaterThan(0);expect(failures.every(r=>r.failure.code==='API_ERROR')).toBe(true);
   const query=vi.spyOn(db,"query");now+=120001;
   await (p as unknown as {refreshDue():Promise<void>}).refreshDue();await idle(p);
   expect(query).not.toHaveBeenCalled();query.mockRestore();
 });
 it("quarantines only a consumed prefetch whose full match/round/generation identity changed",async()=>{
   const p=pool();await p.onApplicationBootstrap();await idle(p);const selected=await p.select(request);
   await db.transaction(tx=>p.confirm(tx,request,selected));
   await db.transaction(tx=>p.discardPrefetch(tx,"00000000-0000-4000-8000-000000000099",request.reservation!.roundKey,1,selected.levelId));
   expect(await count("CONSUMED")).toBe(1);
   await db.transaction(tx=>p.discardPrefetch(tx,request.reservation!.matchId,request.reservation!.roundKey,2,selected.levelId));
   expect(await count("CONSUMED")).toBe(1);
   await db.transaction(tx=>p.discardPrefetch(tx,request.reservation!.matchId,request.reservation!.roundKey,1,selected.levelId));
   expect(await count("CONSUMED")).toBe(0);expect(await count("COOLDOWN")).toBe(1);
   const row=(await db.query<{invalid_reason:string;retry_after:Date;expires_at:Date}>("SELECT invalid_reason,retry_after,expires_at FROM sur_map_candidates WHERE level_id=$1",[selected.levelId])).rows[0]!;
   expect(row.invalid_reason).toBe("PREFETCH_PHASE_CHANGED");expect(row.retry_after).toEqual(row.expires_at);
 });
 it("recovers when the bucket parent rows vanish under a running process (Admin reset-all)",async()=>{
   // beta.122 iOS log: every Survival match failed with
   // sur_map_candidates_bucket_key_fkey after the cache parents were truncated
   // while the pool kept its in-memory bucket keys.
   const p=pool();await p.onApplicationBootstrap();await idle(p);
   await db.query("TRUNCATE sur_map_candidates,sur_map_candidate_buckets");
   const map=await p.select(request);expect(map.geometrySha256).toMatch(/^[a-f0-9]{64}$/);
   expect((await db.query("SELECT 1 FROM sur_map_candidate_buckets WHERE bucket_key=$1",[survivalCandidateBucket(request)])).rows).toHaveLength(1);
   await db.transaction(tx=>p.confirm(tx,request,map));expect(await count("CONSUMED")).toBe(1);
 });
 it("does not keep a bucket key in memory when its first parent INSERT fails",async()=>{
   const p=pool();await p.onApplicationBootstrap();await idle(p);
   const fresh={...request,minimumLevelId:70000000};const original=db.query.bind(db);let failed=false;
   const spy=vi.spyOn(db,"query").mockImplementation(((sql:string,params?:unknown[])=>{
     if(!failed && sql.includes("INSERT INTO sur_map_candidate_buckets")){failed=true;return Promise.reject(new Error("transient connection reset"));}
     return original(sql,params);}) as typeof db.query);
   await expect(p.select(fresh)).rejects.toThrow("transient connection reset");spy.mockRestore();sequence=70000000;
   const map=await p.select(fresh);expect(map.levelId).toBeGreaterThanOrEqual(70000000);
 });

});
