import {DebugUpdateChannelController} from "./debug-update-channel.controller.js";
import { Module } from "@nestjs/common";
import { RankedConfigController } from "./ranked-config.controller.js";
import { RankedConfigService } from "./ranked-config.service.js";
import { RankedModUpdateService } from "./ranked-mod-update.service.js";
import { AppsScriptSurvivalConfigSource, SurvivalConfigService, SURVIVAL_CONFIG_SOURCE } from "./survival-config.service.js";
import {
  AppsScriptRankedConfigSource,
  RANKED_CONFIG_SOURCE,
} from "./ranked-config.source.js";
import {
  AppsScriptCsmpTierSource,
  CSMP_TIER_SOURCE,
} from "./csmp-tier.source.js";

@Module({
  controllers: [RankedConfigController, DebugUpdateChannelController],
  providers: [
    SurvivalConfigService,
    AppsScriptSurvivalConfigSource,
    { provide: SURVIVAL_CONFIG_SOURCE, useExisting: AppsScriptSurvivalConfigSource },
    RankedConfigService,
    RankedModUpdateService,
    AppsScriptRankedConfigSource,
    AppsScriptCsmpTierSource,
    {
      provide: RANKED_CONFIG_SOURCE,
      useExisting: AppsScriptRankedConfigSource,
    },
    {
      provide: CSMP_TIER_SOURCE,
      useExisting: AppsScriptCsmpTierSource,
    },
  ],
  exports: [RankedModUpdateService,RankedConfigService, SurvivalConfigService, CSMP_TIER_SOURCE],
})
export class RankedConfigModule {}
