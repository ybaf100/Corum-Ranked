import { Controller, Get, Header, Inject, Param, ParseUUIDPipe, UseGuards } from "@nestjs/common";
import { CurrentSession } from "../session/current-session.decorator.js";
import { SessionGuard } from "../session/session.guard.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { SurvivalEngineService } from "./survival-engine.service.js";

/** Read-only participant state for future HUD/reconnect adapters. Match creation
 * and telemetry remain internal until M4/M5 admission and session rules exist. */
@Controller("api/ranked/survival/matches")
@UseGuards(SessionGuard)
export class SurvivalEngineController {
  public constructor(@Inject(SurvivalEngineService) private readonly engine:SurvivalEngineService) {}
  @Get(":matchId") @Header("Cache-Control","no-store")
  public state(@Param("matchId",new ParseUUIDPipe()) matchId:string,@CurrentSession() session:RankedSessionContext) {
    return this.engine.viewForPlayer(matchId,session.playerId);
  }
}
