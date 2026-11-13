import { describe, expect, it, vi } from "vitest";
import { CLIENT_IP_HEADER, clientAddress, isLocalPeer } from "../src/common/client-ip.js";
import {
  GENERAL_TIER, RequestRateLimiter, SESSION_TIER, UPLOAD_TIER, rateKey, rateTiersFor, requestRateLimitMiddleware,
} from "../src/common/request-rate-limit.js";

const viaEdge = (ip: string, peer = "172.17.0.1") => ({ headers: { [CLIENT_IP_HEADER]: ip }, socket: { remoteAddress: peer } });

describe("client address behind the Caddy edge", () => {
  it("trusts the edge header only from the host's loopback or Docker bridge", () => {
    for (const peer of ["127.0.0.1", "::1", "::ffff:127.0.0.1", "172.17.0.1", "10.0.0.2", "192.168.1.5"]) expect(isLocalPeer(peer)).toBe(true);
    for (const peer of ["203.0.113.9", "172.32.0.1", "2606:4700::1", undefined]) expect(isLocalPeer(peer)).toBe(false);
    expect(clientAddress(viaEdge("198.51.100.7"))).toBe("198.51.100.7");
    expect(clientAddress(viaEdge("198.51.100.7", "203.0.113.9"))).toBe("203.0.113.9");
    expect(clientAddress(viaEdge("not-an-ip"))).toBe("172.17.0.1");
    expect(clientAddress({ socket: { remoteAddress: "::ffff:203.0.113.9" } })).toBe("203.0.113.9");
    expect(clientAddress({})).toBe("unknown");
  });
});

describe("per-client request budgets", () => {
  it("groups IPv6 clients by /64 and keeps IPv4 addresses apart", () => {
    expect(rateKey("2001:db8:1:2:aaaa::1")).toBe("2001:db8:1:2::/64");
    expect(rateKey("2001:db8:1:2:bbbb:cccc:dddd:eeee")).toBe("2001:db8:1:2::/64");
    expect(rateKey("2001:db8::1")).toBe("2001:db8:0:0::/64");
    expect(rateKey("198.51.100.7")).toBe("198.51.100.7");
  });

  it("puts session creation and diagnostic uploads in their own tiers", () => {
    expect(rateTiersFor("POST", "/api/ranked/session")).toEqual([GENERAL_TIER, SESSION_TIER]);
    expect(rateTiersFor("POST", "/api/ranked/session/?x=1")).toEqual([GENERAL_TIER, SESSION_TIER]);
    expect(rateTiersFor("POST", "/api/ranked/debug/logs")).toEqual([GENERAL_TIER, UPLOAD_TIER]);
    expect(rateTiersFor("GET", "/api/ranked/session/standing")).toEqual([GENERAL_TIER]);
  });

  it("allows a burst, refuses beyond it and refills at the sustained rate", () => {
    let now = 0;
    const limiter = new RequestRateLimiter(1_000, () => now);
    for (let i = 0; i < SESSION_TIER.burst; i++) expect(limiter.take("198.51.100.7", [SESSION_TIER])).toBe(0);
    expect(limiter.take("198.51.100.7", [SESSION_TIER])).toBe(10);
    expect(limiter.take("198.51.100.8", [SESSION_TIER])).toBe(0);
    now += 10_000;
    expect(limiter.take("198.51.100.7", [SESSION_TIER])).toBe(0);
    expect(limiter.take("198.51.100.7", [SESSION_TIER])).toBeGreaterThan(0);
  });

  it("does not spend the general budget on a request another tier refuses", () => {
    const limiter = new RequestRateLimiter(1_000, () => 0);
    for (let i = 0; i < UPLOAD_TIER.burst; i++) limiter.take("a", [GENERAL_TIER, UPLOAD_TIER]);
    for (let i = 0; i < 50; i++) expect(limiter.take("a", [GENERAL_TIER, UPLOAD_TIER])).toBeGreaterThan(0);
    let allowed = 0;
    while (limiter.take("a", [GENERAL_TIER]) === 0) allowed++;
    expect(allowed).toBe(GENERAL_TIER.burst - UPLOAD_TIER.burst);
  });

  it("stays bounded under many distinct clients", () => {
    const limiter = new RequestRateLimiter(100, () => 0);
    for (let i = 0; i < 1_000; i++) limiter.take(`10.0.${i >> 8}.${i & 255}`, [GENERAL_TIER]);
    expect(limiter.size).toBeLessThanOrEqual(100);
  });

  it("answers 429 with Retry-After before routing", () => {
    const limiter = new RequestRateLimiter(1_000, () => 0);
    const middleware = requestRateLimitMiddleware(limiter);
    const response = { status: vi.fn(), setHeader: vi.fn(), json: vi.fn() };
    response.status.mockReturnValue(response);
    const next = vi.fn();
    const request = { method: "POST", url: "/api/ranked/session", ...viaEdge("198.51.100.7") };
    for (let i = 0; i <= SESSION_TIER.burst; i++) middleware(request, response, next);
    expect(next).toHaveBeenCalledTimes(SESSION_TIER.burst);
    expect(response.status).toHaveBeenCalledWith(429);
    expect(response.setHeader).toHaveBeenCalledWith("Retry-After", "10");
    expect(response.json).toHaveBeenCalledWith(expect.objectContaining({ code: "RATE_LIMITED" }));
    middleware({ ...request, ...viaEdge("198.51.100.8") }, response, next);
    expect(next).toHaveBeenCalledTimes(SESSION_TIER.burst + 1);
  });
});
