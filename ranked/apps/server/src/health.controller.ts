import { Controller, Get } from "@nestjs/common";
import { RankedConfigService } from "./config/ranked-config.service.js";
import { DatabaseService } from "./database/database.service.js";

@Controller()
export class HealthController {
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

  @Get("ready")
  public async ready(): Promise<object> {
    const [databaseConnected, schema] = await Promise.all([
      this.database.ping(),
      this.database.schemaStatus(),
    ]);
    const config = this.rankedConfig.getStatus();
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
      missingSchema: schema.missing,
      config,
      serverNow: new Date().toISOString(),
    };
  }
}
