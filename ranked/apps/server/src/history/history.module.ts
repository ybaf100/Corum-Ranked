import { Module } from "@nestjs/common";
import { MatchModule } from "../match/match.module.js";
import { SessionModule } from "../session/session.module.js";
import { SurvivalModule } from "../survival/survival.module.js";
import { HistoryController } from "./history.controller.js";
import { HistoryService } from "./history.service.js";

@Module({
  imports: [MatchModule, SurvivalModule, SessionModule],
  controllers: [HistoryController],
  providers: [HistoryService],
})
export class HistoryModule {}
