import { Module } from "@nestjs/common";
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

@Module({
  imports: [
    ServerEnvironmentModule,
    RuntimeModule,
    SecurityModule,
    DatabaseModule,
    RankedConfigModule,
    SessionModule,
    QueueModule,
    MatchModule,
    DebugBotModule,
  ],
  controllers: [HealthController],
})
export class AppModule {}
