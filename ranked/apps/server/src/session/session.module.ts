import { Module } from "@nestjs/common";
import { RankedConfigModule } from "../config/ranked-config.module.js";
import { SessionController } from "./session.controller.js";
import { SessionGuard } from "./session.guard.js";
import { SessionService } from "./session.service.js";

@Module({
  imports: [RankedConfigModule],
  controllers: [SessionController],
  providers: [SessionService, SessionGuard],
  exports: [SessionService, SessionGuard],
})
export class SessionModule {}
