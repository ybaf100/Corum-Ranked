import { Module } from "@nestjs/common";
import { RankedConfigModule } from "../config/ranked-config.module.js";
import { MatchModule } from "../match/match.module.js";
import { SessionModule } from "../session/session.module.js";
import { DebugBotController } from "./debug-bot.controller.js";
import { DebugBotMatchService } from "./debug-bot.service.js";

const routeEnabled = ["true", "1"].includes(
  process.env.ENABLE_DEBUG_BOT_MATCH?.trim().toLowerCase() || "false",
);

@Module({
  imports: [RankedConfigModule, SessionModule, MatchModule],
  controllers: routeEnabled ? [DebugBotController] : [],
  providers: routeEnabled ? [DebugBotMatchService] : [],
  exports: routeEnabled ? [DebugBotMatchService] : [],
})
export class DebugBotModule {}
