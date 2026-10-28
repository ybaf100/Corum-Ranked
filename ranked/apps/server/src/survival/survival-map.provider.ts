import { createHash } from "node:crypto";
import { unzipSync } from "node:zlib";
import { Inject, Injectable } from "@nestjs/common";
import { survivalDemonFromNativeCode, survivalMapAllowed, survivalMapRange, shuffle,
  type RandomSource, type SurvivalDifficultyGroup, type SurvivalMapPhase,
  type SurvivalMapMetadata, type SurvivalVerifiedMap } from "@corum-ranked/rules";
import type {SqlExecutor} from "../database/database.port.js";
import { RANDOM_SOURCE } from "../common/runtime.module.js";
import { SurvivalGdRelayError } from "./survival-gd-transport.js";

export const SURVIVAL_MAP_PROVIDER = Symbol("SURVIVAL_MAP_PROVIDER");
export interface SurvivalMapRequest {
  group: SurvivalDifficultyGroup;
  phase: SurvivalMapPhase;
  minimumLevelId: number;
  excludedLevelIds: readonly number[];
  reservation?: {matchId:string;roundKey:string;mapGeneration:number;token:string};
}
export interface SurvivalMapProvider {
  select(request: SurvivalMapRequest): Promise<SurvivalVerifiedMap>;
  confirm?(tx:SqlExecutor,request:SurvivalMapRequest,map:SurvivalVerifiedMap):Promise<void>;
  release?(request:SurvivalMapRequest):Promise<void>;
}
export type SurvivalMapErrorCode = "API_ERROR" | "EMPTY_SEARCH" | "INVALID_METADATA" |
  "DOWNLOAD_FAILED" | "INVALID_DATA" | "DUPLICATE_CANDIDATES" | "NO_ELIGIBLE_MAP";
export interface SurvivalMapHttpDiagnostics {
  endpoint:string;
  status:number;
  server:string|null;
  contentType:string|null;
  cfRay:string|null;
  cfMitigated:string|null;
  retryAfter:string|null;
  title:string|null;
  edgeErrorCode:string|null;
  textSummary:string|null;
  bodyKind:"CLOUDFLARE_CHALLENGE"|"CLOUDFLARE_BLOCK"|"HTML"|"TEXT"|"EMPTY";
}
export class SurvivalMapError extends Error {
  public constructor(public readonly code: SurvivalMapErrorCode, message: string,
    public readonly failures: readonly SurvivalMapErrorCode[] = [],
    public readonly http:SurvivalMapHttpDiagnostics|null=null,
    public readonly retryAfterMs=2_000) {super(message); this.name="SurvivalMapError";}
}
const HTTP_BLOCK_RETRY_MS=30_000,MAX_HTTP_RETRY_MS=120_000;
function gdRetryDelay(response:Response):number {
  const value=response.headers.get("retry-after");
  if(value) {
    const delay=/^\d+$/.test(value)?Number(value)*1000:Date.parse(value)-Date.now();
    if(Number.isFinite(delay))return Math.min(MAX_HTTP_RETRY_MS,Math.max(2_000,delay));
  }
  return response.status===403 || response.status===429?HTTP_BLOCK_RETRY_MS:2_000;
}
async function gdHttpDiagnostics(endpoint:string,response:Response):Promise<SurvivalMapHttpDiagnostics> {
  // Read only a small error-page prefix, and publish only an allowlist of
  // transport fields/title and a short public Cloudflare plaintext error.
  // Never retain cookies, authorization or full error pages.
  let sample="";
  const reader=response.body?.getReader();
  if(reader) {
    const parts:Uint8Array[]=[];let size=0;
    try {
      while(size<4_096) {
        const {value,done}=await reader.read();if(done)break;
        const part=value.subarray(0,4_096-size);parts.push(part);size+=part.length;
      }
    }catch{}finally{await reader.cancel().catch(()=>{});}
    sample=Buffer.concat(parts).toString("utf8");
  }
  const clean=(value:string)=>value.replace(/<[^>]*>/g,"").replace(/[\u0000-\u001f\u007f]/g," ").replace(/\s+/g," ").trim().slice(0,160);
  const header=(name:string)=>{const value=response.headers.get(name);return value?clean(value):null;};
  const cfMitigated=header("cf-mitigated");
  const contentType=header("content-type");
  const server=header("server");
  const title=sample.match(/<title\b[^>]*>([\s\S]*?)<\/title>/i)?.[1];
  const challenge=cfMitigated==="challenge" || /\/cdn-cgi\/challenge-platform\//i.test(sample);
  const cloudflare=server?.toLowerCase()==="cloudflare" || /cloudflare/i.test(sample);
  const edgeErrorCode=cloudflare?(sample.match(/\berror(?:\s+code)?\s*:?\s*(1\d{3})\b/i)?.[1]??null):null;
  const blocked=cloudflare && (edgeErrorCode!==null || /attention required|access denied|sorry, you have been blocked/i.test(sample));
  const html=(contentType?.includes("html")??false) || /<(?:!doctype|html|title)\b/i.test(sample);
  return {endpoint,status:response.status,server,contentType,
    cfRay:header("cf-ray"),cfMitigated,retryAfter:header("retry-after"),title:title?clean(title):null,
    edgeErrorCode,textSummary:cloudflare && !html && !challenge && sample?clean(sample):null,
    bodyKind:challenge?"CLOUDFLARE_CHALLENGE":blocked?"CLOUDFLARE_BLOCK":html?"HTML":sample?"TEXT":"EMPTY"};
}
function fields(record: string): Map<string,string> {
  const entries=record.split(":"), result=new Map<string,string>();
  if (entries.length%2) throw new SurvivalMapError("INVALID_METADATA","Odd GD field count");
  for (let i=0;i<entries.length;i+=2) {
    const key=entries[i]!;
    if (!/^\d+$/.test(key) || result.has(key)) throw new SurvivalMapError("INVALID_METADATA","Duplicate or invalid GD field");
    result.set(key,entries[i+1]!);
  }
  return result;
}
function integer(values: Map<string,string>, key: string, required=false): number {
  const text=values.get(key);
  if ((text === undefined || text === "") && !required) return 0;
  if (!text || !/^\d+$/.test(text) || !Number.isSafeInteger(Number(text))) throw new SurvivalMapError("INVALID_METADATA",`Invalid GD field ${key}`);
  return Number(text);
}
export function parseSurvivalMetadata(record: string): SurvivalMapMetadata {
  const row=fields(record), demon=integer(row,"17"), automatic=integer(row,"25"), length=integer(row,"15");
  if (demon>1 || automatic>1 || length>5 || integer(row,"42")>3) throw new SurvivalMapError("INVALID_METADATA","Invalid GD booleans or length");
  const denominator=integer(row,"8"), numerator=integer(row,"9");
  if (!demon && (!denominator || !numerator)) throw new SurvivalMapError("INVALID_METADATA","Unrated native difficulty");
  return {...(row.get("2")?{name:row.get("2")!.slice(0,100)}:{}),levelId:integer(row,"1",true),stars:integer(row,"18",true),demon:demon===1,
    demonDifficulty:demon ? survivalDemonFromNativeCode(integer(row,"43")) : null,
    automatic:automatic===1,platformer:length===5,length,featureScore:integer(row,"19"),epic:integer(row,"42"),
    featureGrade:integer(row,"42")===3?"MYTHIC":integer(row,"42")===2?"LEGENDARY":integer(row,"42")===1?"EPIC":integer(row,"19")>0?"FEATURED":"NONE"};
}
export function parseSurvivalSearch(body: string, page: number) {
  if (body.trim()==="-1") throw new SurvivalMapError("EMPTY_SEARCH","GD returned no levels");
  const sections=body.split("#"), pagination=sections[3]?.split(":");
  if (!pagination || pagination.length!==3 || pagination.some(s=>!/^\d+$/.test(s))) throw new SurvivalMapError("INVALID_METADATA","Invalid GD pagination");
  const [total,offset,pageSize]=pagination.map(Number) as [number,number,number];
  if (!Number.isSafeInteger(total) || total>2_000_000 || pageSize<1 || pageSize>100 || offset!==page*pageSize ||
    (total ? offset>=total : offset!==0)) throw new SurvivalMapError("INVALID_METADATA","Out-of-range GD page");
  const records=sections[0] ? sections[0].split("|") : [];
  if (records.length>pageSize) throw new SurvivalMapError("INVALID_METADATA","Oversized GD page");
  // Native search enumerates and filters candidates independently. A stale or
  // unrated entry must not discard every valid rated map on the same page.
  const users=new Map((sections[1]??"").split("|").map(row=>{const [id,name]=row.split(":");return [id,name] as const;}));
  const levels:SurvivalMapMetadata[]=[];
  for(const record of records)try{const metadata=parseSurvivalMetadata(record),creator=users.get(fields(record).get("6"));levels.push({...metadata,...(creator?{creator:creator.slice(0,100)}:{})});}catch{}
  if(records.length && !levels.length)throw new SurvivalMapError("INVALID_METADATA","No valid metadata on the GD page");
  return {total,offset,pageSize,levels};
}
/** Native GD geometry format, bounded before decompression and before objects
 * are examined. Encoded or downloaded-empty data is never deemed playable. */
export function validSurvivalGeometry(geometry: string): boolean {
  if (!geometry || Buffer.byteLength(geometry)>32*1024*1024 || geometry.includes("\0")) return false;
  const records=geometry.split(";");
  if (records.length<2 || !records[0]?.includes("k")) return false;
  let objects=0;
  for (const record of records.slice(1)) {
    if (!record) continue;
    const values=record.split(","); if (values.length%2) return false;
    let id=false;
    for (let i=0;i<values.length;i+=2) {
      const key=values[i]!, value=values[i+1]!;
      if (!/^[1-9]\d*$/.test(key)) return false;
      if (key==="1") {if (!/^[1-9]\d*$/.test(value)) return false; id=true;}
      if ((key==="2" || key==="3") && (!value.trim() || !Number.isFinite(Number(value)))) return false;
    }
    if (!id) return false;
    objects++;
  }
  return objects>0;
}
export function verifySurvivalDownload(body: string, expected: SurvivalMapMetadata, request: SurvivalMapRequest): SurvivalVerifiedMap {
  if (body.trim()==="-1") throw new SurvivalMapError("DOWNLOAD_FAILED","GD download returned -1");
  const record=body.split("#")[0]!, row=fields(record), metadata=parseSurvivalMetadata(record);
  if (metadata.levelId!==expected.levelId || !survivalMapAllowed(metadata,request.group,request.phase,request.minimumLevelId)) {
    throw new SurvivalMapError("INVALID_METADATA","Downloaded map differs from validated catalog");
  }
  const encoded=row.get("4") ?? "", version=integer(row,"5",true);
  if (!version || encoded.length>24*1024*1024 || !/^[A-Za-z0-9_+/=-]+$/.test(encoded)) throw new SurvivalMapError("INVALID_DATA","Invalid compressed GD level");
  let geometry: Buffer;
  try {geometry=unzipSync(Buffer.from(encoded,"base64"),{maxOutputLength:32*1024*1024});}
  catch {throw new SurvivalMapError("INVALID_DATA","Level decompression failed or exceeded budget");}
  const decoded=geometry.toString("utf8");
  if (!geometry.equals(Buffer.from(decoded)) || !validSurvivalGeometry(decoded)) throw new SurvivalMapError("INVALID_DATA","Malformed native level geometry");
  return {...metadata,...(expected.creator?{creator:expected.creator}:{}),levelVersion:version,geometrySha256:createHash("sha256").update(geometry).digest("hex"),
    geometryBytes:geometry.length,songIds:integer(row,"35")?[integer(row,"35")]:[],sfxIds:[],audioVerification:"METADATA_ONLY",audioDependencyCoverage:"PRIMARY_SONG_ONLY",verifiedAt:new Date().toISOString()};
}

interface RecentCatalog {
  expiresAt:number;
  pageCount:number;
  visited:Set<number>;
  levels:Map<number,SurvivalMapMetadata>;
  sources:Map<number,number>;
}
const CATALOG_TTL_MS=10*60_000,MAX_CATALOG_LEVELS=512;

/** Server-owned candidate catalogs, replenished from rated GD pages.
 * Search metadata is provisional; the downloaded map supplies the final
 * certificate. The exact Survival predicate and match exclusions always apply. */
@Injectable()
export class GdSurvivalMapProvider implements SurvivalMapProvider {
  private readonly fetcher: typeof fetch;
  private readonly catalogs=new Map<string,RecentCatalog>();
  private readonly catalogJobs=new Map<string,Promise<RecentCatalog>>();
  private readonly badCandidates=new Map<number,number>();
  private legacyRelay=false;
  private blocked:{until:number;error:SurvivalMapError}|null=null;
  public constructor(@Inject(RANDOM_SOURCE) private readonly random: RandomSource,
    fetcher: typeof fetch = fetch) {this.fetcher=fetcher;}
  private async post(path: string, extra: Record<string,string>, signal: AbortSignal, maxBytes: number): Promise<string> {
    this.checkHttpCooldown();
    let response: Response;
    try {response=await this.fetcher(`https://www.boomlings.com/database/${path}`,{method:"POST",
      headers:{"content-type":"application/x-www-form-urlencoded","accept":"*/*","user-agent":""},
      body:new URLSearchParams({gameVersion:"22",binaryVersion:"45",secret:"Wmfd2893gb7",...extra}).toString(),
      signal:AbortSignal.any([signal,AbortSignal.timeout(8_000)])});}
    catch(error) {
      if(error instanceof SurvivalGdRelayError) {
        const failure=new SurvivalMapError("API_ERROR",error.message,[],null,HTTP_BLOCK_RETRY_MS);
        this.blocked={until:Date.now()+HTTP_BLOCK_RETRY_MS,error:failure};throw failure;
      }
      throw new SurvivalMapError("API_ERROR",`GD ${path} connection failed or timed out`);
    }
    if (!response.ok || !response.body) {
      const http=await gdHttpDiagnostics(path,response),retryAfterMs=gdRetryDelay(response);
      const error=new SurvivalMapError("API_ERROR",`GD ${path} HTTP ${response.status}; http=${JSON.stringify(http)}`,
        [],http,retryAfterMs);
      if(response.status===403 || response.status===429 || retryAfterMs>2_000)
        this.blocked={until:Date.now()+retryAfterMs,error};
      throw error;
    }
    if(path==="getGJLevels21.php")this.legacyRelay=response.headers.get("x-corum-gd-legacy-search")==="1";
    const reader=response.body.getReader(), parts:Uint8Array[]=[]; let size=0;
    try {
      for (;;) {const {value,done}=await reader.read(); if (done) break;
        size+=value.byteLength;
        if (size>maxBytes) {await reader.cancel(); throw new SurvivalMapError("INVALID_DATA","GD response exceeded size budget");}
        parts.push(value);
      }
    } catch (e) {if (e instanceof SurvivalMapError) throw e; throw new SurvivalMapError("API_ERROR","GD body download failed");}
    return Buffer.concat(parts).toString("utf8");
  }
  private checkHttpCooldown():void {
    if(!this.blocked)return;
    const remaining=this.blocked.until-Date.now();
    if(remaining<=0){this.blocked=null;return;}
    const error=this.blocked.error;
    throw new SurvivalMapError(error.code,error.message,error.failures,error.http,Math.max(2_000,remaining));
  }
  private catalogKey(filter:Record<string,string>):string {return `${this.legacyRelay?"4":filter.type??"4"}:${filter.diff}:${filter.demonFilter??""}`;}
  private async search(filter:Record<string,string>,page:number,signal:AbortSignal) {
    const actualPage=this.legacyRelay?Math.min(255,page):page;
    return parseSurvivalSearch(await this.post("getGJLevels21.php",{
      type:"4",str:"",star:"1",len:"0,1,2,3",page:String(actualPage),...filter,...(this.legacyRelay?{type:"4"}:{}),
    },signal,2*1024*1024),this.legacyRelay?Math.min(255,actualPage):actualPage);
  }
  private addCandidates(catalog:RecentCatalog,levels:readonly SurvivalMapMetadata[],page:number):void {
    for(const level of levels){catalog.levels.set(level.levelId,level);catalog.sources.set(level.levelId,page);}
    while(catalog.levels.size>MAX_CATALOG_LEVELS)catalog.levels.delete(catalog.levels.keys().next().value!);
  }
  private async catalog(filter:Record<string,string>,signal:AbortSignal):Promise<RecentCatalog> {
    const key=this.catalogKey(filter),existing=this.catalogs.get(key);
    if(existing && existing.expiresAt>Date.now())return existing;
    const active=this.catalogJobs.get(key);if(active)return active;
    const job=(async()=>{
      const initial=await this.search(filter,0,signal);
      if(!initial.total)throw new SurvivalMapError("EMPTY_SEARCH","No rated GD catalog candidates");
      const fresh:RecentCatalog={expiresAt:Date.now()+CATALOG_TTL_MS,
        pageCount:Math.min(this.legacyRelay?256:200000,Math.ceil(initial.total/initial.pageSize)),visited:new Set([0]),levels:new Map(),sources:new Map()};
      this.addCandidates(fresh,initial.levels,0);this.catalogs.set(key,fresh);return fresh;
    })().finally(()=>this.catalogJobs.delete(key));
    this.catalogJobs.set(key,job);return job;
  }
  private async replenish(catalog:RecentCatalog,filter:Record<string,string>,signal:AbortSignal):Promise<void> {
    if(catalog.visited.size>=catalog.pageCount)return;
    let page=Math.floor(this.random.next()*catalog.pageCount);
    while(catalog.visited.has(page))page=(page+1)%catalog.pageCount;
    catalog.visited.add(page);
    try {this.addCandidates(catalog,(await this.search(filter,page,signal)).levels,this.legacyRelay?Math.min(255,page):page);}
    catch(e){catalog.visited.delete(page);throw e;}
  }
  public async select(request: SurvivalMapRequest): Promise<SurvivalVerifiedMap> {
    this.checkHttpCooldown();
    const allowed=survivalMapRange(request.group,request.phase), filters:Record<string,string>[]=[];
    if (allowed.minimumStars!==null && allowed.maximumStars!==null) {
      const categories=new Set<number>();
      for (let stars=allowed.minimumStars;stars<=allowed.maximumStars;stars++) categories.add(stars<=5 ? 3 : stars<=7 ? 4 : 5);
      for(const category of categories)filters.push({diff:String(category)});
    }
    const demonFilters={EASY:"1",MEDIUM:"2",HARD:"3",INSANE:"4",EXTREME:"5"};
    for (const demon of allowed.demons) filters.push({diff:"-2",demonFilter:demonFilters[demon]});
    const searchFilters=filters.flatMap(filter=>["2","4","11"].map(type=>({...filter,type})));
    const signal=AbortSignal.timeout(30_000), excluded=new Set(request.excludedLevelIds), seen=new Set<number>();
    const failures:SurvivalMapErrorCode[]=[];let lastDetail="No eligible rated GD candidates",downloads=0;
    let lastHttp:SurvivalMapHttpDiagnostics|null=null;
    const failed=(e:unknown,fallback:SurvivalMapErrorCode="API_ERROR")=>{
      // Forbidden/throttled responses apply to the upstream route, not one
      // candidate. Stop this selection rather than retrying every filter/page.
      if(e instanceof SurvivalMapError && (e.http?.status===403 || e.http?.status===429 || e.retryAfterMs>2_000))throw e;
      failures.push(e instanceof SurvivalMapError?e.code:fallback);
      lastDetail=e instanceof Error?e.message:"GD lookup failed";
      lastHttp=e instanceof SurvivalMapError?e.http:null;
    };
    for(const [id,until]of this.badCandidates)if(until<=Date.now())this.badCandidates.delete(id);
    // Collect across sources and pages BEFORE choosing, rather than stopping
    // at the first eligible page zero. IDs are coarse strata, never upload years.
    const candidates:{level:SurvivalMapMetadata;type:string;page:number;catalog:RecentCatalog}[]=[];
    for(const filter of shuffle(searchFilters,this.random)) {
      if(signal.aborted)break;
      try {
        const catalog=await this.catalog(filter,signal);
        try {await this.replenish(catalog,filter,signal);}catch(e){failed(e);}
        for(const level of catalog.levels.values()) {
          if(!survivalMapAllowed(level,request.group,request.phase,request.minimumLevelId))continue;
          if(excluded.has(level.levelId)){if(!failures.includes("DUPLICATE_CANDIDATES"))failures.push("DUPLICATE_CANDIDATES");continue;}
          if(this.badCandidates.has(level.levelId))continue;
          candidates.push({level,type:this.legacyRelay?"4":filter.type!,page:catalog.sources.get(level.levelId)??0,catalog});
        }
      }catch(e){failed(e);}
    }
    // Round-robin independently shuffled source/ID strata, then deduplicate IDs.
    const strata=new Map<string,typeof candidates>();
    for(const candidate of candidates){const key=`${candidate.type}:${survivalIdBand(candidate.level.levelId)}`;
      const bucket=strata.get(key)??[];bucket.push(candidate);strata.set(key,bucket);}
    const groups=shuffle([...strata.values()].map(items=>shuffle(items,this.random)),this.random);
    while(groups.some(items=>items.length) && !signal.aborted && downloads<12)for(const items of groups){
      const candidate=items.pop();if(!candidate || downloads>=12 || signal.aborted)continue;
      const {level,type,page,catalog}=candidate;if(seen.has(level.levelId))continue;
      seen.add(level.levelId);downloads++;
      try {
        const map=verifySurvivalDownload(await this.post("downloadGJLevel22.php",{levelID:String(level.levelId)},signal,24*1024*1024),level,request);
        return {...map,sourceSearchType:type,sourcePage:page,sourceIdBand:survivalIdBand(map.levelId)};
      }catch(e){failed(e,"DOWNLOAD_FAILED");
        if(e instanceof SurvivalMapError && ["INVALID_DATA","INVALID_METADATA"].includes(e.code)){
          this.badCandidates.set(level.levelId,Date.now()+120_000);catalog.levels.delete(level.levelId);
        }
      }
    }
    throw new SurvivalMapError(failures.at(-1)??"NO_ELIGIBLE_MAP",lastDetail,failures,lastHttp);
  }
}

/** A bounded ID stratum, not an asserted upload date. */
export function survivalIdBand(levelId:number):string {return levelId<70_000_000?"40m-70m":levelId<100_000_000?"70m-100m":"100m+";}
