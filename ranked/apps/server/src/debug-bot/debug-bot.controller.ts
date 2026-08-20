import { Body, Controller, Post, UseGuards } from "@nestjs/common";
import { CurrentSession } from "../session/current-session.decorator.js";
import { SessionGuard } from "../session/session.guard.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { CreateDebugBotMatchDto } from "./debug-bot.dto.js";
import { DebugBotService } from "./debug-bot.service.js";

@Controller("api/ranked/debug")
@UseGuards(SessionGuard)
export class DebugBotController {
  public constructor(private readonly debugBot: DebugBotService) {}

  @Post("bot-match")
  public create(
    @CurrentSession() session: RankedSessionContext,
    @Body() body: CreateDebugBotMatchDto,
  ) {
    return this.debugBot.create(session, body);
  }
}
