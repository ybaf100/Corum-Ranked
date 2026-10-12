import { createHash } from "node:crypto";
import { unzipSync } from "node:zlib";
import { Inject, Injectable } from "@nestjs/common";
import { survivalDemonFromNativeCode, survivalMapAllowed, survivalMapRange, shuffle,
  type RandomSource, type SurvivalDifficultyGroup, type SurvivalMapPhase,
  type SurvivalMapMetadata, type SurvivalVerifiedMap } from "@corum-ranked/rules";
import { RANDOM_SOURCE } from "../common/runtime.module.js";

export const SURVIVAL_MAP_PROVIDER = Symbol("SURVIVAL_MAP_PROVIDER");
export interface SurvivalMapRequest {
  group: SurvivalDifficultyGroup;
  phase: SurvivalMapPhase;
  minimumLevelId: number;
  excludedLevelIds: readonly number[];
}
export interface SurvivalMapProvider {
  select(request: SurvivalMapRequest): Promise<SurvivalVerifiedMap>;
}
export type SurvivalMapErrorCode = "API_ERROR" | "EMPTY_SEARCH" | "INVALID_METADATA" |
  "DOWNLOAD_FAILED" | "INVALID_DATA" | "DUPLICATE_CANDIDATES" | "NO_ELIGIBLE_MAP";
export class SurvivalMapError extends Error {
  public constructor(public readonly code: SurvivalMapErrorCode, message: string,
    public readonly failures: readonly SurvivalMapErrorCode[] = []) {super(message); this.name="SurvivalMapError";}
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
  if (demon>1 || automatic>1 || length>5) throw new SurvivalMapError("INVALID_METADATA","Invalid GD booleans or length");
  const denominator=integer(row,"8"), numerator=integer(row,"9");
  if (!demon && (!denominator || !numerator)) throw new SurvivalMapError("INVALID_METADATA","Unrated native difficulty");
  return {levelId:integer(row,"1",true),stars:integer(row,"18",true),demon:demon===1,
    demonDifficulty:demon ? survivalDemonFromNativeCode(integer(row,"43")) : null,
    automatic:automatic===1,platformer:length===5};
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
  const levels:SurvivalMapMetadata[]=[];
  for(const record of records)try{levels.push(parseSurvivalMetadata(record));}catch{}
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
  if (metadata.levelId!==expected.levelId || !survivalMapAllowed(metadata,request.group,request.phase,request.minimumLevelId) ||
    Object.keys(expected).some(key=>expected[key as keyof SurvivalMapMetadata]!==metadata[key as keyof SurvivalMapMetadata])) {
    throw new SurvivalMapError("INVALID_METADATA","Downloaded map differs from validated catalog");
  }
  const encoded=row.get("4") ?? "", version=integer(row,"5",true);
  if (!version || encoded.length>24*1024*1024 || !/^[A-Za-z0-9_+/=-]+$/.test(encoded)) throw new SurvivalMapError("INVALID_DATA","Invalid compressed GD level");
  let geometry: Buffer;
  try {geometry=unzipSync(Buffer.from(encoded,"base64"),{maxOutputLength:32*1024*1024});}
  catch {throw new SurvivalMapError("INVALID_DATA","Level decompression failed or exceeded budget");}
  const decoded=geometry.toString("utf8");
  if (!geometry.equals(Buffer.from(decoded)) || !validSurvivalGeometry(decoded)) throw new SurvivalMapError("INVALID_DATA","Malformed native level geometry");
  return {...metadata,levelVersion:version,geometrySha256:createHash("sha256").update(geometry).digest("hex"),
    geometryBytes:geometry.length,verifiedAt:new Date().toISOString()};
}

/** Real GD provider. Search narrows native categories, while the range predicate
 * remains authoritative (e.g. GOLD R3 admits 8/9 stars and Easy Demon only).
 * Bootstrap establishes pagination; random unvisited pages prevent a fixed pool.
 * Bounded failure returns control to the engine's durable retry scheduler. */
@Injectable()
export class GdSurvivalMapProvider implements SurvivalMapProvider {
  private readonly fetcher: typeof fetch;
  public constructor(@Inject(RANDOM_SOURCE) private readonly random: RandomSource,
    fetcher: typeof fetch = fetch) {this.fetcher=fetcher;}
  private async post(path: string, extra: Record<string,string>, signal: AbortSignal, maxBytes: number): Promise<string> {
    let response: Response;
    try {response=await this.fetcher(`https://www.boomlings.com/database/${path}`,{method:"POST",
      headers:{"content-type":"application/x-www-form-urlencoded","user-agent":""},
      body:new URLSearchParams({gameVersion:"22",binaryVersion:"45",secret:"Wmfd2893gb7",...extra}).toString(),
      signal:AbortSignal.any([signal,AbortSignal.timeout(8_000)])});}
    catch {throw new SurvivalMapError("API_ERROR","GD request failed");}
    if (!response.ok || !response.body) throw new SurvivalMapError("API_ERROR",`GD HTTP ${response.status}`);
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
  public async select(request: SurvivalMapRequest): Promise<SurvivalVerifiedMap> {
    const allowed=survivalMapRange(request.group,request.phase), filters:Record<string,string>[]=[];
    if (allowed.minimumStars!==null && allowed.maximumStars!==null) {
      const categories=new Set<number>();
      for (let stars=allowed.minimumStars;stars<=allowed.maximumStars;stars++) categories.add(stars<=5 ? 3 : stars<=7 ? 4 : 5);
      for(const category of categories)filters.push({diff:String(category)});
    }
    const demonFilters={EASY:"1",MEDIUM:"2",HARD:"3",INSANE:"4",EXTREME:"5"};
    for (const demon of allowed.demons) filters.push({diff:"-2",demonFilter:demonFilters[demon]});
    const signal=AbortSignal.timeout(30_000), excluded=new Set(request.excludedLevelIds), seen=new Set<number>();
    const failures:SurvivalMapErrorCode[]=[];
    const inspect=async(levels:readonly SurvivalMapMetadata[]):Promise<SurvivalVerifiedMap|null> => {
      for (const level of shuffle(levels,this.random)) {
        // Ineligible maps never enter the duplicate-candidate pool: repeated
        // old IDs or wrong difficulties mean exhaustion, not a reuse failure.
        if (!survivalMapAllowed(level,request.group,request.phase,request.minimumLevelId)) continue;
        if (excluded.has(level.levelId) || seen.has(level.levelId)) {failures.push("DUPLICATE_CANDIDATES"); continue;}
        seen.add(level.levelId);
        try {return verifySurvivalDownload(await this.post("downloadGJLevel22.php",{levelID:String(level.levelId)},signal,24*1024*1024),level,request);}
        catch (e) {failures.push(e instanceof SurvivalMapError ? e.code : "DOWNLOAD_FAILED");}
        if (signal.aborted) return null;
      }
      return null;
    };
    // Follow GD native searches (and Versus): one difficulty per query and a
    // bounded catalog window. Deep advertised pages can be unavailable even
    // when page zero reports a much larger total.
    const groups:{filter:Record<string,string>;initial:ReturnType<typeof parseSurvivalSearch>;visited:Set<number>;pageCount:number;failed:boolean}[]=[];
    const search=async(filter:Record<string,string>,page:number)=>parseSurvivalSearch(await this.post("getGJLevels21.php",
      {type:"2",str:"",star:"1",len:"0,1,2,3,4",page:String(page),...filter},signal,2*1024*1024),page);
    for(const filter of shuffle(filters,this.random)) {
      if(signal.aborted)break;
      try {
        const initial=await search(filter,0);
        if(!initial.total)throw new SurvivalMapError("EMPTY_SEARCH","No rated catalog candidates");
        groups.push({filter,initial,visited:new Set([0]),pageCount:Math.ceil(Math.min(initial.total,10_000)/initial.pageSize),failed:false});
      }catch(e){failures.push(e instanceof SurvivalMapError?e.code:"API_ERROR");}
    }
    // Give every eligible difficulty a turn instead of exhausting one category
    // before another Demon tier or normal-star category can be searched.
    for(let scan=0;scan<3 && !signal.aborted;scan++)for(const group of groups) {
      if(signal.aborted || group.failed || group.visited.size>=group.pageCount)continue;
      let page=Math.floor(this.random.next()*group.pageCount);
      while(group.visited.has(page))page=(page+1)%group.pageCount;
      group.visited.add(page);
      try {
        const next=await search(group.filter,page);
        const map=await inspect(next.levels);if(map)return map;
      }catch(e){failures.push(e instanceof SurvivalMapError?e.code:"API_ERROR");group.failed=true;}
    }
    // Retain a validated first-page fallback, without making popular maps the
    // fixed operating pool. Never relax ID, stars/Demon, data or reuse filters.
    for(const group of groups) {if(signal.aborted)break;const map=await inspect(group.initial.levels);if(map)return map;}
    throw new SurvivalMapError(failures.at(-1) ?? "NO_ELIGIBLE_MAP","No verified eligible Survival map in this search budget",failures);
  }
}
