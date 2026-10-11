import { Module } from "@nestjs/common";
import { RankedConfigModule } from "../config/ranked-config.module.js";
import { SessionController } from "./session.controller.js";
import { SessionGuard } from "./session.guard.js";
import { AdmissionVersionGuard } from "./admission-version.guard.js";
import { SessionService } from "./session.service.js";
import { ForumRecordsModule } from "../forum/forum-records.module.js";
import { ForumSessionController } from "../forum/forum-session.controller.js";
import { StandingController } from "../leaderboard/standing.controller.js";

@Module({
  imports: [RankedConfigModule, ForumRecordsModule],
  controllers: [SessionController, ForumSessionController, StandingController],
  providers: [SessionService, SessionGuard, AdmissionVersionGuard],
  exports: [SessionService, SessionGuard, AdmissionVersionGuard],
})
export class SessionModule {}

