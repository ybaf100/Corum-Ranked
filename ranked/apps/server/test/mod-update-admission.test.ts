import "reflect-metadata";
import { Test } from "@nestjs/testing";
import type { INestApplication } from "@nestjs/common";
import { afterAll, beforeAll, describe, expect, it, vi } from "vitest";
import { SERVER_ENVIRONMENT } from "../src/config/server-environment.js";
import { parseRankedModUpdates } from "../src/config/ranked-mod-updates.js";
import { SERVER_CLOCK } from "../src/common/runtime.module.js";
import { TokenService } from "../src/common/token.service.js";
import { DATABASE } from "../src/database/database.port.js";
import { SessionGuard } from "../src/session/session.guard.js";
import { AdmissionVersionGuard } from "../src/session/admission-version.guard.js";
import { SessionController } from "../src/session/session.controller.js";
import { SessionService } from "../src/session/session.service.js";
import { QueueController } from "../src/queue/queue.controller.js";
import { QueueService } from "../src/queue/queue.service.js";
import { environmentFixture } from "./fixtures.js";
import { REQUIRED_RANKED_CLIENT_VERSION } from "../src/session/client-version.js";

const version = REQUIRED_RANKED_CLIENT_VERSION;
const nextVersion = version.replace(/\d+$/, value => String(Number(value) + 1));
const mods = (v = version) => [{id:"hwanhee1.corum_ranked",version:v,enabled:true,loaded:true}];
describe("pinned channel update configuration", () => {
  it("keeps channel versions independent and normalizes hashes", () => {
    expect(parseRankedModUpdates(undefined)).toEqual({});
    expect(parseRankedModUpdates(JSON.stringify({ debug: {version,sha256:"A".repeat(64),sizeBytes:100},
      release: {version:"v0.5.0-beta.55",sha256:"b".repeat(64),sizeBytes:200} })))
      .toMatchObject({debug:{version,sha256:"a".repeat(64)},release:{version:"v0.5.0-beta.55"}});
  });
  it.each(["null", "[]", '{"latest":{}}', '{"debug":{"version":"../bad"}}',
    JSON.stringify({release:{version,sha256:"a".repeat(64),sizeBytes:128*1024*1024+1}})])
    ("rejects unsafe update metadata: %s", text => { expect(() => parseRankedModUpdates(text)).toThrow(); });
  it("keeps the server-selected HTTPS package URL in the channel descriptor",()=>{
    const downloadUrl=`https://downloads.example.org/ranked/debug-${version}/Corum-Ranked-${version}-debug.geode`;
    expect(parseRankedModUpdates(JSON.stringify({debug:{version,sha256:"a".repeat(64),sizeBytes:100,downloadUrl}})).debug?.downloadUrl)
      .toBe(downloadUrl);
  });
  it.each(["http://example.org/mod.geode","https://user:password@example.org/mod.geode","https://example.org/mod.geode#fragment"])
    ("rejects unsafe package URL %s",downloadUrl=>{
      expect(()=>parseRankedModUpdates(JSON.stringify({debug:{version,sha256:"a".repeat(64),sizeBytes:100,downloadUrl}}))).toThrow();
    });
});

describe("HTTP admission checks before matchmaking, with uninterrupted admitted sessions", () => {
  let app: INestApplication, base: string;
  const environment = {...environmentFixture(),rankedModUpdates:{debug:{version,sha256:"a".repeat(64),sizeBytes:100},
    release:{version:"v0.5.0-beta.55",sha256:"b".repeat(64),sizeBytes:100}}};
  const tokens = new TokenService(environment);
  const join = vi.fn(() => ({status:"QUEUED"}));
  const heartbeat = vi.fn(() => ({status:"MATCHED"}));
  const create = vi.fn(() => ({sessionToken:"new"}));
  beforeAll(async () => {
    const database = {query: vi.fn(async (_sql: string, params: readonly unknown[]) => ({ rows:
      params[0] === tokens.hash("admitted-token") ? [{session_id:"session",player_id:"player",gd_account_id:"1",
        gd_username:"Player",displayed_tier:"RED",hidden_mmr:300,placement_games:5,client_version:version,build_channel:"debug"}] : [] }))};
    const module = await Test.createTestingModule({controllers:[SessionController,QueueController],providers:[
      SessionGuard,AdmissionVersionGuard,{provide:DATABASE,useValue:database},{provide:TokenService,useValue:tokens},
      {provide:SERVER_CLOCK,useValue:{now:()=>new Date()}},{provide:SERVER_ENVIRONMENT,useValue:environment},
      {provide:SessionService,useValue:{create,heartbeat}},{provide:QueueService,useValue:{join,heartbeat}}
    ]}).compile();
    app=module.createNestApplication();app.useLogger(false);await app.listen(0,"127.0.0.1");base=await app.getUrl();
  });
  afterAll(async()=>{await app?.close();});
  const post = (path: string, body: unknown) => fetch(base+"/api/ranked/"+path,{method:"POST",
    headers:{"content-type":"application/json",authorization:"Bearer admitted-token"},body:JSON.stringify(body)});
  it("uses the selected channel when creating a lobby session", async () => {
    const body={gdAccountId:"1",gdUsername:"Player",clientVersion:version,clientBuildChannel:"debug",installedMods:mods()};
    expect((await post("session",body)).status).toBe(201);
    expect((await post("session",{...body,clientBuildChannel:"release"})).status).toBe(426);
    expect(create).toHaveBeenCalledTimes(1);
  });
  it("rejects a stale queue join before mutation but keeps old-session heartbeats alive", async () => {
    expect((await post("queue/join",{installedMods:mods()})).status).toBe(201);
    environment.rankedModUpdates.debug.version=nextVersion;
    const stale=await post("queue/join",{installedMods:mods()});
    expect(stale.status).toBe(426);
    expect(await stale.json()).toMatchObject({requiredVersion:nextVersion});
    expect(join).toHaveBeenCalledTimes(1);
    expect((await post("queue/heartbeat",{})).status).toBe(201);
    expect((await post("session/heartbeat",{})).status).toBe(201);
    expect((await post("queue/join",{installedMods:"invalid"})).status).toBe(426);
  });
});
