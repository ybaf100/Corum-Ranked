import { gzipSync } from "node:zlib";
import { describe,expect,it,vi } from "vitest";
import { SeededRandom } from "@corum-ranked/rules";
import { createGdRelayWorker } from "../../../tools/gd-relay/worker.mjs";
import { createSurvivalGdFetch } from "../src/survival/survival-gd-transport.js";
import { GdSurvivalMapProvider } from "../src/survival/survival-map.provider.js";

const token="fixture_"+"a".repeat(48),relay={origin:"https://owned-relay.example",token};
const request={group:"RED_BRONZE",phase:"ROUND_1",minimumLevelId:40_000_000,excludedLevelIds:[]} as const;
const record=(id:number)=>`1:${id}:18:4:17::43:0:25::15:2:8:10:9:30`;
const geometry="kS38,1;1,1,2,30,3,10;";
function serverFetch(worker:ReturnType<typeof createGdRelayWorker>) {
  return vi.fn(async(input:string|URL|Request,init?:RequestInit)=>{
    expect(String(input)).toMatch(/^https:\/\/owned-relay\.example\/(?:getGJLevels21|downloadGJLevel22)\.php$/);
    expect(init?.redirect).toBe("manual");
    expect(new Headers(init?.headers).get("authorization")).toBe(`Bearer ${token}`);
    return worker.fetch(new Request(input,init),{GD_RELAY_TOKEN:token});
  }) as unknown as typeof fetch;
}
describe("Survival authenticated relay and real GD validation pipeline",()=>{
  it("selects and verifies downloaded geometry through the relay, including exclusions on reuse",async()=>{
    const upstream=vi.fn(async(input:string|URL|Request,init?:RequestInit)=>{
      expect(String(input)).toMatch(/^https:\/\/www\.boomlings\.com\/database\//);
      expect(new Headers(init?.headers).get("authorization")).toBeNull();
      if(String(input).endsWith("getGJLevels21.php"))return new Response(`${record(40000000)}|${record(40000001)}###2:0:10#hash`);
      const id=Number(new URLSearchParams(String(init?.body)).get("levelID"));
      return new Response(`${record(id)}:5:1:4:${gzipSync(geometry).toString("base64url")}#hash#hash`);
    }) as unknown as typeof fetch;
    const transport=serverFetch(createGdRelayWorker(upstream));
    const provider=new GdSurvivalMapProvider(new SeededRandom(1),createSurvivalGdFetch(relay,transport));
    const first=await provider.select(request);
    expect(first.geometryBytes).toBe(Buffer.byteLength(geometry));expect(first.geometrySha256).toMatch(/^[a-f0-9]{64}$/);
    const second=await provider.select({...request,excludedLevelIds:[first.levelId]});
    expect(second.levelId).not.toBe(first.levelId);expect(second.stars).toBe(4);
    expect(upstream).toHaveBeenCalled();
  });
  it("keeps an older type-4 relay playable while rejecting XL locally",async()=>{
    const worker=createGdRelayWorker(async(input,init)=>String(input).endsWith("getGJLevels21.php")?
      new Response(`${record(40000000).replace("15:2","15:4")}|${record(40000001)}###2:0:10#hash`):
      new Response(`${record(Number(new URLSearchParams(String(init?.body)).get("levelID")))}:5:1:4:${gzipSync(geometry).toString("base64url")}#hash`));
    let rejected=0;
    const legacy=vi.fn(async(input:string|URL|Request,init?:RequestInit)=>{
      const form=new URLSearchParams(String(init?.body));
      if(String(input).endsWith("getGJLevels21.php") && (form.get("type")!=="4" || form.get("len")!=="0,1,2,3,4" || Number(form.get("page"))>255)){
        rejected++;return new Response("legacy filter",{status:400,headers:{"x-corum-gd-relay-error":"INVALID_REQUEST"}});
      }
      return worker.fetch(new Request(input,init),{GD_RELAY_TOKEN:token});
    }) as unknown as typeof fetch;
    const provider=new GdSurvivalMapProvider(new SeededRandom(1),createSurvivalGdFetch(relay,legacy));
    expect((await provider.select(request)).levelId).toBe(40000001);expect(rejected).toBe(1);
  });
  it("retains the actual GD 1005/Ray instead of Cloudflare's outer Worker headers and honors cooldown",async()=>{
    const transport=serverFetch(createGdRelayWorker(async()=>new Response("error code: 1005",{status:403,
      headers:{server:"cloudflare","cf-ray":"GD-SIN","content-type":"text/plain"}})));
    const withOuterHeaders=vi.fn(async(input:string|URL|Request,init?:RequestInit)=>{
      const r=await transport(input,init);r.headers.set("cf-ray","worker-other-Ray");r.headers.set("server","worker-edge");return r;
    }) as unknown as typeof fetch;
    const provider=new GdSurvivalMapProvider(new SeededRandom(1),createSurvivalGdFetch(relay,withOuterHeaders));
    await expect(provider.select(request)).rejects.toMatchObject({http:{status:403,edgeErrorCode:"1005",cfRay:"GD-SIN",server:"cloudflare"},retryAfterMs:30000});
    await expect(provider.select(request)).rejects.toThrow("GD-SIN");expect(transport).toHaveBeenCalledTimes(1);
  });
  it.each(["UNAUTHORIZED","UPSTREAM_TIMEOUT","RESPONSE_TOO_LARGE"])("distinguishes relay %s from a GD denial without exposing the token",async(code)=>{
    const transport=vi.fn(async()=>new Response(token,{status:code==="UNAUTHORIZED"?401:502,
      headers:{"x-corum-gd-relay":"v1","x-corum-gd-relay-error":code,server:"cloudflare"}})) as unknown as typeof fetch;
    const provider=new GdSurvivalMapProvider(new SeededRandom(1),createSurvivalGdFetch(relay,transport));
    let error:unknown;try{await provider.select(request);}catch(e){error=e;}
    expect(error).toMatchObject({code:"API_ERROR",http:null,retryAfterMs:30000,message:expect.stringContaining(code)});
    expect(JSON.stringify(error)).not.toContain(token);
    await expect(provider.select(request)).rejects.toThrow(code);expect(transport).toHaveBeenCalledTimes(1);
  });
  it("rejects an unrelated HTML 200 relay response without falling back to the blocked Render route",async()=>{
    const transport=vi.fn(async()=>new Response("<html>login</html>")) as unknown as typeof fetch;
    await expect(new GdSurvivalMapProvider(new SeededRandom(1),createSurvivalGdFetch(relay,transport)).select(request))
      .rejects.toThrow("INVALID_RELAY_RESPONSE");expect(transport).toHaveBeenCalledTimes(1);
  });
  it("still rejects forged difficulty and invalid geometry even with the correct relay marker",async()=>{
    for(const change of ["wrong-difficulty","bad-geometry"]) {
      const worker=createGdRelayWorker(async(input)=>String(input).endsWith("getGJLevels21.php")?
        new Response(`${record(40000000)}###1:0:10#hash`):
        new Response(`${change==="wrong-difficulty"?record(40000000).replace("18:4","18:9"):record(40000000)}:5:1:4:${gzipSync(change==="bad-geometry"?"garbage":geometry).toString("base64url")}#hash`));
      await expect(new GdSurvivalMapProvider(new SeededRandom(1),createSurvivalGdFetch(relay,serverFetch(worker))).select(request))
        .rejects.toMatchObject({code:change==="wrong-difficulty"?"INVALID_METADATA":"INVALID_DATA"});
    }
  });
  it("rejects token-bearing redirects without following them",async()=>{
    const transport=vi.fn(async(_input:string|URL|Request,init?:RequestInit)=>{
      expect(init?.redirect).toBe("manual");return new Response(null,{status:302,headers:{location:"https://other.example"}});
    }) as unknown as typeof fetch;
    await expect(new GdSurvivalMapProvider(new SeededRandom(1),createSurvivalGdFetch(relay,transport)).select(request))
      .rejects.toThrow("INVALID_RELAY_RESPONSE");expect(transport).toHaveBeenCalledTimes(1);
  });
  it("leaves direct transport requests unchanged when no relay is configured",async()=>{
    const transport=vi.fn(async(input:string|URL|Request,init?:RequestInit)=>{
      expect(String(input)).toMatch(/^https:\/\/www\.boomlings\.com\/database\//);
      expect(new Headers(init?.headers).get("authorization")).toBeNull();
      return String(input).endsWith("getGJLevels21.php")?new Response(`${record(40000000)}###1:0:10#hash`):
        new Response(`${record(40000000)}:5:1:4:${gzipSync(geometry).toString("base64url")}#hash`);
    }) as unknown as typeof fetch;
    expect((await new GdSurvivalMapProvider(new SeededRandom(1),createSurvivalGdFetch(undefined,transport)).select(request)).levelId).toBe(40000000);
  });
});
