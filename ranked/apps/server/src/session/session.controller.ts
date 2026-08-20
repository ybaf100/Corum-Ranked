import { Body, Controller, Post, UseGuards } from "@nestjs/common";
import { CurrentSession } from "./current-session.decorator.js";
import { CreateSessionDto } from "./session.dto.js";
import { SessionGuard } from "./session.guard.js";
import { SessionService } from "./session.service.js";
import type { RankedSessionContext } from "./session.types.js";

@Controller("api/ranked/session")
export class SessionController {
  public constructor(private readonly sessions: SessionService) {}

  @Post()
  public create(@Body() body: CreateSessionDto) {
    return this.sessions.create(body);
  }

  @Post("heartbeat")
  @UseGuards(SessionGuard)
  public heartbeat(@CurrentSession() session: RankedSessionContext) {
    return this.sessions.heartbeat(session);
  }
}
