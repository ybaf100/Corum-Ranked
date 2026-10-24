import { Inject, Injectable, Optional } from "@nestjs/common";
import { DATABASE, type SqlExecutor } from "../database/database.port.js";
import { RankedMapMetadataResolver } from "./map-metadata.js";
import {
  SERVER_ENVIRONMENT,
  type ServerEnvironment,
} from "./server-environment.js";
import type {
  RankedConfigDocument,
  RankedConfigSourceResponse,
} from "./ranked-config.document.js";

export const RANKED_CONFIG_SOURCE = Symbol("RANKED_CONFIG_SOURCE");

export interface RankedConfigSource {
  fetchConfig(): Promise<RankedConfigDocument>;
}

@Injectable()
export class AppsScriptRankedConfigSource implements RankedConfigSource {
  private readonly metadata: RankedMapMetadataResolver;
  private databaseIdentity:string|null=null;
  public constructor(
    @Inject(SERVER_ENVIRONMENT) private readonly environment: ServerEnvironment,
    @Optional() @Inject(DATABASE) database?: SqlExecutor,
  ) { this.metadata = new RankedMapMetadataResolver(fetch, database); }

  public async fetchConfig(): Promise<RankedConfigDocument> {
    const url = new URL(this.environment.rankedConfigUrl);
    if (!url.searchParams.has("action")) url.searchParams.set("action", "ranked_config");
    const response = await fetch(url, {
      method: "GET",
      headers: { accept: "application/json" },
      signal: AbortSignal.timeout(this.environment.rankedConfigFetchTimeoutMs),
    });
    if (!response.ok) {
      throw new Error(`Ranked config source returned HTTP ${response.status}`);
    }
    const body = (await response.json()) as RankedConfigSourceResponse;
    if (!body.ok || body.action !== "ranked_config" || !body.data) {
      throw new Error(body.error || "Ranked config source returned an invalid envelope");
    }
    const {generatedAt:_generatedAt,...content}=body.data;
    const identity=JSON.stringify(content);
    // Keep the existing bounded HTTP enrichment/TTL refresh, but background
    // refreshes of the same document must neither reload nor UPSERT its DB cache.
    const enriched=await this.metadata.enrich(body.data,identity!==this.databaseIdentity);
    this.databaseIdentity=identity;
    return enriched;
  }
}
