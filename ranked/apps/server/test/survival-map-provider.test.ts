import { gzipSync } from "node:zlib";
import { describe,expect,it,vi } from "vitest";
import { SeededRandom } from "@corum-ranked/rules";
import { GdSurvivalMapProvider,parseSurvivalMetadata,parseSurvivalSearch,validSurvivalGeometry,
  verifySurvivalDownload,type SurvivalMapRequest } from "../src/survival/survival-map.provider.js";

const request:SurvivalMapRequest={group:"RED_BRONZE",phase:"ROUND_1",minimumLevelId:40000000,excludedLevelIds:[]};
const record=(id=40000000,extra="")=>`1:${id}:18:4:17::43:0:25::15:2:8:10:9:30${extra}`;
const geometry="kS38,1;1,1,2,30,3,10;";
const download=(id=40000000,data=geometry)=>`${record(id)}:5:1:4:${gzipSync(data).toString("base64url")}#hash#hash`;
const page=(ids:number[],total=ids.length,offset=0,size=10)=>`${ids.map(id=>record(id)).join("|")}#creators#songs#${total}:${offset}:${size}#hash`;
describe("actual GD Survival search/download protocol",()=>{
  it("parses native zero/empty fields and distinguishes Demon subtype codes",()=>{
    expect(parseSurvivalMetadata(record())).toMatchObject({levelId:40000000,demon:false,automatic:false,platformer:false});
    const demon=record().replace("18:4","18:10").replace("17::43:0","17:1:43:4");
    expect(parseSurvivalMetadata(demon)).toMatchObject({demon:true,demonDifficulty:"MEDIUM"});
    expect(parseSurvivalMetadata(record().replace("15:2","15:5")).platformer).toBe(true);
  });
  it.each(["-1","html",page([40000000],1,10),record()+":18:4"])("rejects malformed catalog %s",body=>{
    expect(()=>parseSurvivalSearch(body,0)).toThrow();
  });
  it("keeps eligible records on sparse pages and filters stale metadata independently",()=>{
    const body=`${record()}|${record(40000001).replace("18:4","18:invalid")}###10:0:10#hash`;
    expect(parseSurvivalSearch(body,0).levels.map(level=>level.levelId)).toEqual([40000000]);
    expect(parseSurvivalSearch(page([40000000],2),0).levels).toHaveLength(1);
  });
  it("accepts a rated 40-million map only after real geometry download validation",()=>{
    const valid=verifySurvivalDownload(download(),parseSurvivalMetadata(record()),request);
    expect(valid.levelId).toBe(40000000);expect(valid.geometryBytes).toBe(Buffer.byteLength(geometry));expect(valid.geometrySha256).toMatch(/^[a-f0-9]{64}$/);
  });
  it.each(["", "header", "kS38,1;", "kS38,1;1,0;", "kS38,1;1,1,2,NaN;", "kS38,1;1,1,2;", "kS38,1;1,1;\0"])("rejects invalid geometry %s",bad=>{
    expect(validSurvivalGeometry(bad)).toBe(false);expect(()=>verifySurvivalDownload(download(40000000,bad),parseSurvivalMetadata(record()),request)).toThrow();
  });
  it("rejects wrong ID, unrated, Auto, Platformer, changed stars and broken compressed data",()=>{
    const expected=parseSurvivalMetadata(record());
    for(const body of [download(39999999),download().replace("18:4","18:0"),download().replace("25::","25:1:"),
      download().replace("15:2","15:5"),download().replace("18:4","18:9"),`${record()}:5:1:4:not-valid-base64`])
      expect(()=>verifySurvivalDownload(body,expected,request)).toThrow();
  });
  it("uses native Demon filtering, never confuses search filter with metadata code",async()=>{
    const paths:string[]=[];
    const fetcher=vi.fn(async(url:string|URL|Request,init?:RequestInit)=>{
      const body=new URLSearchParams(String(init?.body));paths.push(String(url));
      const demon=record().replace("18:4","18:10").replace("17::43:0","17:1:43:5");
      if(String(url).endsWith("getGJLevels21.php")){expect(body.get("diff")).toBe("-2");expect(body.get("demonFilter")).toBe("4");return new Response(`${demon}###1:0:10#hash`);}
      return new Response(`${demon}:5:1:4:${gzipSync(geometry).toString("base64url")}#hash#hash`);
    }) as unknown as typeof fetch;
    const map=await new GdSurvivalMapProvider(new SeededRandom(1),fetcher).select({...request,group:"GOLD",phase:"TIEBREAK"});
    expect(map.demonDifficulty).toBe("INSANE");expect(paths).toHaveLength(2);
  });
  it("randomizes validated pages, excludes duplicates/old IDs, and uses the same map only once",async()=>{
    const pages:number[]=[];let downloadRequests=0;
    const fetcher=vi.fn(async(url:string|URL|Request,init?:RequestInit)=>{
      const body=new URLSearchParams(String(init?.body));
      if(String(url).endsWith("getGJLevels21.php")){const p=Number(body.get("page"));pages.push(p);return new Response(p===0?page([39999999,40000000],4,0,2):page([40000001,40000002],4,2,2));}
      downloadRequests++;return new Response(download(Number(body.get("levelID"))));
    }) as unknown as typeof fetch;
    const map=await new GdSurvivalMapProvider(new SeededRandom(1),fetcher).select({...request,excludedLevelIds:[40000000,40000001]});
    expect(map.levelId).toBe(40000002);expect(pages).toEqual([0,0,1]);expect(downloadRequests).toBe(1);
  });
  it("retries API failure and invalid downloads while retaining exact constraints",async()=>{
    let searches=0,downloads=0;
    const fetcher=vi.fn(async(url:string|URL|Request,init?:RequestInit)=>{
      if(String(url).endsWith("getGJLevels21.php")){searches++;return searches===1?new Response("error",{status:500}):new Response(page([40000000,40000001]));}
      downloads++;return new Response(download(Number(new URLSearchParams(String(init?.body)).get("levelID")),downloads===1?"invalid":geometry));
    }) as unknown as typeof fetch;
    const result=await new GdSurvivalMapProvider(new SeededRandom(1),fetcher).select(request);expect(result.levelId).toBeGreaterThanOrEqual(40000000);expect(searches).toBe(2);expect(downloads).toBe(2);
  });
  it("reports exhaustion without generating a level ID or selecting a wrong difficulty",async()=>{
    const fetcher=vi.fn(async()=>new Response(page([39999999]))) as unknown as typeof fetch;
    await expect(new GdSurvivalMapProvider(new SeededRandom(1),fetcher).select(request)).rejects.toMatchObject({code:"NO_ELIGIBLE_MAP"});
    expect(vi.mocked(fetcher).mock.calls.every(([url])=>String(url).endsWith("getGJLevels21.php"))).toBe(true);
  });
});
