import { Body, Controller, Get, Header, Param, Post, UseGuards } from "@nestjs/common";
import { CurrentSession } from "../session/current-session.decorator.js";
import { SessionGuard } from "../session/session.guard.js";
import { AdmissionVersionGuard } from "../session/admission-version.guard.js";
import { EnvironmentRecheckDto } from "../session/session.dto.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { DebugChallengeSendDto, fixedMapRequestOf } from "./debug-bot.dto.js";
import { DebugChallengeService } from "./debug-challenge.service.js";

// Debug clients only: designated-map challenges between players in the lobby.
@Controller("api/ranked/debug/challenges")
@UseGuards(SessionGuard)
export class DebugChallengeController {
  public constructor(private readonly challenges: DebugChallengeService) {}

  @Get("lobby")
  @Header("Cache-Control", "no-store")
  public lobby(@CurrentSession() session: RankedSessionContext) { return this.challenges.lobby(session); }

  @Get()
  @Header("Cache-Control", "no-store")
  public state(@CurrentSession() session: RankedSessionContext) { return this.challenges.state(session); }

  @Post()
  @UseGuards(AdmissionVersionGuard)
  public send(@CurrentSession() session: RankedSessionContext, @Body() body: DebugChallengeSendDto) {
    return this.challenges.send(session, { ...body, fixedMaps: fixedMapRequestOf(body.fixedMaps)! });
  }

  @Post(":id/accept")
  @UseGuards(AdmissionVersionGuard)
  public accept(@CurrentSession() session: RankedSessionContext, @Param("id") id: string, @Body() body: EnvironmentRecheckDto) {
    return this.challenges.accept(session, id, body);
  }

  @Post(":id/decline")
  public decline(@CurrentSession() session: RankedSessionContext, @Param("id") id: string) { return this.challenges.decline(session, id); }

  @Post(":id/cancel")
  public cancel(@CurrentSession() session: RankedSessionContext, @Param("id") id: string) { return this.challenges.cancel(session, id); }
}
