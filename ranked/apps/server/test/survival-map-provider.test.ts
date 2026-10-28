import { gzipSync } from "node:zlib";
import { afterEach,describe,expect,it,vi } from "vitest";
import { SeededRandom } from "@corum-ranked/rules";
import { GdSurvivalMapProvider,parseSurvivalMetadata,parseSurvivalSearch,validSurvivalGeometry,
  verifySurvivalDownload,type SurvivalMapRequest } from "../src/survival/survival-map.provider.js";

const request:SurvivalMapRequest={group:"RED_BRONZE",phase:"ROUND_1",minimumLevelId:40000000,excludedLevelIds:[]};
const record=(id=40000000,extra="")=>`1:${id}:18:4:17::43:0:25::15:2:8:10:9:30${extra}`;
const geometry="kS38,1;1,1,2,30,3,10;";
const download=(id=40000000,data=geometry)=>`${record(id)}:5:1:4:${gzipSync(data).toString("base64url")}#hash#hash`;
const page=(ids:number[],total=ids.length,offset=0,size=10)=>`${ids.map(id=>record(id)).join("|")}#creators#songs#${total}:${offset}:${size}#hash`;
afterEach(()=>vi.useRealTimers());
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
  it("uses current download metadata when catalog stars changed within the exact allowed range",()=>{
    const valid=verifySurvivalDownload(download().replace("18:4","18:5"),parseSurvivalMetadata(record()),request);
    expect(valid.stars).toBe(5);
  });
  it.each(["", "header", "kS38,1;", "kS38,1;1,0;", "kS38,1;1,1,2,NaN;", "kS38,1;1,1,2;", "kS38,1;1,1;\0"])("rejects invalid geometry %s",bad=>{
    expect(validSurvivalGeometry(bad)).toBe(false);expect(()=>verifySurvivalDownload(download(40000000,bad),parseSurvivalMetadata(record()),request)).toThrow();
  });
  it("rejects wrong ID, unrated, Auto, Platformer, changed stars and broken compressed data",()=>{
    const expected=parseSurvivalMetadata(record());
    for(const body of [download(39999999),download().replace("18:4","18:0"),download().replace("25::","25:1:"),
      download().replace("15:2","15:4"),download().replace("15:2","15:5"),download().replace("18:4","18:9"),`${record()}:5:1:4:not-valid-base64`])
      expect(()=>verifySurvivalDownload(body,expected,request)).toThrow();
  });
  it("uses native Demon filtering, never confuses search filter with metadata code",async()=>{
    const paths:string[]=[];
    const fetcher=vi.fn(async(url:string|URL|Request,init?:RequestInit)=>{
      const body=new URLSearchParams(String(init?.body));paths.push(String(url));
      const demon=record().replace("18:4","18:10").replace("17::43:0","17:1:43:5");
      if(String(url).endsWith("getGJLevels21.php")){expect(["2","4","11"]).toContain(body.get("type"));expect(body.get("star")).toBe("1");expect(body.get("diff")).toBe("-2");expect(body.get("demonFilter")).toBe("4");return new Response(`${demon}###1:0:10#hash`);}
      return new Response(`${demon}:5:1:4:${gzipSync(geometry).toString("base64url")}#hash#hash`);
    }) as unknown as typeof fetch;
    const map=await new GdSurvivalMapProvider(new SeededRandom(1),fetcher).select({...request,group:"GOLD",phase:"TIEBREAK"});
    expect(map.demonDifficulty).toBe("INSANE");expect(paths).toHaveLength(4);
  });
  it("randomizes validated pages, excludes duplicates/old IDs, and uses the same map only once",async()=>{
    const pages:number[]=[];let downloadRequests=0;
    const fetcher=vi.fn(async(url:string|URL|Request,init?:RequestInit)=>{
      const body=new URLSearchParams(String(init?.body));
      if(String(url).endsWith("getGJLevels21.php")){const p=Number(body.get("page"));pages.push(p);return new Response(p===0?page([39999999,40000000],4,0,2):page([40000001,40000002],4,2,2));}
      downloadRequests++;return new Response(download(Number(body.get("levelID"))));
    }) as unknown as typeof fetch;
    const map=await new GdSurvivalMapProvider(new SeededRandom(1),fetcher).select({...request,excludedLevelIds:[40000000,40000001]});
    expect(map.levelId).toBe(40000002);expect(pages).toEqual(Array(6).fill([0,1]).flat());expect(downloadRequests).toBe(1);
  });
  it("shares a recent candidate pool but revalidates every selected download and match exclusion",async()=>{
    let searches=0,downloads=0;
    const fetcher=vi.fn(async(url:string|URL|Request,init?:RequestInit)=>{
      if(String(url).endsWith("getGJLevels21.php")){searches++;return new Response(page([40000000,40000001]));}
      downloads++;return new Response(download(Number(new URLSearchParams(String(init?.body)).get("levelID"))));
    }) as unknown as typeof fetch;
    const provider=new GdSurvivalMapProvider(new SeededRandom(1),fetcher);
    const first=await provider.select(request);
    const second=await provider.select({...request,excludedLevelIds:[first.levelId]});
    expect(second.levelId).not.toBe(first.levelId);expect(downloads).toBe(2);
    // ROUND_1 searches categories 3 and 4 across all three sources; each is bootstrapped
    // at most once, not re-fetched for every round or concurrent match.
    expect(searches).toBe(6);
  });
  it.each([[0,0],[12,0],[0,1],[4,2],[1,3]])("keeps GD feature %i / epic %i through verified metadata",async(featureScore,epic)=>{
    const quality=record()+`:19:${featureScore}:42:${epic}`;
    expect(parseSurvivalMetadata(quality)).toMatchObject({length:2,featureScore,epic});
    expect(verifySurvivalDownload(`${quality}:5:1:4:${gzipSync(geometry).toString("base64url")}#hash`,parseSurvivalMetadata(quality),request))
      .toMatchObject({featureScore,epic,featureGrade:epic===3?"MYTHIC":epic===2?"LEGENDARY":epic===1?"EPIC":featureScore>0?"FEATURED":"NONE"});
  });
  it("samples catalogs past the former page 255 boundary without a release-date filter",async()=>{
    const pages:number[]=[];
    const fetcher=vi.fn(async(url:string|URL|Request,init?:RequestInit)=>{
      const form=new URLSearchParams(String(init?.body));
      if(String(url).endsWith("getGJLevels21.php")){
        expect(form.get("len")).toBe("0,1,2,3");
        const index=Number(form.get("page"));pages.push(index);
        return new Response(index===0?page([39999999],20000):page([40000001],20000,index*10));
      }
      return new Response(download(Number(form.get("levelID"))));
    }) as unknown as typeof fetch;
    const provider=new GdSurvivalMapProvider({next:()=>.8},fetcher);
    expect((await provider.select(request)).levelId).toBe(40000001);
    expect(pages).toEqual(Array(6).fill([0,1600]).flat());
  });
  it("expands liked, recent and awarded catalogs and rejects XL before any download",async()=>{
    const types=new Set<string>();let downloads=0;
    const fetcher=vi.fn(async(url:string|URL|Request,init?:RequestInit)=>{
      if(String(url).endsWith("getGJLevels21.php")){
        const form=new URLSearchParams(String(init?.body));types.add(form.get("type")!);
        return new Response(`${record().replace("15:2","15:4")}###1:0:10#hash`);
      }
      downloads++;return new Response(download());
    }) as unknown as typeof fetch;
    await expect(new GdSurvivalMapProvider(new SeededRandom(1),fetcher).select(request)).rejects.toMatchObject({code:"NO_ELIGIBLE_MAP"});
    expect([...types].sort()).toEqual(["11","2","4"]);expect(downloads).toBe(0);
  });
  it("retains API status in diagnostic errors and leaves timers to the engine",async()=>{
    const fetcher=vi.fn(async()=>new Response("unavailable",{status:403})) as unknown as typeof fetch;
    await expect(new GdSurvivalMapProvider(new SeededRandom(1),fetcher).select(request))
      .rejects.toMatchObject({code:"API_ERROR",message:expect.stringContaining("GD getGJLevels21.php HTTP 403"),http:{status:403}});
    expect(fetcher).toHaveBeenCalledTimes(1);
  });
  it.each(["1005","1006","1010","1015","1020"])("records Cloudflare plaintext error %s without assuming a challenge or changing agents",async(code)=>{
    const fetcher=vi.fn(async(_url:string|URL|Request,init?:RequestInit)=>{
      expect(new Headers(init?.headers).get("user-agent")).toBe("");
      return new Response(`error code: ${code}\r\n`,{status:403,headers:{server:"cloudflare",
        "content-type":"text/plain; charset=UTF-8","cf-ray":"fixture-SIN"}});
    }) as unknown as typeof fetch;
    const provider=new GdSurvivalMapProvider(new SeededRandom(1),fetcher);
    await expect(provider.select(request)).rejects.toMatchObject({code:"API_ERROR",retryAfterMs:30000,
      http:{edgeErrorCode:code,textSummary:`error code: ${code}`,bodyKind:"CLOUDFLARE_BLOCK",cfMitigated:null,title:null},
      message:expect.stringContaining(`\"edgeErrorCode\":\"${code}\"`)});
    await expect(provider.select({...request,group:"GOLD"})).rejects.toMatchObject({http:{edgeErrorCode:code}});
    expect(fetcher).toHaveBeenCalledTimes(1);
  });
  it("keeps unknown Cloudflare plaintext errors visible but bounds their summaries",async()=>{
    const fetcher=vi.fn(async()=>new Response("Forbidden\n"+"x".repeat(200)+"private-outside-summary",{
      status:403,headers:{server:"cloudflare","content-type":"text/plain","set-cookie":"private-cookie"}})) as unknown as typeof fetch;
    let failure:unknown;
    try{await new GdSurvivalMapProvider(new SeededRandom(1),fetcher).select(request);}catch(error){failure=error;}
    expect(failure).toMatchObject({http:{edgeErrorCode:null,textSummary:"Forbidden "+"x".repeat(150),bodyKind:"TEXT"}});
    expect(JSON.stringify(failure)).not.toContain("private-outside-summary");
    expect(JSON.stringify(failure)).not.toContain("private-cookie");expect(fetcher).toHaveBeenCalledTimes(1);
  });
  it("does not copy unrelated plaintext or full HTML denial bodies into diagnostics",async()=>{
    for(const [body,headers,expected] of [
      ["error code: 1010 private-origin-body",{server:"origin","content-type":"text/plain"},{edgeErrorCode:null,bodyKind:"TEXT"}],
      ["<html><title>Access denied</title>Cloudflare error code: 1020 private-html-body</html>",
        {server:"cloudflare","content-type":"text/html"},{edgeErrorCode:"1020",bodyKind:"CLOUDFLARE_BLOCK"}],
      ["error code: 1010 private-challenge-body",{server:"cloudflare","content-type":"text/plain","cf-mitigated":"challenge"},
        {edgeErrorCode:"1010",bodyKind:"CLOUDFLARE_CHALLENGE"}],
    ] as const) {
      const fetcher=vi.fn(async()=>new Response(body,{status:403,headers})) as unknown as typeof fetch;
      let failure:unknown;
      try{await new GdSurvivalMapProvider(new SeededRandom(1),fetcher).select(request);}catch(error){failure=error;}
      expect(failure).toMatchObject({http:{...expected,textSummary:null}});
      expect(JSON.stringify(failure)).not.toMatch(/private-(?:origin|html|challenge)-body/);
      expect(fetcher).toHaveBeenCalledTimes(1);
    }
  });
  it("captures challenge diagnostics, suppresses all selections during cooldown and recovers afterward",async()=>{
    vi.useFakeTimers();vi.setSystemTime(0);let searches=0;
    const fetcher=vi.fn(async(url:string|URL|Request)=>{
      if(String(url).endsWith("getGJLevels21.php")) {
        searches++;
        if(searches===1)return new Response('<html><title>Just a moment...</title>private-response-body</html>',{
          status:403,headers:{server:"cloudflare","content-type":"text/html","cf-ray":"fixture-ray","cf-mitigated":"challenge",
            "set-cookie":"private-cookie","authorization":"private-header"}});
        return new Response(page([40000000]));
      }
      return new Response(download());
    }) as unknown as typeof fetch;
    const provider=new GdSurvivalMapProvider(new SeededRandom(1),fetcher);
    await expect(provider.select(request)).rejects.toMatchObject({code:"API_ERROR",retryAfterMs:30000,
      http:{status:403,cfRay:"fixture-ray",cfMitigated:"challenge",title:"Just a moment...",bodyKind:"CLOUDFLARE_CHALLENGE"}});
    for(const group of ["RED_BRONZE","SILVER","GOLD"] as const)
      await expect(provider.select({...request,group})).rejects.toThrow("fixture-ray");
    expect(searches).toBe(1);
    vi.setSystemTime(29999);await expect(provider.select(request)).rejects.toThrow("HTTP 403");expect(searches).toBe(1);
    vi.setSystemTime(30000);expect((await provider.select(request)).levelId).toBe(40000000);expect(searches).toBe(7);
  });
  it("bounds error-body reads and excludes raw bodies and private response headers",async()=>{
    let cancelled=false;
    const body=new ReadableStream<Uint8Array>({start(controller){
      controller.enqueue(Buffer.from("x".repeat(4096)+"<title>private-body-secret</title>"));
    },cancel(){cancelled=true;}});
    const fetcher=vi.fn(async()=>new Response(body,{status:403,headers:{"content-type":"text/html","set-cookie":"private-cookie-secret"}})) as unknown as typeof fetch;
    let failure:unknown;
    try{await new GdSurvivalMapProvider(new SeededRandom(1),fetcher).select(request);}catch(error){failure=error;}
    expect(failure).toMatchObject({http:{title:null,bodyKind:"HTML"}});
    expect(JSON.stringify(failure)).not.toContain("private-body-secret");expect(JSON.stringify(failure)).not.toContain("private-cookie-secret");
    expect(cancelled).toBe(true);expect(fetcher).toHaveBeenCalledTimes(1);
  });
  it.each([["90",90000],["999999999",120000],["invalid",30000]])("honors bounded Retry-After %s without exhausting map filters",async(value,delay)=>{
    const fetcher=vi.fn(async()=>new Response("rate limited",{status:429,headers:{"retry-after":value}})) as unknown as typeof fetch;
    await expect(new GdSurvivalMapProvider(new SeededRandom(1),fetcher).select(request))
      .rejects.toMatchObject({retryAfterMs:delay,http:{status:429,retryAfter:value}});
    expect(fetcher).toHaveBeenCalledTimes(1);
  });
  it("retries API failure and invalid downloads while retaining exact constraints",async()=>{
    let searches=0,downloads=0;
    const fetcher=vi.fn(async(url:string|URL|Request,init?:RequestInit)=>{
      if(String(url).endsWith("getGJLevels21.php")){searches++;return searches===1?new Response("error",{status:500}):new Response(page([40000000,40000001]));}
      downloads++;return new Response(download(Number(new URLSearchParams(String(init?.body)).get("levelID")),downloads===1?"invalid":geometry));
    }) as unknown as typeof fetch;
    const result=await new GdSurvivalMapProvider(new SeededRandom(1),fetcher).select(request);expect(result.levelId).toBeGreaterThanOrEqual(40000000);expect(searches).toBe(6);expect(downloads).toBe(2);
  });
  it("reports exhaustion without generating a level ID or selecting a wrong difficulty",async()=>{
    const fetcher=vi.fn(async()=>new Response(page([39999999]))) as unknown as typeof fetch;
    await expect(new GdSurvivalMapProvider(new SeededRandom(1),fetcher).select(request)).rejects.toMatchObject({code:"NO_ELIGIBLE_MAP"});
    expect(vi.mocked(fetcher).mock.calls.every(([url])=>String(url).endsWith("getGJLevels21.php"))).toBe(true);
  });
});
