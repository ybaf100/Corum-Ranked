import {ForumRecordsModule} from "../forum/forum-records.module.js";
import {SurvivalPlayerInfoController} from "./survival-player-info.controller.js";
import { Logger,Module } from "@nestjs/common";
import { AchievementModule } from "../achievement/achievement.module.js";
import { RankedConfigModule } from "../config/ranked-config.module.js";
import { SessionModule } from "../session/session.module.js";
import { SurvivalOnlineController } from "./survival-online.controller.js";
import { SurvivalQueueController } from "./survival-queue.controller.js";
import { SurvivalOnlineService } from "./survival-online.service.js";
import { SurvivalQueueService } from "./survival-queue.service.js";
import { SurvivalRateService } from "./survival-rate.service.js";
import { SurvivalController } from "./survival.controller.js";
import { SurvivalService } from "./survival.service.js";
import type { RandomSource } from "@corum-ranked/rules";
import { RANDOM_SOURCE } from "../common/runtime.module.js";
import { SurvivalEngineService } from "./survival-engine.service.js";
import { SurvivalEngineController } from "./survival-engine.controller.js";
import { GdSurvivalMapProvider, SURVIVAL_MAP_PROVIDER } from "./survival-map.provider.js";
import { SurvivalDebugController } from "./survival-debug.controller.js";
import { SurvivalDebugService } from "./survival-debug.service.js";
import { SERVER_ENVIRONMENT,type ServerEnvironment } from "../config/server-environment.js";
import { createSurvivalGdFetch } from "./survival-gd-transport.js";
@Module({ imports: [ForumRecordsModule,RankedConfigModule, SessionModule, AchievementModule], controllers: [SurvivalPlayerInfoController,SurvivalController,SurvivalEngineController,SurvivalDebugController,SurvivalOnlineController,SurvivalQueueController],
  providers: [SurvivalService,SurvivalEngineService,SurvivalDebugService,SurvivalOnlineService,SurvivalQueueService,SurvivalRateService,{provide:SURVIVAL_MAP_PROVIDER,
    useFactory:(random:RandomSource,environment:ServerEnvironment)=>{
      Logger.log(`Survival GD transport=${environment.survivalGdRelay?"relay":"direct"}`,"SurvivalGdTransport");
      return new GdSurvivalMapProvider(random,createSurvivalGdFetch(environment.survivalGdRelay));
    },
    inject:[RANDOM_SOURCE,SERVER_ENVIRONMENT]}],
  exports: [SurvivalService,SurvivalEngineService] })
export class SurvivalModule {}
