import { Inject, Injectable, Logger, Optional, type OnApplicationBootstrap } from "@nestjs/common";
import { REQUIRED_RANKED_CLIENT_VERSION } from "../session/client-version.js";
import { SERVER_ENVIRONMENT, type ServerEnvironment } from "./server-environment.js";
import {
  parseRankedModUpdates,
  rankedModDownloadUrl,
  type RankedBuildChannel,
  type RankedModUpdate,
  type RankedModUpdates,
} from "./ranked-mod-updates.js";

export const RANKED_MOD_UPDATE_FETCH = Symbol("RANKED_MOD_UPDATE_FETCH");
const DESCRIPTOR_MAX_BYTES = 4_096;
const FAILED_LOOKUP_RETRY_MS = 20_000;
const FETCH_TIMEOUT_MS = 4_000;

interface CachedDescriptor {
  readonly metadata?: RankedModUpdate;
  readonly retryAt: number;
}

/** Resolve the server's exact immutable R2 version and build channel.
 * This only serves lobby configuration; session/round admission stays local. */
@Injectable()
export class RankedModUpdateService implements OnApplicationBootstrap {
  private readonly logger = new Logger(RankedModUpdateService.name);
  private readonly cache = new Map<string, CachedDescriptor>();
  private readonly pending = new Map<string, Promise<RankedModUpdate | undefined>>();

  public constructor(
    @Optional() @Inject(SERVER_ENVIRONMENT) private readonly environment?: ServerEnvironment,
    @Optional() @Inject(RANKED_MOD_UPDATE_FETCH) private readonly fetchRemote: typeof fetch = fetch,
  ) {}

  public onApplicationBootstrap(): void {
    // Warm the immutable descriptor cache without delaying server startup.
    void this.getClientUpdates();
  }

  public async getClientUpdates(): Promise<RankedModUpdates> {
    const configured = this.environment?.rankedModUpdates ?? {};
    const result: Partial<Record<RankedBuildChannel, RankedModUpdate>> = {};
    await Promise.all((["debug", "release"] as const).map(async (channel) => {
      const explicit = configured[channel];
      const metadata = explicit ?? await this.resolve(channel, REQUIRED_RANKED_CLIENT_VERSION);
      if (metadata) {
        const base=this.environment?.rankedDownloadBaseUrl;
        result[channel] = { ...metadata,...(base && !metadata.downloadUrl?{
          downloadUrl:rankedModDownloadUrl(base,channel,metadata.version),
        }:{}) };
      }
    }));
    return result;
  }

  private async resolve(channel: RankedBuildChannel, version: string): Promise<RankedModUpdate | undefined> {
    if(!this.environment?.rankedDownloadBaseUrl)return undefined;
    const key = `${channel}-${version}`;
    const cached = this.cache.get(key);
    if (cached?.metadata) return cached.metadata;
    if (cached && cached.retryAt > Date.now()) return undefined;
    const active = this.pending.get(key);
    if (active) return active;
    const request = this.fetchDescriptor(channel, version).finally(() => this.pending.delete(key));
    this.pending.set(key, request);
    return request;
  }

  private async fetchDescriptor(channel: RankedBuildChannel, version: string): Promise<RankedModUpdate | undefined> {
    const key = `${channel}-${version}`;
    try {
      const base=this.environment!.rankedDownloadBaseUrl!;
      const url = `${base}/ranked/${key}/${channel}.json`;
      const response = await this.fetchRemote(url, {
        method: "GET",
        headers: { accept: "application/json" },
        signal: AbortSignal.timeout(FETCH_TIMEOUT_MS),
      });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      if (Number(response.headers.get("content-length")) > DESCRIPTOR_MAX_BYTES) {
        await response.body?.cancel();
        throw new Error("Update descriptor is too large");
      }
      const text = await response.text();
      if (Buffer.byteLength(text, "utf8") > DESCRIPTOR_MAX_BYTES) throw new Error("Update descriptor is too large");
      const updates = parseRankedModUpdates(text);
      const metadata = updates[channel];
      if (Object.keys(updates).length !== 1 || !metadata || metadata.version !== version) {
        throw new Error("Update descriptor channel/version does not match the server requirement");
      }
      if(metadata.downloadUrl && metadata.downloadUrl!==rankedModDownloadUrl(base,channel,version))
        throw new Error("Update package URL does not match this immutable R2 descriptor");
      // Published object paths are immutable; keep successful lookups for this process.
      this.cache.set(key, { metadata, retryAt: 0 });
      return metadata;
    } catch (error) {
      this.cache.set(key, { retryAt: Date.now() + FAILED_LOOKUP_RETRY_MS });
      this.logger.warn(`Ranked update ${key} is not available: ${error instanceof Error ? error.message : "lookup failed"}`);
      return undefined;
    }
  }
}
