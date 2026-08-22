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
      client: snapshot.client ?? {
        audio: { enabled: false, fadeInSeconds: 0.8, fadeOutSeconds: 0.6, resources: [] },
        ui: { fadeInSeconds: 0.24, fadeOutSeconds: 0.18 },
      },
      allowedMods: snapshot.allowedMods.filter((rule) => rule.enabled),
    };
  }
}
