import {
  Inject,
  Injectable,
  Logger,
  ServiceUnavailableException,
  Optional,
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
import { DATABASE, type DatabasePort } from "../database/database.port.js";
import { ModeAdmissionFence } from "./mode-admission.js";
import { expandSoloTierBands } from "@corum-ranked/rules";

const copySnapshot = (snapshot: RankedConfigSnapshot): RankedConfigSnapshot =>
  structuredClone(snapshot);

@Injectable()
export class RankedConfigService implements OnApplicationBootstrap, OnApplicationShutdown {
  private readonly admission = new ModeAdmissionFence();
  public withAdmission<T>(operation: () => Promise<T>): Promise<T> { return this.admission.run(operation); }
  private readonly logger = new Logger(RankedConfigService.name);
  private snapshot: RankedConfigSnapshot | null = null;
  private lastAttemptAt: string | null = null;
  private lastError: string | null = null;
  private refreshPromise: Promise<boolean> | null = null;
  private refreshTimer: ReturnType<typeof setInterval> | null = null;

  public constructor(
    @Inject(RANKED_CONFIG_SOURCE) private readonly source: RankedConfigSource,
    @Inject(SERVER_ENVIRONMENT) private readonly environment: ServerEnvironment,
    @Optional() @Inject(DATABASE) private readonly database?: DatabasePort,
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
      const sourceDocument = await this.source.fetchConfig();
      const document = { ...sourceDocument, operational: { ...sourceDocument.operational,
        tierBands: expandSoloTierBands(sourceDocument.operational.tierBands),
        modRestrictionEnabled: sourceDocument.operational.modRestrictionEnabled === undefined ? true : sourceDocument.operational.modRestrictionEnabled } };
      const errors = validateRankedConfigDocument(document);
      if (errors.length > 0) throw new Error(`Invalid Ranked config: ${errors.join("; ")}`);
      await this.withAdmission(async () => {
        // Startup OFF and the enabled -> OFF transition must clean QUEUED.
        // Display metadata may change its own generation while OFF; that is
        // not another admission change and must not repeatedly wake the DB.
        if (!document.operational.enabled && this.snapshot?.operational.enabled!==false && this.database) {
          // Leave MATCHED rows and their admitted games untouched. Publishing
          // OFF happens only after queue cleanup commits; admissions share this fence.
          await this.database.transaction(async transaction => {
            await transaction.query("UPDATE ranked_queue_entries SET status = 'LEFT' WHERE status = 'QUEUED'");
          });
        }
        this.snapshot = { ...structuredClone(document), fetchedAt: new Date().toISOString() };
      });
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

  public getSnapshotOrNull(): RankedConfigSnapshot | null {
    return this.snapshot ? copySnapshot(this.snapshot) : null;
  }

  public getStatus(): RankedConfigStatus {
    return {
      ready: this.snapshot !== null,
      queueEnabled: this.snapshot?.operational.enabled ?? false,
      generation: this.snapshot?.generation ?? null,
      fetchedAt: this.snapshot?.fetchedAt ?? null,
      lastAttemptAt: this.lastAttemptAt,
      lastError: this.lastError,
    };
  }
}
