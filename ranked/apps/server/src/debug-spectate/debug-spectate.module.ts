import { Module } from "@nestjs/common";
import { MatchModule } from "../match/match.module.js";
import { SessionModule } from "../session/session.module.js";
import { DebugSpectateController } from "./debug-spectate.controller.js";
import { DebugSpectateService } from "./debug-spectate.service.js";
@Module({ imports: [SessionModule, MatchModule], controllers: [DebugSpectateController], providers: [DebugSpectateService] })
export class DebugSpectateModule {}
