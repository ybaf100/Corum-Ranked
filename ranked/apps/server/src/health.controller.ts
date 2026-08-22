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
    return {
      ready: databaseReady && config.ready,
      databaseReady,
      databaseConnected,
      schemaReady: schema.ready,
      missingSchema: schema.missing,
      config,
      serverNow: new Date().toISOString(),
    };
  }
}
