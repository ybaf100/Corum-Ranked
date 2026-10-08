import { Module } from "@nestjs/common";
import { SessionModule } from "../session/session.module.js";
import { AchievementController } from "./achievement.controller.js";
import { AchievementService } from "./achievement.service.js";

@Module({
  imports: [SessionModule],
  controllers: [AchievementController],
  providers: [AchievementService],
  exports: [AchievementService],
})
export class AchievementModule {}
