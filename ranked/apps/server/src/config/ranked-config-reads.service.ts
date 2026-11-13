import { Inject, Injectable, Logger, Optional } from "@nestjs/common";
import { DATABASE, type SqlExecutor } from "../database/database.port.js";
import { SERVER_CLOCK, type ServerClock } from "../common/runtime.module.js";
import type { RankedClientPresentationConfig } from "./ranked-config.document.js";
import { readRankedAudioVolumes, type RankedAudioVolume } from "./ranked-audio-volume.js";
import { readRankedSeasons, type RankedSeasons } from "./ranked-seasons.js";

export interface RankedConfigReadValues {
  readonly volumes: readonly RankedAudioVolume[];
  readonly seasons: RankedSeasons;
}

/** Values younger than this are served without touching the database. */
export const CONFIG_READS_FRESH_MS = 30_000;

/**
 * Database-backed parts of GET /api/ranked/config (audio volumes, seasons).
 *
 * Neon suspends its compute after five idle minutes and the first query then
 * waits for it to start again. Lobby entry must not wait for that: once a
 * value has been read it is served immediately and refreshed in the
 * background, and that refresh also wakes the database for the session
 * creation that follows a few hundred milliseconds later.
 */
@Injectable()
export class RankedConfigReadsService {
  private readonly logger = new Logger(RankedConfigReadsService.name);
  private cached: { readonly key: string; readonly at: number; readonly values: RankedConfigReadValues } | undefined;
  private refreshing: { readonly key: string; readonly generation: number; readonly promise: Promise<RankedConfigReadValues> } | undefined;
  private generation = 0;

  public constructor(
    @Optional() @Inject(DATABASE) private readonly database?: SqlExecutor,
    @Optional() @Inject(SERVER_CLOCK) private readonly clock?: ServerClock,
  ) {}

  private now(): number { return this.clock?.now().getTime() ?? Date.now(); }

  public async read(client: RankedClientPresentationConfig | undefined): Promise<RankedConfigReadValues> {
    const key = [...new Set((client?.audio.resources ?? []).map((resource) => resource.songId))].sort((a, b) => a - b).join(",");
    const cached = this.cached?.key === key ? this.cached : undefined;
    if (cached && this.now() - cached.at < CONFIG_READS_FRESH_MS) return cached.values;
    const refresh = this.refresh(client, key);
    if (cached) {
      refresh.catch((error: unknown) => this.logger.warn(`Background config read failed: ${error instanceof Error ? error.message : String(error)}`));
      return cached.values;
    }
    return refresh;
  }

  /** Admin writes call this so the next request reads the new values. */
  public invalidate(): void {
    this.cached = undefined;
    this.generation += 1;
  }

  private refresh(client: RankedClientPresentationConfig | undefined, key: string): Promise<RankedConfigReadValues> {
    if (this.refreshing?.key === key && this.refreshing.generation === this.generation) return this.refreshing.promise;
    const generation = this.generation;
    const promise = Promise.all([readRankedAudioVolumes(client, this.database), readRankedSeasons(this.database)])
      .then(([volumes, seasons]) => {
        const values = { volumes, seasons };
        // A read that raced an admin write must not overwrite the invalidation.
        if (generation === this.generation) this.cached = { key, at: this.now(), values };
        return values;
      })
      .finally(() => { if (this.refreshing?.promise === promise) this.refreshing = undefined; });
    this.refreshing = { key, generation, promise };
    return promise;
  }
}
