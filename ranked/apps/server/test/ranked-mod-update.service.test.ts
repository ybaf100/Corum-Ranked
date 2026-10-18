import "reflect-metadata";
import { Logger } from "@nestjs/common";
import { Test } from "@nestjs/testing";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { RankedConfigController } from "../src/config/ranked-config.controller.js";
import { RankedConfigService } from "../src/config/ranked-config.service.js";
import { RankedModUpdateService, RANKED_MOD_UPDATE_FETCH } from "../src/config/ranked-mod-update.service.js";
import { SERVER_ENVIRONMENT } from "../src/config/server-environment.js";
import { rankedModDownloadUrl, type RankedBuildChannel } from "../src/config/ranked-mod-updates.js";
import { REQUIRED_RANKED_CLIENT_VERSION } from "../src/session/client-version.js";
import { configDocumentFixture, environmentFixture as baseEnvironmentFixture } from "./fixtures.js";

const version = REQUIRED_RANKED_CLIENT_VERSION;
const base="https://downloads.example.org";
const environmentFixture=()=>({...baseEnvironmentFixture(),rankedDownloadBaseUrl:base});
const descriptor = (channel: RankedBuildChannel, target = version) => ({
  [channel]: { version: target, sha256: (channel === "debug" ? "a" : "b").repeat(64), sizeBytes: 12345,
    downloadUrl:rankedModDownloadUrl(base,channel,target) },
});
const validFetch = () => vi.fn<typeof fetch>().mockImplementation(async (url,options) =>
  options?.method==="HEAD"?new Response(null,{headers:{"content-length":"12345"}}):Response.json(descriptor(String(url).includes("/debug-") ? "debug" : "release")));

beforeEach(() => vi.spyOn(Logger.prototype, "warn").mockImplementation(() => {}));
afterEach(() => { vi.restoreAllMocks(); vi.useRealTimers(); });

describe("server-selected mod update descriptors", () => {
  it("serves the identical verified bytes through a configured CDN origin for both channels without rewriting immutable descriptors",async()=>{
    const cdn="https://cdn.example.org",fetchRemote=validFetch();
    const service=new RankedModUpdateService({...environmentFixture(),rankedDownloadCdnBaseUrl:cdn},fetchRemote);
    const updates=await service.getClientUpdates();
    for(const channel of ["debug","release"] as const){
      expect(updates[channel]).toEqual({...descriptor(channel)[channel],downloadUrl:rankedModDownloadUrl(cdn,channel,version)});
    }
    expect(fetchRemote.mock.calls.every(([url])=>String(url).startsWith(base+"/"))).toBe(true);
  });
  it("a CDN alias cannot make a wrong-origin or wrong-channel remote descriptor acceptable",async()=>{
    const fetchRemote=validFetch();
    fetchRemote.mockImplementationOnce(async()=>Response.json({debug:{...descriptor("debug").debug,downloadUrl:"https://untrusted.example.org/wrong.geode"}}));
    const service=new RankedModUpdateService({...environmentFixture(),rankedDownloadCdnBaseUrl:"https://cdn.example.org"},fetchRemote);
    expect(await service.getClientUpdates()).not.toHaveProperty("debug");
  });
  it("automatically resolves both exact R2 channel packages without manual JSON configuration", async () => {
    const fetchRemote = validFetch();
    const service = new RankedModUpdateService(environmentFixture(), fetchRemote);
    expect(await service.getClientUpdates()).toEqual({ ...descriptor("debug"), ...descriptor("release") });
    expect(fetchRemote.mock.calls.map(([url]) => String(url))).toEqual([
      `${base}/ranked/debug-${version}/debug.json`,
      `${base}/ranked/release-${version}/release.json`,
    ]);
    for (const [, options] of fetchRemote.mock.calls) {
      expect(options?.signal).toBeInstanceOf(AbortSignal);
      expect(options?.headers).toEqual({ accept: "application/json" });
    }
  });

  it("does not fall back to GitHub Releases before the R2 origin is configured",async()=>{
    const fetchRemote=validFetch();
    const service=new RankedModUpdateService(baseEnvironmentFixture(),fetchRemote);
    expect(await service.getClientUpdates()).toEqual({});expect(fetchRemote).not.toHaveBeenCalled();
  });

  it("adds the configured R2 package URL to legacy manual pins",async()=>{
    const pin={version:"v0.5.0-beta.56",sha256:"a".repeat(64),sizeBytes:12345};
    const fetchRemote=validFetch();
    const service=new RankedModUpdateService({...environmentFixture(),rankedModUpdates:{debug:pin}},fetchRemote);
    expect((await service.getClientUpdates()).debug).toEqual({...pin,
      downloadUrl:rankedModDownloadUrl(base,"debug",pin.version)});
  });

  it("keeps explicit channel pins authoritative and resolves only an unspecified channel", async () => {
    const explicit = descriptor("debug", "v0.5.0-beta.56");
    const fetchRemote = validFetch();
    const service = new RankedModUpdateService({ ...environmentFixture(), rankedModUpdates: explicit }, fetchRemote);
    expect(await service.getClientUpdates()).toEqual({ ...explicit, ...descriptor("release") });
    expect(fetchRemote).toHaveBeenCalledTimes(1);
    expect(String(fetchRemote.mock.calls[0]![0])).toContain(`/release-${version}/release.json`);
  });

  it("does no network lookup when both channels are explicitly configured", async () => {
    const pins = { ...descriptor("debug", "v0.5.0-beta.56"), ...descriptor("release", "v0.5.0-beta.55") };
    const fetchRemote = validFetch();
    const service = new RankedModUpdateService({ ...environmentFixture(), rankedModUpdates: pins }, fetchRemote);
    expect(await service.getClientUpdates()).toEqual(pins);
    expect(fetchRemote).not.toHaveBeenCalled();
  });

  it("warms the cache without delaying startup and shares concurrent lookups", async () => {
    let finish!: () => void;
    const gate = new Promise<void>((resolve) => { finish = resolve; });
    const fetchRemote = vi.fn<typeof fetch>().mockImplementation(async (url) => {
      await gate;
      return Response.json(descriptor(String(url).includes("/debug-") ? "debug" : "release"));
    });
    const service = new RankedModUpdateService(environmentFixture(), fetchRemote);
    expect(service.onApplicationBootstrap()).toBeUndefined();
    const first = service.getClientUpdates(), second = service.getClientUpdates();
    expect(fetchRemote).toHaveBeenCalledTimes(2);
    finish();
    expect(await first).toEqual(await second);
    const isolated = await service.getClientUpdates();
    (isolated.debug as { version: string }).version = "modified by a caller";
    expect(await service.getClientUpdates()).toEqual({ ...descriptor("debug"), ...descriptor("release") });
    expect(fetchRemote).toHaveBeenCalledTimes(2);
  });

  it("retries unpublished releases after 20 seconds and keeps a valid channel available", async () => {
    vi.useFakeTimers(); vi.setSystemTime(0);
    const fetchRemote = validFetch();
    fetchRemote.mockImplementationOnce(async () => new Response("Not Found", { status: 404 }));
    const service = new RankedModUpdateService(environmentFixture(), fetchRemote);
    expect(await service.getClientUpdates()).toEqual(descriptor("release"));
    expect(await service.getClientUpdates()).toEqual(descriptor("release"));
    expect(fetchRemote).toHaveBeenCalledTimes(2);
    vi.setSystemTime(20_000);
    expect(await service.getClientUpdates()).toEqual({ ...descriptor("debug"), ...descriptor("release") });
    expect(fetchRemote).toHaveBeenCalledTimes(3);
  });

  it.each([
    ["wrong version", () => Response.json(descriptor("debug", "v0.5.0-beta.999"))],
    ["wrong channel", () => Response.json(descriptor("release"))],
    ["mixed channels", () => Response.json({ ...descriptor("debug"), ...descriptor("release") })],
    ["invalid digest", () => Response.json({ debug: { version, sha256: "bad", sizeBytes: 12345 } })],
    ["invalid size", () => Response.json({ debug: { version, sha256: "a".repeat(64), sizeBytes: 0 } })],
    ["wrong package origin", () => Response.json({debug:{...descriptor("debug").debug,
      downloadUrl:"https://another.example.org/not-this-package.geode"}})],
    ["wrong package channel", () => Response.json({debug:{...descriptor("debug").debug,
      downloadUrl:rankedModDownloadUrl(base,"release",version)}})],
    ["invalid JSON", () => new Response("<html>not a descriptor</html>")],
    ["oversized header", () => new Response("{}", { headers: { "content-length": "4097" } })],
    ["oversized body", () => new Response(" ".repeat(4097))],
  ] as const)("omits an invalid %s descriptor without blocking the other channel", async (_label, response) => {
    const fetchRemote = validFetch();
    fetchRemote.mockImplementationOnce(async () => response());
    const service = new RankedModUpdateService(environmentFixture(), fetchRemote);
    expect(await service.getClientUpdates()).toEqual(descriptor("release"));
    expect(fetchRemote).toHaveBeenCalledTimes(2);
  });

  it("handles network/timeout errors without rejecting public configuration", async () => {
    const fetchRemote = vi.fn<typeof fetch>().mockRejectedValue(new Error("request aborted"));
    const service = new RankedModUpdateService(environmentFixture(), fetchRemote);
    expect(await service.getClientUpdates()).toEqual({});
    expect(await service.getClientUpdates()).toEqual({});
    expect(fetchRemote).toHaveBeenCalledTimes(2);
  });

  it("returns resolved descriptors through the actual public config HTTP endpoint", async () => {
    const document = configDocumentFixture();
    const module = await Test.createTestingModule({
      controllers: [RankedConfigController],
      providers: [
        RankedModUpdateService,
        { provide: RankedConfigService, useValue: { getSnapshot: () => ({ ...document, fetchedAt: new Date().toISOString() }) } },
        { provide: SERVER_ENVIRONMENT, useValue: environmentFixture() },
        { provide: RANKED_MOD_UPDATE_FETCH, useValue: validFetch() },
      ],
    }).compile();
    const app = module.createNestApplication(); app.useLogger(false);
    try {
      await app.listen(0, "127.0.0.1");
      const response = await fetch(`${await app.getUrl()}/api/ranked/config`);
      expect(response.ok).toBe(true);
      expect(response.headers.get("cache-control")).toBe("no-store");
      expect(await response.json()).toMatchObject({
        requiredClientVersion: version,
        modUpdates: { ...descriptor("debug"), ...descriptor("release") },
      });
    } finally { await app.close(); }
  });
});
