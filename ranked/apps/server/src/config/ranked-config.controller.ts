import { Controller, Get } from "@nestjs/common";
import { RankedConfigService } from "./ranked-config.service.js";

@Controller("api/ranked")
export class RankedConfigController {
  public constructor(private readonly rankedConfig: RankedConfigService) {}

  @Get("config")
  public getClientConfig(): object {
    const snapshot = this.rankedConfig.getSnapshot();
    return {
      serverNow: new Date().toISOString(),
      generation: snapshot.generation,
      rulesVersion: snapshot.operational.rules.rulesVersion,
      queueEnabled: snapshot.operational.enabled,
      rules: snapshot.operational.rules,
      cbf: snapshot.operational.cbf,
      allowedMods: snapshot.allowedMods.filter((rule) => rule.enabled),
    };
  }
}
