import { Module } from "@nestjs/common";
import { SurvivalModule } from "./survival/survival.module.js";
import { RankedConfigModule } from "./config/ranked-config.module.js";
import { ServerEnvironmentModule } from "./config/server-environment.module.js";
import { DatabaseModule } from "./database/database.module.js";
import { HealthController } from "./health.controller.js";
import { RuntimeModule } from "./common/runtime.module.js";
import { SecurityModule } from "./common/security.module.js";
import { SessionModule } from "./session/session.module.js";
import { QueueModule } from "./queue/queue.module.js";
import { MatchModule } from "./match/match.module.js";
import { DebugBotModule } from "./debug-bot/debug-bot.module.js";
import { DebugSpectateModule } from "./debug-spectate/debug-spectate.module.js";
import { LeaderboardModule } from "./leaderboard/leaderboard.module.js";
import { PlacementModule } from "./placement/placement.module.js";
import { AchievementModule } from "./achievement/achievement.module.js";
import { AdminModule } from "./admin/admin.module.js";
import { HistoryModule } from "./history/history.module.js";

@Module({
  imports: [
    SurvivalModule,
    ServerEnvironmentModule,
    RuntimeModule,
    SecurityModule,
    DatabaseModule,
    RankedConfigModule,
    SessionModule,
    QueueModule,
    MatchModule,
    DebugBotModule,
    DebugSpectateModule,
    LeaderboardModule,
    PlacementModule,
    AchievementModule,
    AdminModule,
    HistoryModule,
  ],
  controllers: [HealthController],
})
export class AppModule {}
