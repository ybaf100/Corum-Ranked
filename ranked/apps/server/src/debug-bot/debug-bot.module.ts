import { SessionModule } from "../session/session.module.js";
import { Module } from "@nestjs/common";
import { MatchModule } from "../match/match.module.js";
import { QueueModule } from "../queue/queue.module.js";
import { RankedConfigModule } from "../config/ranked-config.module.js";
import { DebugBotController } from "./debug-bot.controller.js";
import { DebugBotService } from "./debug-bot.service.js";
import { DebugChallengeController } from "./debug-challenge.controller.js";
import { DebugChallengeService } from "./debug-challenge.service.js";

@Module({
  imports: [SessionModule, QueueModule, MatchModule, RankedConfigModule],
  controllers: [DebugBotController, DebugChallengeController],
  providers: [DebugBotService, DebugChallengeService],
  exports: [DebugBotService],
})
export class DebugBotModule {}
