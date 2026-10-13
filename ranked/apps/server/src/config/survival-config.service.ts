import { Inject, Injectable, Logger, ServiceUnavailableException,Optional, type OnApplicationBootstrap, type OnApplicationShutdown } from "@nestjs/common";
import { validateSurvivalConfig, type SurvivalConfig } from "@corum-ranked/rules";
import { SERVER_ENVIRONMENT, type ServerEnvironment } from "./server-environment.js";
import { DATABASE,type DatabasePort } from "../database/database.port.js";
import { lockSurvivalAdmission } from "../survival/survival-admission.js";
import { ModeAdmissionFence } from "./mode-admission.js";

export interface SurvivalConfigDocument {
  readonly generation: string;
  readonly generatedAt: string;
  readonly config: SurvivalConfig;
}
export const SURVIVAL_CONFIG_SOURCE = Symbol("SURVIVAL_CONFIG_SOURCE");
export interface SurvivalConfigSource { fetchConfig(): Promise<SurvivalConfigDocument>; }

@Injectable()
export class AppsScriptSurvivalConfigSource implements SurvivalConfigSource {
  public constructor(@Inject(SERVER_ENVIRONMENT) private readonly environment: ServerEnvironment) {}
  public async fetchConfig(): Promise<SurvivalConfigDocument> {
    // Same deployment and spreadsheet. Explicitly replace a pinned ranked_config action.
    const url = new URL(this.environment.rankedConfigUrl);
    url.searchParams.set("action", "survival_config");
    const response = await fetch(url, { headers: { accept: "application/json" },
      signal: AbortSignal.timeout(this.environment.rankedConfigFetchTimeoutMs) });
    if (!response.ok) throw new Error(`Survival config source returned HTTP ${response.status}`);
    const body = await response.json() as { ok: boolean; action: string; data?: SurvivalConfigDocument; error?: string };
    if (!body.ok || body.action !== "survival_config" || !body.data) throw new Error(body.error || "Invalid Survival config envelope");
    return body.data;
  }
}

@Injectable()
export class SurvivalConfigService implements OnApplicationBootstrap, OnApplicationShutdown {
  private readonly logger = new Logger(SurvivalConfigService.name);
  private snapshot: (SurvivalConfigDocument & { fetchedAt: string }) | null = null;
  private error: string | null = null;
  private pending: Promise<boolean> | null = null;
  private timer: ReturnType<typeof setInterval> | null = null;
  private readonly admission = new ModeAdmissionFence();
  public withAdmission<T>(operation: () => Promise<T>): Promise<T> { return this.admission.run(operation); }
  public constructor(@Inject(SURVIVAL_CONFIG_SOURCE) private readonly source: SurvivalConfigSource,
    @Inject(SERVER_ENVIRONMENT) private readonly environment: ServerEnvironment,
    @Optional() @Inject(DATABASE)private readonly database?:DatabasePort) {}
  public async onApplicationBootstrap(): Promise<void> {
    await this.refresh();
    this.timer = setInterval(() => { void this.refresh(); }, this.environment.rankedConfigRefreshMs);
    this.timer.unref();
  }
  public onApplicationShutdown(): void { if (this.timer) clearInterval(this.timer); this.timer = null; }
  public refresh(): Promise<boolean> {
    if (this.pending) return this.pending;
    this.pending = this.performRefresh().finally(() => { this.pending = null; });
    return this.pending;
  }
  private async performRefresh(): Promise<boolean> {
    try {
      const document = await this.source.fetchConfig();
      const errors = [...validateSurvivalConfig(document.config)];
      if (!document.generation?.trim() || !Number.isFinite(Date.parse(document.generatedAt))) errors.push("Invalid Survival document identity");
      if (errors.length) throw new Error(errors.join("; "));
      // M3 internal match admission shares this fence. Existing matches use
      // their captured config. M5 must remove its QUEUED rows before publishing.
      return this.withAdmission(async () => {
        if(this.database)await this.database.transaction(async tx=>{
          await lockSurvivalAdmission(tx);
          const current=(await tx.query<{generated_at:Date|string}>("SELECT generated_at FROM sur_mode_state WHERE singleton=TRUE")).rows[0];
          if(current && new Date(current.generated_at).getTime()>Date.parse(document.generatedAt))throw new Error("Stale Survival configuration generation");
          await tx.query(`INSERT INTO sur_mode_state(singleton,enabled,generation,generated_at) VALUES(TRUE,$1,$2,$3)
            ON CONFLICT(singleton) DO UPDATE SET enabled=EXCLUDED.enabled,generation=EXCLUDED.generation,generated_at=EXCLUDED.generated_at`,
            [document.config.enabled,document.generation,document.generatedAt]);
          if(!document.config.enabled)await tx.query("UPDATE sur_queue_entries SET status='DISABLED' WHERE status='QUEUED'");
        });
        this.snapshot = { ...structuredClone(document), fetchedAt: new Date().toISOString() };
        this.error = null; return true;
      });
    } catch (error) {
      this.error = error instanceof Error ? error.message : "Survival config refresh failed";
      this.logger.error(this.error); return false;
    }
  }
  public getSnapshot() {
    if (!this.snapshot) throw new ServiceUnavailableException("Survival configuration is unavailable; run setupCorumSurvivalConfig");
    return structuredClone(this.snapshot);
  }
  public getStatus() { return { ready: this.snapshot !== null, enabled: this.snapshot?.config.enabled ?? false,
    generation: this.snapshot?.generation ?? null, fetchedAt: this.snapshot?.fetchedAt ?? null, error: this.error }; }
}
