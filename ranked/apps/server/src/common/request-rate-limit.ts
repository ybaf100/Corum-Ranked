import { isIP } from "node:net";
import { clientAddress, type ClientAddressSource } from "./client-ip.js";

/**
 * In-process per-client request budgets, applied before routing so a single
 * flooding client is refused before it reaches Neon, Argon or the GD servers.
 * Volumetric attacks are absorbed by Cloudflare in front of the server; this
 * only bounds what an individual address can make the application do.
 */
export interface RateTier {
  readonly name: string;
  /** Sustained requests per second. */
  readonly ratePerSecond: number;
  /** Requests allowed at once before the sustained rate applies. */
  readonly burst: number;
}

/**
 * The general tier is generous: one player publishes spectator poses every
 * 50 ms and several players may share a NAT address (PC rooms, dormitories).
 */
export const GENERAL_TIER: RateTier = { name: "general", ratePerSecond: 60, burst: 600 };
/**
 * Session creation can call Argon, whose public rate limit (750/hour) is shared
 * by every player; players create sessions only when entering the lobby.
 */
export const SESSION_TIER: RateTier = { name: "session", ratePerSecond: 0.1, burst: 10 };
/** Diagnostic uploads are large and rare. */
export const UPLOAD_TIER: RateTier = { name: "upload", ratePerSecond: 0.1, burst: 6 };

export const rateTiersFor = (method: string, path: string): readonly RateTier[] => {
  const route = path.split("?")[0]!.replace(/\/+$/, "");
  if (method === "POST" && route === "/api/ranked/session") return [GENERAL_TIER, SESSION_TIER];
  if (method === "POST" && route === "/api/ranked/debug/logs") return [GENERAL_TIER, UPLOAD_TIER];
  return [GENERAL_TIER];
};

/** IPv6 clients usually own a whole /64, so they share one budget per prefix. */
export const rateKey = (address: string): string => {
  if (isIP(address) !== 6) return address;
  const [head = "", tail = ""] = address.toLowerCase().split("::");
  const left = head ? head.split(":") : [];
  const right = tail ? tail.split(":") : [];
  const groups = address.includes("::") ? [...left, ...Array(Math.max(0, 8 - left.length - right.length)).fill("0"), ...right] : left;
  return `${groups.slice(0, 4).map((group) => group.replace(/^0+(?=.)/, "")).join(":")}::/64`;
};

interface Bucket { tokens: number; at: number }

export class RequestRateLimiter {
  private readonly buckets = new Map<string, Bucket>();

  public constructor(private readonly maxKeys = 100_000, private readonly now: () => number = Date.now) {}

  /** Returns 0 when allowed, otherwise the seconds to wait. All tiers must have a token. */
  public take(address: string, tiers: readonly RateTier[]): number {
    const at = this.now();
    const key = rateKey(address);
    const buckets = tiers.map((tier) => [tier, this.bucket(`${tier.name}|${key}`, tier, at)] as const);
    const blocked = buckets.find(([, bucket]) => bucket.tokens < 1);
    if (blocked) {
      const [tier, bucket] = blocked;
      return Math.max(1, Math.ceil((1 - bucket.tokens) / tier.ratePerSecond));
    }
    for (const [, bucket] of buckets) bucket.tokens -= 1;
    return 0;
  }

  public get size(): number { return this.buckets.size; }

  private bucket(id: string, tier: RateTier, at: number): Bucket {
    let bucket = this.buckets.get(id);
    if (bucket) {
      bucket.tokens = Math.min(tier.burst, bucket.tokens + ((at - bucket.at) / 1_000) * tier.ratePerSecond);
      bucket.at = at;
      // Refresh recency so eviction removes the least recently active clients.
      this.buckets.delete(id);
      this.buckets.set(id, bucket);
      return bucket;
    }
    if (this.buckets.size >= this.maxKeys) {
      for (const [old] of this.buckets) {
        this.buckets.delete(old);
        if (this.buckets.size < this.maxKeys * 0.9) break;
      }
    }
    bucket = { tokens: tier.burst, at };
    this.buckets.set(id, bucket);
    return bucket;
  }
}

interface MiddlewareRequest extends ClientAddressSource { readonly method: string; readonly originalUrl?: string; readonly url: string }
interface MiddlewareResponse {
  status(code: number): MiddlewareResponse;
  setHeader(name: string, value: string): unknown;
  json(body: unknown): unknown;
}

export const requestRateLimitMiddleware = (limiter = new RequestRateLimiter()) =>
  (request: MiddlewareRequest, response: MiddlewareResponse, next: () => void): void => {
    const path = request.originalUrl ?? request.url;
    const wait = limiter.take(clientAddress(request), rateTiersFor(request.method, path));
    if (wait === 0) { next(); return; }
    response.setHeader("Retry-After", String(wait));
    response.status(429).json({ code: "RATE_LIMITED", message: "Too many requests. Try again shortly.", retryAfterSeconds: wait });
  };
