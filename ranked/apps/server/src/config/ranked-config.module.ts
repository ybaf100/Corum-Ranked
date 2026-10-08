import { Module } from "@nestjs/common";
import { RankedConfigController } from "./ranked-config.controller.js";
import { RankedConfigService } from "./ranked-config.service.js";
import {
  AppsScriptRankedConfigSource,
  RANKED_CONFIG_SOURCE,
} from "./ranked-config.source.js";
import {
  AppsScriptCsmpTierSource,
  CSMP_TIER_SOURCE,
} from "./csmp-tier.source.js";

@Module({
  controllers: [RankedConfigController],
  providers: [
    RankedConfigService,
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
  exports: [RankedConfigService, CSMP_TIER_SOURCE],
})
export class RankedConfigModule {}
