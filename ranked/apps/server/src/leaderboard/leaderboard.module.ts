import { Module } from "@nestjs/common";
import { RankedConfigModule } from "../config/ranked-config.module.js";
import { LeaderboardController } from "./leaderboard.controller.js";
import { LeaderboardService } from "./leaderboard.service.js";

@Module({
  imports: [RankedConfigModule],
  controllers: [LeaderboardController],
  providers: [LeaderboardService],
  exports: [LeaderboardService],
})
export class LeaderboardModule {}
