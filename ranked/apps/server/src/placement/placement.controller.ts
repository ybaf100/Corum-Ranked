import { Body, Controller, Get, Param, Post, UseGuards } from "@nestjs/common";
import { CurrentSession } from "../session/current-session.decorator.js";
import { SessionGuard } from "../session/session.guard.js";
import type { RankedSessionContext } from "../session/session.types.js";
import {
  PlacementAttemptEndDto,
  PlacementAttemptProgressDto,
  PlacementAttemptStartDto,
  ReadyPlacementMapDto,
  SkipPlacementDto,
  StartPlacementDto,
} from "./placement.dto.js";
import { PlacementService } from "./placement.service.js";

@Controller("api/ranked/placement")
@UseGuards(SessionGuard)
export class PlacementController {
  public constructor(private readonly placement: PlacementService) {}

  @Get("active")
  public active(@CurrentSession() session: RankedSessionContext) {
    return this.placement.active(session);
  }

  @Post("start")
  public start(
    @CurrentSession() session: RankedSessionContext,
    @Body() body: StartPlacementDto,
  ) {
    return this.placement.start(session, body);
  }

  @Post("skip")
  public skip(
    @CurrentSession() session: RankedSessionContext,
    @Body() body: SkipPlacementDto,
  ) {
    return this.placement.skip(session, body);
  }

  @Get(":id/state")
  public state(
    @Param("id") runId: string,
    @CurrentSession() session: RankedSessionContext,
  ) {
    return this.placement.state(runId, session);
  }

  @Post(":id/ready")
  public ready(
    @Param("id") runId: string,
    @CurrentSession() session: RankedSessionContext,
    @Body() body: ReadyPlacementMapDto,
  ) {
    return this.placement.ready(runId, session, body);
  }

  @Post(":id/attempt/start")
  public startAttempt(
    @Param("id") runId: string,
    @CurrentSession() session: RankedSessionContext,
    @Body() body: PlacementAttemptStartDto,
  ) {
    return this.placement.startAttempt(runId, session, body);
  }

  @Post(":id/attempt/progress")
  public progress(
    @Param("id") runId: string,
    @CurrentSession() session: RankedSessionContext,
    @Body() body: PlacementAttemptProgressDto,
  ) {
    return this.placement.updateAttemptProgress(runId, session, body);
  }

  @Post(":id/attempt/end")
  public endAttempt(
    @Param("id") runId: string,
    @CurrentSession() session: RankedSessionContext,
    @Body() body: PlacementAttemptEndDto,
  ) {
    return this.placement.endAttempt(runId, session, body);
  }

  @Post(":id/continue")
  public continueResult(
    @Param("id") runId: string,
    @CurrentSession() session: RankedSessionContext,
  ) {
    return this.placement.continueResult(runId, session);
  }

  @Post(":id/heartbeat")
  public heartbeat(
    @Param("id") runId: string,
    @CurrentSession() session: RankedSessionContext,
  ) {
    return this.placement.heartbeat(runId, session);
  }

  @Post(":id/result/acknowledge")
  public acknowledgeResult(
    @Param("id") runId: string,
    @CurrentSession() session: RankedSessionContext,
  ) {
    return this.placement.acknowledgeResult(runId, session);
  }
}
