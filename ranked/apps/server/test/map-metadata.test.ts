import { readFile } from "node:fs/promises";
import { PGlite } from "@electric-sql/pglite";
import { describe, expect, it } from "vitest";
import { mapPresentationFields, snapshotMap, type RankedMap } from "@corum-ranked/rules";
import { RankedMapMetadataResolver, explicitLengthSeconds, gddlColor, officialMap,
  parseGdMaps, parseGddl } from "../src/config/map-metadata.js";
import { configDocumentFixture } from "./fixtures.js";
import { PgliteDatabase } from "./pglite-database.js";

const map = (id: number, title = `map-${id}`): RankedMap => ({
  levelId: String(id), canonicalLevelId: String(id), alternateLevelId: String(id),
  playableLevelId: String(id), title, creator: "", difficulty: "", pool: 2,
  qualifyingPercent: 20, active: true,
});
const document = (...maps: RankedMap[]) => ({ ...configDocumentFixture(), maps });
const gd = (id: number, stars: number, demon = false, ticks = 0) =>
  `1:${id}:6:55:8:10:9:50:17:${Number(demon)}:18:${stars}:43:3:15:3:46:99999:57:${ticks}`;
const json = (body: unknown) => new Response(JSON.stringify(body), {headers: {"content-type":"application/json"}});

describe("map display metadata", () => {
  it("uses real star counts and verification seconds, never length categories or editor time", () => {
    const rows = parseGdMaps(`${gd(42,8,false,22800)}|${gd(43,10,true)}#55:creator:1`);
    expect(rows).toEqual([
      {id:"42", creator:"creator", difficulty:"Insane", demon:false, stars:8, verificationSeconds:95},
      {id:"43", creator:"creator", difficulty:"Easy Demon", demon:true, stars:10, verificationSeconds:null},
    ]);
    expect(parseGdMaps("-1")).toEqual([]);
    expect(explicitLengthSeconds("Long(1m~1m59s)")).toBeNull();
    expect(explicitLengthSeconds("1m 35s")).toBe(95);
    expect(explicitLengthSeconds("95s")).toBe(95);
    expect(explicitLengthSeconds("unknown")).toBeNull();
  });

  it("rounds GDDL ratings, falls back to DefaultRating and uses the Integration palette", () => {
    expect(parseGddl({ID:42,Rating:12.6,Meta:{seconds:93.2}},42)).toEqual({tier:13,seconds:93.2,difficulty:"",creator:""});
    expect(parseGddl({ID:42,Rating:null,DefaultRating:5,Meta:{}},42)).toEqual({tier:5,seconds:null,difficulty:"",creator:""});
    expect(parseGddl({ID:41,Rating:20},42)).toBeNull();
    expect(parseGddl({ID:42,Rating:500,Meta:{seconds:-5}},42)).toEqual({tier:null,seconds:null,difficulty:"",creator:""});
    expect(gddlColor(1)).toBe("#dddfee"); expect(gddlColor(13)).toBe("#dd5a5a");
    for (const invalid of [0,-1,41,1.5]) expect(gddlColor(invalid)).toBeNull();
  });

  it("recovers GDDL tier, duration and original difficulty when GD catalog lookup fails", async () => {
    const requests: string[]=[];
    const fetcher: typeof fetch=async input=>{
      const url=String(input);requests.push(url);
      if(url.includes("boomlings")) return new Response("-1");
      return json({ID:56427638,Rating:4.7,Meta:{seconds:116.2,Difficulty:"Easy",Publisher:{name:"Stinger613"}}});
    };
    const source=document(map(56427638,"Light it up"));
    const resolver=new RankedMapMetadataResolver(fetcher);
    const result=await resolver.enrich(source);
    expect(result.maps[0]).toMatchObject({creator:"Stinger613",difficulty:"Easy Demon",
      metadata:{kind:"DEMON",gddlTier:5,gddlColor:"#d4bbe2",durationSeconds:116.2}});
    expect(result.maps[0]!.playableLevelId).toBe("56427638");
    expect(requests).toEqual(["https://www.boomlings.com/database/getGJLevels21.php","https://gdladder.com/api/levels/56427638"]);
    await resolver.enrich(source);expect(requests).toHaveLength(2);
  });

  it("does not erase a cached demon tier during an incomplete GDDL refresh", async () => {
    const client=new PGlite(),database=new PgliteDatabase(client);
    try {
      await client.exec(await readFile(new URL("../../../migrations/0018_map_metadata_cache.sql",import.meta.url),"utf8"));
      const source=document(map(56427638,"Light it up"));
      const fetcher: typeof fetch=async input=>String(input).includes("boomlings") ? new Response("-1") :
        json({ID:56427638,Rating:4.7,Meta:{seconds:116.2,Difficulty:"Easy",Publisher:{name:"Stinger613"}}});
      const first=await new RankedMapMetadataResolver(fetcher,database).enrich(source);
      await database.query("UPDATE ranked_map_metadata_cache SET fetched_at=CURRENT_TIMESTAMP-INTERVAL '2 days'");
      const incomplete: typeof fetch=async input=>String(input).includes("boomlings") ? new Response("-1") : json({ID:56427638,Rating:null});
      const refreshed=await new RankedMapMetadataResolver(incomplete,database).enrich(source);
      expect(refreshed.maps).toEqual(first.maps);
    } finally {await client.close();}
  },30_000);

  it("stops failed GD batching so a large catalog retains time for independent GDDL recovery", async () => {
    let gdCalls=0,gddlCalls=0;
    const fetcher: typeof fetch=async input=>{
      const url=String(input);
      if(url.includes("boomlings")){++gdCalls;return new Response("-1");}
      ++gddlCalls;const id=Number(url.split("/").at(-1));
      return json({ID:id,Rating:3.4,Meta:{Difficulty:"Easy",seconds:80}});
    };
    const result=await new RankedMapMetadataResolver(fetcher).enrich(document(...Array.from({length:120},(_,i)=>map(1_000+i))));
    expect(gdCalls).toBe(1);expect(gddlCalls).toBe(120);
    expect(result.maps.every(m=>m.metadata?.kind==="DEMON"&&m.metadata.gddlTier===3)).toBe(true);
  });

  it("uses official difficulty stars and official GDDL IDs without changing playable copy IDs", async () => {
    const requests: string[] = [];
    const fetcher: typeof fetch = async input => {
      const url = String(input); requests.push(url);
      const id = Number(url.split("/").at(-1));
      return json({ID:id,Rating: id===3 ? 5.4 : 3.2,Meta:{seconds: id===1 ? 86 : 99}});
    };
    const source = document(map(77238470,"xStep"),map(77239595,"Blast Processing"),
      map(77238488,"Clubstep"),map(77239599,"Theory of Everything II"),map(77240630,"Deadlocked"));
    const enriched = await new RankedMapMetadataResolver(fetcher).enrich(source);
    expect(enriched.maps[0]!.metadata).toMatchObject({kind:"NORMAL",stars:8,officialLevelId:10,durationSeconds:84});
    expect(enriched.maps[1]!.metadata).toMatchObject({stars:6,officialLevelId:17});
    expect(enriched.maps.slice(2).map(m=>m.metadata?.gddlTier)).toEqual([3,3,5]);
    expect(enriched.maps[2]!.metadata).toMatchObject({kind:"DEMON",stars:null,gddlColor:"#d3cbe7",durationSeconds:86});
    expect(requests.sort()).toEqual([1,2,3].map(id=>`https://gdladder.com/api/levels/${id}`));
    expect(enriched.maps.map(m=>m.playableLevelId)).toEqual(source.maps.map(m=>m.playableLevelId));
    expect(source.maps.every(m=>m.metadata===undefined)).toBe(true);
    expect(enriched.generation).not.toBe(source.generation);
    expect(enriched.operational.generation).toBe(enriched.generation);
    expect(source.operational.generation).toBe(source.generation);
    expect(officialMap("unknown")).toBeUndefined();
  });

  it("enriches ordinary/demon IDs, uses an exact download duration and leaves unresolved durations unknown", async () => {
    const requests: string[] = [];
    const fetcher: typeof fetch = async (input, init) => {
      const url=String(input); requests.push(url);
      if (url.includes("getGJLevels")) {
        const body = new URLSearchParams(String(init?.body));
        expect(body.get("type")).toBe("19"); expect(body.get("str")).toBe("42,43,44");
        return new Response(`${gd(42,8,false,22800)}|${gd(43,10,true)}|${gd(44,6)}#55:creator:1`);
      }
      if (url.includes("gdladder")) return json({ID:43,Rating:null,DefaultRating:16,Meta:{seconds:90.5}});
      expect(new URLSearchParams(String(init?.body)).get("levelID")).toBe("44");
      return new Response(gd(44,6)); // Older verification has no exact time.
    };
    const source=document(map(42),map(43),map(44));
    const resolver=new RankedMapMetadataResolver(fetcher);
    const enriched=await resolver.enrich(source);
    expect(enriched.maps[0]).toMatchObject({creator:"creator",metadata:{kind:"NORMAL",stars:8,durationSeconds:95}});
    expect(enriched.maps[1]!.metadata).toMatchObject({kind:"DEMON",stars:null,gddlTier:16,gddlColor:"#da4534",durationSeconds:90.5});
    expect(enriched.maps[2]!.metadata?.durationSeconds).toBeNull();
    expect(requests).toHaveLength(3);
    await resolver.enrich(source); expect(requests).toHaveLength(3);
    const snap=snapshotMap(enriched.maps[1]!);
    expect(snap.metadata).toEqual(enriched.maps[1]!.metadata);
    expect(snap.metadata).not.toBe(enriched.maps[1]!.metadata);
    expect(Object.isFrozen(snap.metadata)).toBe(true);
    const fields=mapPresentationFields(enriched.maps,"43");
    expect(fields.metadata).toEqual(snap.metadata);
    expect(fields.metadata).not.toBe(enriched.maps[1]!.metadata);
    expect(mapPresentationFields(enriched.maps,"999")).toEqual({});
    expect(enriched.maps.map(m=>[m.playableLevelId,m.pool,m.qualifyingPercent]))
      .toEqual(source.maps.map(m=>[m.playableLevelId,m.pool,m.qualifyingPercent]));
  });

  it("recovers exact verification duration from a download and honors optional exact length overrides", async () => {
    const fetcher: typeof fetch = async input => new Response(String(input).includes("getGJLevels")
      ? gd(42,7) : gd(42,7,false,22320));
    const resolver=new RankedMapMetadataResolver(fetcher);
    const first=await resolver.enrich(document(map(42)));
    expect(first.maps[0]!.metadata?.durationSeconds).toBe(93);
    expect((await resolver.enrich(document(map(42)))).generation).toBe(first.generation);
    const override=await resolver.enrich(document({...map(42),length:"1m 35s"}));
    expect(override.maps[0]!.metadata?.durationSeconds).toBe(95);
    expect(override.generation).not.toBe(first.generation);
  });

  it("persists its cache across server restarts and preserves measured official duration on refresh failure", async () => {
    const client=new PGlite(), database=new PgliteDatabase(client);
    try {
      const migration=await readFile(new URL("../../../migrations/0018_map_metadata_cache.sql",import.meta.url),"utf8");
      await client.exec(migration); await client.exec(migration);
      let calls=0;
      const online: typeof fetch=async()=>{++calls; return json({ID:1,Rating:3.2,Meta:{seconds:86}});};
      const source=document(map(77238488,"Clubstep"));
      const first=await new RankedMapMetadataResolver(online,database).enrich(source);
      expect(calls).toBe(1);
      const offline: typeof fetch=async()=>{++calls;throw new Error("offline");};
      const restored=await new RankedMapMetadataResolver(offline,database).enrich(source);
      expect(restored.maps).toEqual(first.maps); expect(calls).toBe(1);
      await database.query("UPDATE ranked_map_metadata_cache SET fetched_at=CURRENT_TIMESTAMP-INTERVAL '2 days'");
      const stale=await new RankedMapMetadataResolver(offline,database).enrich(source);
      expect(stale.maps).toEqual(first.maps); expect(calls).toBe(2);
    } finally { await client.close(); }
  },30_000);

  it("bounds network enrichment and keeps the catalog playable when external services fail", async () => {
    const fetcher: typeof fetch=(_input,init)=>new Promise((_resolve,reject)=>
      init?.signal?.addEventListener("abort",()=>reject(new Error("aborted")),{once:true}));
    const source=document(map(42));
    const start=Date.now(), enriched=await new RankedMapMetadataResolver(fetcher,undefined,15).enrich(source);
    expect(Date.now()-start).toBeLessThan(1_000);
    expect(enriched.maps[0]).toMatchObject({...map(42),metadata:{kind:"UNKNOWN",durationSeconds:null}});
  });
});
