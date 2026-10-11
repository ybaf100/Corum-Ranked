import { Module } from "@nestjs/common";
import { AchievementModule } from "../achievement/achievement.module.js";
import { RankedConfigModule } from "../config/ranked-config.module.js";
import { SessionModule } from "../session/session.module.js";
import { SurvivalController } from "./survival.controller.js";
import { SurvivalService } from "./survival.service.js";
@Module({ imports: [RankedConfigModule, SessionModule, AchievementModule], controllers: [SurvivalController],
  providers: [SurvivalService], exports: [SurvivalService] })
export class SurvivalModule {}
