import { Module } from "@nestjs/common";
import { RankedConfigModule } from "../config/ranked-config.module.js";
import { SessionController } from "./session.controller.js";
import { SessionGuard } from "./session.guard.js";
import { SessionService } from "./session.service.js";
import { ForumRecordsModule } from "../forum/forum-records.module.js";
import { ForumSessionController } from "../forum/forum-session.controller.js";

@Module({
  imports: [RankedConfigModule, ForumRecordsModule],
  controllers: [SessionController, ForumSessionController],
  providers: [SessionService, SessionGuard],
  exports: [SessionService, SessionGuard],
})
export class SessionModule {}

