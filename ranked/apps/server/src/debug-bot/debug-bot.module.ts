import { Module } from "@nestjs/common";
import { MatchModule } from "../match/match.module.js";
import { QueueModule } from "../queue/queue.module.js";
import { RankedConfigModule } from "../config/ranked-config.module.js";
import { DebugBotController } from "./debug-bot.controller.js";
import { DebugBotService } from "./debug-bot.service.js";

@Module({
  imports: [QueueModule, MatchModule, RankedConfigModule],
  controllers: [DebugBotController],
  providers: [DebugBotService],
  exports: [DebugBotService],
})
export class DebugBotModule {}
