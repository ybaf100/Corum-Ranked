import {
  Inject,
  Injectable,
  Logger,
  ServiceUnavailableException,
  type OnApplicationBootstrap,
  type OnApplicationShutdown,
} from "@nestjs/common";
import {
  RANKED_CONFIG_SOURCE,
  type RankedConfigSource,
} from "./ranked-config.source.js";
import {
  SERVER_ENVIRONMENT,
  type ServerEnvironment,
} from "./server-environment.js";
import type {
  RankedConfigSnapshot,
  RankedConfigStatus,
} from "./ranked-config.document.js";
import { validateRankedConfigDocument } from "./ranked-config.validator.js";

const copySnapshot = (snapshot: RankedConfigSnapshot): RankedConfigSnapshot =>
  structuredClone(snapshot);

@Injectable()
export class RankedConfigService implements OnApplicationBootstrap, OnApplicationShutdown {
  private readonly logger = new Logger(RankedConfigService.name);
  private snapshot: RankedConfigSnapshot | null = null;
  private lastAttemptAt: string | null = null;
  private lastError: string | null = null;
  private refreshPromise: Promise<boolean> | null = null;
  private refreshTimer: ReturnType<typeof setInterval> | null = null;

  public constructor(
    @Inject(RANKED_CONFIG_SOURCE) private readonly source: RankedConfigSource,
    @Inject(SERVER_ENVIRONMENT) private readonly environment: ServerEnvironment,
  ) {}

  public async onApplicationBootstrap(): Promise<void> {
    await this.refresh();
    this.refreshTimer = setInterval(() => {
      void this.refresh();
    }, this.environment.rankedConfigRefreshMs);
    this.refreshTimer.unref();
  }

  public onApplicationShutdown(): void {
    if (this.refreshTimer) clearInterval(this.refreshTimer);
    this.refreshTimer = null;
  }

  public async refresh(): Promise<boolean> {
    if (this.refreshPromise) return this.refreshPromise;
    this.refreshPromise = this.performRefresh().finally(() => {
      this.refreshPromise = null;
    });
    return this.refreshPromise;
  }

  private async performRefresh(): Promise<boolean> {
    this.lastAttemptAt = new Date().toISOString();
    try {
      const document = await this.source.fetchConfig();
      const errors = validateRankedConfigDocument(document);
      if (errors.length > 0) throw new Error(`Invalid Ranked config: ${errors.join("; ")}`);
      this.snapshot = {
        ...structuredClone(document),
        fetchedAt: new Date().toISOString(),
      };
      this.lastError = null;
      return true;
    } catch (error) {
      this.lastError = error instanceof Error ? error.message : "Unknown config refresh failure";
      this.logger.error(this.lastError);
      return false;
    }
  }

  public getSnapshot(): RankedConfigSnapshot {
    if (!this.snapshot) {
      throw new ServiceUnavailableException("Ranked config has no valid snapshot");
    }
    return copySnapshot(this.snapshot);
  }

  public getStatus(): RankedConfigStatus {
    return {
      ready: this.snapshot !== null && this.snapshot.operational.enabled,
      generation: this.snapshot?.generation ?? null,
      fetchedAt: this.snapshot?.fetchedAt ?? null,
      lastAttemptAt: this.lastAttemptAt,
      lastError: this.lastError,
    };
  }
}
