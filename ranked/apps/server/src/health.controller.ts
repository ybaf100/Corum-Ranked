import { Controller, Get, Logger } from "@nestjs/common";
import { RankedConfigService } from "./config/ranked-config.service.js";
import { DatabaseService } from "./database/database.service.js";

@Controller()
export class HealthController {
  private readonly logger = new Logger(HealthController.name);
  public constructor(
    private readonly database: DatabaseService,
    private readonly rankedConfig: RankedConfigService,
  ) {}

  @Get("health")
  public health(): object {
    return {
      status: "ok",
      service: "corum-ranked-server",
      sourceSha: /^[a-f0-9]{40}$/.test(process.env.CORUM_SOURCE_SHA ?? "") ? process.env.CORUM_SOURCE_SHA : null,
      serverNow: new Date().toISOString(),
    };
  }

  /** Public readiness: booleans only. Missing schema objects, config source
   * errors and fetch timings stay in the server log, never in a public reply. */
  @Get("ready")
  public async ready(): Promise<object> {
    const [databaseConnected, schema] = await Promise.all([
      this.database.ping(),
      this.database.schemaStatus(),
    ]);
    const config = this.rankedConfig.getStatus();
    if (!schema.ready && schema.missing.length) this.logger.warn(`Schema objects missing: ${schema.missing.join(", ")}`);
    if (!config.ready && config.lastError) this.logger.warn(`Ranked config not ready: ${config.lastError}`);
    const databaseReady = databaseConnected && schema.ready;
    const publicDataReady = databaseReady;
    const rankedReady = databaseReady && config.ready && config.queueEnabled;
    return {
      ready: publicDataReady,
      publicDataReady,
      rankedReady,
      databaseReady,
      databaseConnected,
      schemaReady: schema.ready,
      config: { ready: config.ready, queueEnabled: config.queueEnabled },
      serverNow: new Date().toISOString(),
    };
  }
}
