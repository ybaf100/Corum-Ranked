import { HttpException } from "@nestjs/common";
import { describe, expect, it, vi } from "vitest";
import { loadArgonEnvironment, type ServerEnvironment } from "../src/config/server-environment.js";
import { ArgonService } from "../src/session/argon.service.js";
import { SessionController } from "../src/session/session.controller.js";
import type { CreateSessionDto } from "../src/session/session.dto.js";
import type { SessionService } from "../src/session/session.service.js";
import { REQUIRED_RANKED_CLIENT_VERSION } from "../src/session/client-version.js";

const TOKEN = "argon-token-0123456789abcdef";
const environment = (mode: "required" | "off" = "required") =>
  ({ argon: { mode, baseUrl: "https://argon.example" } }) as unknown as ServerEnvironment;
const reply = (status: number, body: unknown) =>
  new Response(typeof body === "string" ? body : JSON.stringify(body), { status, headers: { "content-type": "application/json" } });
const failure = async (promise: Promise<unknown>) => {
  try { await promise; } catch (error) {
    expect(error).toBeInstanceOf(HttpException);
    const http = error as HttpException;
    return { status: http.getStatus(), body: http.getResponse() as Record<string, unknown> };
  }
  throw new Error("expected a rejection");
};

describe("Argon GD account ownership", () => {
  it("defaults parsed deployments to required and rejects non-HTTPS servers", () => {
    expect(loadArgonEnvironment({})).toEqual({ mode: "required", baseUrl: "https://argon.globed.dev" });
    expect(loadArgonEnvironment({ RANKED_ARGON_MODE: "off" }).mode).toBe("off");
    expect(loadArgonEnvironment({ RANKED_ARGON_URL: "http://localhost:4341" }).baseUrl).toBe("http://localhost:4341");
    expect(() => loadArgonEnvironment({ RANKED_ARGON_URL: "http://argon.example" })).toThrow();
    expect(() => loadArgonEnvironment({ RANKED_ARGON_MODE: "maybe" })).toThrow();
  });

  it("uses the username stored in the token and caches the verified pair", async () => {
    const fetchRemote = vi.fn(async (url: URL | RequestInfo) => {
      const query = new URL(String(url)).searchParams;
      expect(new URL(String(url)).pathname).toBe("/v1/validation/check-strong");
      expect(query.get("account_id")).toBe("7001");
      expect(query.get("authtoken")).toBe(TOKEN);
      return reply(200, { valid: false, valid_weak: true, username: "RealName" });
    });
    const argon = new ArgonService(environment(), fetchRemote as unknown as typeof fetch);
    await expect(argon.verify("7001", "SpoofedName", TOKEN)).resolves.toEqual({ accountId: "7001", username: "RealName", verified: true });
    await expect(argon.verify("7001", "SpoofedName", TOKEN)).resolves.toMatchObject({ username: "RealName" });
    expect(fetchRemote).toHaveBeenCalledTimes(1);
  });

  it("refuses missing, malformed and impersonating tokens", async () => {
    const fetchRemote = vi.fn(async () => reply(200, { valid: false, valid_weak: false, cause: "mismatched account id" }));
    const argon = new ArgonService(environment(), fetchRemote as unknown as typeof fetch);
    expect(await failure(argon.verify("7001", "Name", undefined))).toMatchObject({ status: 401, body: { code: "ARGON_TOKEN_REQUIRED" } });
    expect(await failure(argon.verify("7001", "Name", "short"))).toMatchObject({ status: 401, body: { code: "ARGON_TOKEN_INVALID" } });
    expect(await failure(argon.verify("7001", "Name", TOKEN))).toMatchObject({
      status: 401, body: { code: "ARGON_TOKEN_INVALID", cause: "mismatched account id" },
    });
  });

  it("fails closed with 503 when Argon is unreachable or rate limited", async () => {
    for (const fetchRemote of [vi.fn(async () => reply(429, "slow down")), vi.fn(async () => { throw new Error("offline"); })]) {
      const argon = new ArgonService(environment(), fetchRemote as unknown as typeof fetch);
      expect(await failure(argon.verify("7001", "Name", TOKEN))).toMatchObject({ status: 503, body: { code: "ARGON_UNAVAILABLE" } });
    }
  });

  it("passes the claim through only when explicitly switched off", async () => {
    const fetchRemote = vi.fn();
    const argon = new ArgonService(environment("off"), fetchRemote as unknown as typeof fetch);
    await expect(argon.verify("7001", "Claimed", undefined)).resolves.toEqual({ accountId: "7001", username: "Claimed", verified: false });
    expect(fetchRemote).not.toHaveBeenCalled();
  });

  it("verifies before the session is created and never forwards the token", async () => {
    const create = vi.fn(async () => ({ ok: true }));
    const verify = vi.fn(async () => ({ accountId: "7001", username: "RealName", verified: true }));
    const controller = new SessionController({ create } as unknown as SessionService, undefined, undefined,
      { verify } as unknown as ArgonService);
    const body: CreateSessionDto = {
      gdAccountId: "7001", gdUsername: "SpoofedName", argonToken: TOKEN, clientVersion: REQUIRED_RANKED_CLIENT_VERSION,
      clientInstanceId: "test-process-instance",
      installedMods: [{ id: "hwanhee1.corum_ranked", version: REQUIRED_RANKED_CLIENT_VERSION, enabled: true, loaded: true, internal: false, system: false }],
    };
    await controller.create(body);
    expect(verify).toHaveBeenCalledWith("7001", "SpoofedName", TOKEN);
    const forwarded = (create.mock.calls[0] as unknown[])[0] as Record<string, unknown>;
    expect(forwarded.gdUsername).toBe("RealName");
    expect(forwarded).not.toHaveProperty("argonToken");
  });
});
