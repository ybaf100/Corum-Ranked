import { Inject, Injectable, Logger, ServiceUnavailableException,Optional, type OnApplicationBootstrap, type OnApplicationShutdown } from "@nestjs/common";
import { validateSurvivalConfig, soloBaseTierBands, survivalConfigForPlayerCount, type SurvivalPlayerCount, type SurvivalConfig } from "@corum-ranked/rules";
import { RankedConfigService } from "./ranked-config.service.js";
import { SERVER_ENVIRONMENT, type ServerEnvironment } from "./server-environment.js";
import { DATABASE,type DatabasePort,type SqlExecutor } from "../database/database.port.js";
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
  private operationPlayerCount: SurvivalPlayerCount = 16;
  private persistedDocument: string | null = null;
  private snapshot: (SurvivalConfigDocument & { fetchedAt: string }) | null = null;
  private error: string | null = null;
  private pending: Promise<boolean> | null = null;
  private timer: ReturnType<typeof setInterval> | null = null;
  private readonly admission = new ModeAdmissionFence();
  public withAdmission<T>(operation: () => Promise<T>): Promise<T> { return this.admission.run(operation); }
  public constructor(@Inject(SURVIVAL_CONFIG_SOURCE) private readonly source: SurvivalConfigSource,
    @Inject(SERVER_ENVIRONMENT) private readonly environment: ServerEnvironment,
    @Optional() @Inject(DATABASE)private readonly database?:DatabasePort,
    @Optional() private readonly rankedConfig?:RankedConfigService) {}
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
        // Apps Script stamps generatedAt on every read; it is not a config change.
        const {generatedAt:_generatedAt,...content}=document;
        const identity=JSON.stringify(content);
        if(this.database && identity!==this.persistedDocument)await this.database.transaction(async tx=>{
          await lockSurvivalAdmission(tx);
          const current=(await tx.query<{generated_at:Date|string;match_player_count:SurvivalPlayerCount}>("SELECT generated_at,match_player_count FROM sur_mode_state WHERE singleton=TRUE")).rows[0];
          if(current && new Date(current.generated_at).getTime()>Date.parse(document.generatedAt))throw new Error("Stale Survival configuration generation");
          this.operationPlayerCount=current?.match_player_count??16;
          const generation=this.formatGeneration(document.generation,this.operationPlayerCount);
          await tx.query(`INSERT INTO sur_mode_state(singleton,enabled,generation,generated_at) VALUES(TRUE,$1,$2,$3)
            ON CONFLICT(singleton) DO UPDATE SET enabled=EXCLUDED.enabled,generation=EXCLUDED.generation,generated_at=EXCLUDED.generated_at`,
            [document.config.enabled,generation,document.generatedAt]);
          if(!document.config.enabled)await tx.query("UPDATE sur_queue_entries SET status='DISABLED' WHERE status='QUEUED'");
        });
        this.persistedDocument=identity;
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
    const snapshot=structuredClone(this.snapshot);
    let config=survivalConfigForPlayerCount(snapshot.config,this.operationPlayerCount);
    const solo=this.rankedConfig?.getSnapshotOrNull();
    if(solo){
      const bands=soloBaseTierBands(solo.operational.tierBands);
      config={...config,tierBands:config.tierBands.map(band=>{
        const boundary=bands.find(b=>b.tier===band.group);
        return boundary?{...band,minimumLp:boundary.minInclusive}:band;
      })};
      const errors=validateSurvivalConfig(config);
      if(errors.length)throw new ServiceUnavailableException(`Invalid shared Survival tier boundaries: ${errors.join("; ")}`);
    }
    return {...snapshot,generation:this.formatGeneration(snapshot.generation,this.operationPlayerCount),config};
  }
  private formatGeneration(generation:string,count:SurvivalPlayerCount):string {return count===64?`${generation}:64p`:generation;}
  /** Caller holds both the admission fence and its SQL advisory lock. */
  public async operationMode(tx:SqlExecutor,count?:SurvivalPlayerCount):Promise<SurvivalPlayerCount> {
    const snapshot=this.snapshot;
    if(!snapshot)throw new ServiceUnavailableException("Survival configuration is unavailable");
    if(count!==undefined){
      if(count!==16 && count!==64)throw new ServiceUnavailableException("Invalid Survival format");
      const config=survivalConfigForPlayerCount(snapshot.config,count);
      const errors=validateSurvivalConfig(config);if(errors.length)throw new ServiceUnavailableException(errors.join("; "));
      await tx.query(`INSERT INTO sur_mode_state(singleton,enabled,generation,generated_at,match_player_count)
        VALUES(TRUE,$1,$2,$3,$4) ON CONFLICT(singleton) DO UPDATE SET
        match_player_count=EXCLUDED.match_player_count,generation=EXCLUDED.generation`,
        [snapshot.config.enabled,this.formatGeneration(snapshot.generation,count),snapshot.generatedAt,count]);
      return count;
    }
    const row=(await tx.query<{match_player_count:SurvivalPlayerCount}>("SELECT match_player_count FROM sur_mode_state WHERE singleton=TRUE")).rows[0];
    return row?.match_player_count??16;
  }
  /** Publish memory only after the transaction was acknowledged. */
  public applyOperationMode(count:SurvivalPlayerCount):void {this.operationPlayerCount=count;}
  public invalidateOperationMode():void {this.persistedDocument=null;}
  public getStatus() { return { ready: this.snapshot !== null, enabled: this.snapshot?.config.enabled ?? false,
    generation: this.snapshot?this.formatGeneration(this.snapshot.generation,this.operationPlayerCount):null, matchPlayerCount:this.operationPlayerCount, fetchedAt: this.snapshot?.fetchedAt ?? null, error: this.error }; }
}
