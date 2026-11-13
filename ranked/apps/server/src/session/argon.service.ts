import {
  HttpException,
  HttpStatus,
  Inject,
  Injectable,
  Logger,
  Optional,
} from "@nestjs/common";
import { createHash } from "node:crypto";
import { SERVER_ENVIRONMENT, type ServerEnvironment } from "../config/server-environment.js";

/** Injectable fetch so tests never reach the public Argon service. */
export const ARGON_FETCH = Symbol("ARGON_FETCH");

export interface ArgonIdentity {
  readonly accountId: string;
  /** The username stored inside the Argon token (from the GD servers), not the client's claim. */
  readonly username: string;
  readonly verified: boolean;
}

const VERIFIED_CACHE_MS = 6 * 60 * 60 * 1_000;
const VERIFIED_CACHE_LIMIT = 20_000;
const ARGON_TIMEOUT_MS = 6_000;
const TOKEN_PATTERN = /^[\x21-\x7e]{16,4096}$/;
/** A refused token is answered locally for this long instead of asking Argon again. */
const REFUSED_CACHE_MS = 10 * 60 * 1_000;


const refused = (code: string, message: string, cause?: string) =>
  new HttpException({ code, message, ...(cause ? { cause } : {}) }, HttpStatus.UNAUTHORIZED);
const unavailable = () => new HttpException({
  code: "ARGON_UNAVAILABLE",
  message: "Geometry Dash account verification is temporarily unavailable. Try again shortly.",
}, HttpStatus.SERVICE_UNAVAILABLE);

/**
 * Proves GD account ownership with an Argon authtoken (https://github.com/GlobedGD/argon).
 * The client never sends its GJP or password; Argon confirms that the token
 * belongs to the claimed account ID and returns the username stored in it.
 * Successful checks are cached per (account, token) so repeated lobby entries
 * stay well inside Argon's public rate limits (750/hour, 10000/day).
 */
@Injectable()
export class ArgonService {
  private readonly logger = new Logger(ArgonService.name);
  private readonly verified = new Map<string, { readonly username: string; readonly until: number }>();
  private readonly pending = new Map<string, Promise<string>>();
  private readonly refusedTokens = new Map<string, { readonly cause: string; readonly until: number }>();

  public constructor(
    @Optional() @Inject(SERVER_ENVIRONMENT) private readonly environment?: ServerEnvironment,
    @Optional() @Inject(ARGON_FETCH) private readonly fetchRemote: typeof fetch = fetch,
  ) {}

  /** Parsed deployments always set a mode (default "required"); only hand-built test environments omit it. */
  public get required(): boolean {
    return this.environment?.argon?.mode === "required";
  }

  public async verify(accountId: string, claimedUsername: string, token: string | undefined): Promise<ArgonIdentity> {
    if (!this.required) return { accountId, username: claimedUsername, verified: false };
    if (!token) {
      throw refused("ARGON_TOKEN_REQUIRED", "Update Ranked: Geometry Dash account verification is required.");
    }
    if (!TOKEN_PATTERN.test(token)) throw refused("ARGON_TOKEN_INVALID", "The account verification token is malformed.");
    const key = `${accountId}:${createHash("sha256").update(token).digest("hex")}`;
    const now = Date.now();
    const cached = this.verified.get(key);
    if (cached && cached.until > now) return { accountId, username: cached.username, verified: true };
    if (cached) this.verified.delete(key);
    const refusal = this.refusedTokens.get(key);
    if (refusal && refusal.until > now) {
      throw refused("ARGON_TOKEN_INVALID", "Geometry Dash account verification failed.", refusal.cause);
    }
    if (refusal) this.refusedTokens.delete(key);
    let check = this.pending.get(key);
    if (!check) {
      check = this.check(accountId, claimedUsername, token)
        .catch((error: unknown) => {
          if (error instanceof HttpException && error.getStatus() === HttpStatus.UNAUTHORIZED) {
            const body = error.getResponse() as { cause?: unknown };
            this.remember(this.refusedTokens, key, { cause: typeof body.cause === "string" ? body.cause : "invalid token", until: Date.now() + REFUSED_CACHE_MS });
          }
          throw error;
        })
        .finally(() => this.pending.delete(key));
      this.pending.set(key, check);
    }
    const username = await check;
    this.remember(this.verified, key, { username, until: Date.now() + VERIFIED_CACHE_MS });
    return { accountId, username, verified: true };
  }

  private remember<V>(map: Map<string, V>, key: string, value: V): void {
    if (map.size >= VERIFIED_CACHE_LIMIT) {
      const oldest = map.keys().next().value;
      if (oldest !== undefined) map.delete(oldest);
    }
    map.set(key, value);
  }

  private async check(accountId: string, claimedUsername: string, token: string): Promise<string> {
    const url = new URL("/v1/validation/check-strong", this.environment!.argon!.baseUrl);
    url.searchParams.set("account_id", accountId);
    url.searchParams.set("username", claimedUsername);
    url.searchParams.set("authtoken", token);
    let response: Response;
    try {
      response = await this.fetchRemote(url, {
        method: "GET",
        headers: { accept: "application/json" },
        redirect: "error",
        signal: AbortSignal.timeout(ARGON_TIMEOUT_MS),
      });
    } catch {
      this.logger.warn("Argon validation request failed");
      throw unavailable();
    }
    if (response.status !== 200) {
      await response.body?.cancel().catch(() => undefined);
      this.logger.warn(`Argon validation returned HTTP ${response.status}`);
      throw unavailable();
    }
    let body: { valid?: unknown; valid_weak?: unknown; username?: unknown; cause?: unknown };
    try {
      body = await response.json() as typeof body;
    } catch {
      throw unavailable();
    }
    // valid_weak: the token belongs to this account ID. valid additionally
    // matches the claimed username; a mismatch only means a stale client name,
    // so the authoritative name inside the token is used instead.
    if (body.valid_weak !== true && body.valid !== true) {
      const cause = typeof body.cause === "string" ? body.cause.slice(0, 200) : "invalid token";
      throw refused("ARGON_TOKEN_INVALID", "Geometry Dash account verification failed.", cause);
    }
    const name = typeof body.username === "string" ? body.username.trim().slice(0, 32) : "";
    if (name) return name;
    if (body.valid === true) return claimedUsername;
    throw refused("ARGON_TOKEN_INVALID", "Geometry Dash account verification failed.", "username unavailable");
  }
}
