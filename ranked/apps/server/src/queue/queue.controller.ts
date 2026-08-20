import { Body, Controller, Get, Post, UseGuards } from "@nestjs/common";
import { CurrentSession } from "../session/current-session.decorator.js";
import { EnvironmentRecheckDto } from "../session/session.dto.js";
import { SessionGuard } from "../session/session.guard.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { QueueService } from "./queue.service.js";

@Controller("api/ranked/queue")
@UseGuards(SessionGuard)
export class QueueController {
  public constructor(private readonly queue: QueueService) {}

  @Post("join")
  public join(
    @CurrentSession() session: RankedSessionContext,
    @Body() body: EnvironmentRecheckDto,
  ) {
    return this.queue.join(session, body);
  }

  @Post("leave")
  public leave(@CurrentSession() session: RankedSessionContext) {
    return this.queue.leave(session);
  }

  @Post("heartbeat")
  public heartbeat(@CurrentSession() session: RankedSessionContext) {
    return this.queue.heartbeat(session);
  }

  @Get("status")
  public status(@CurrentSession() session: RankedSessionContext) {
    return this.queue.status(session);
  }
}
