import {
  Body,
  Controller,
  Get,
  Headers,
  Param,
  Post,
  UseGuards,
} from "@nestjs/common";
import { CurrentSession } from "../session/current-session.decorator.js";
import { SessionGuard } from "../session/session.guard.js";
import type { RankedSessionContext } from "../session/session.types.js";
import {
  AttemptEndDto,
  AttemptProgressDto,
  AttemptStartDto,
  AttemptStartIntentDto,
  ReadyMatchDto,
  ResourceFailureDto,
  SubmitBanDto,
} from "./match.dto.js";
import { MatchService } from "./match.service.js";

@Controller("api/ranked/matches")
@UseGuards(SessionGuard)
export class MatchController {
  public constructor(private readonly matches: MatchService) {}

  @Get()
  public recent(@CurrentSession() session: RankedSessionContext) {
    return this.matches.history(session);
  }

  @Get(":id/state")
  public state(
    @Param("id") matchId: string,
    @Headers("x-match-token") matchToken: string,
    @CurrentSession() session: RankedSessionContext,
  ) {
    return this.matches.state(matchId, matchToken, session);
  }

  @Get(":id")
  public history(
    @Param("id") matchId: string,
    @Headers("x-match-token") matchToken: string,
    @CurrentSession() session: RankedSessionContext,
  ) {
    return this.matches.state(matchId, matchToken, session);
  }

  @Post(":id/ready")
  public ready(
    @Param("id") matchId: string,
    @Headers("x-match-token") matchToken: string,
    @CurrentSession() session: RankedSessionContext,
    @Body() body: ReadyMatchDto,
  ) {
    return this.matches.ready(matchId, matchToken, session, body);
  }

  @Post(":id/ban")
  public ban(
    @Param("id") matchId: string,
    @Headers("x-match-token") matchToken: string,
    @CurrentSession() session: RankedSessionContext,
    @Body() body: SubmitBanDto,
  ) {
    return this.matches.submitBan(matchId, matchToken, session, body);
  }


  @Post(":id/resource-failure")
  public resourceFailure(
    @Param("id") matchId: string,
    @Headers("x-match-token") matchToken: string,
    @CurrentSession() session: RankedSessionContext,
    @Body() body: ResourceFailureDto,
  ) {
    return this.matches.reportResourceFailure(matchId, matchToken, session, body);
  }

  @Post(":id/attempt/intent")
  public startAttemptIntent(
    @Param("id") matchId: string,
    @Headers("x-match-token") matchToken: string,
    @CurrentSession() session: RankedSessionContext,
    @Body() body: AttemptStartIntentDto,
  ) {
    return this.matches.startAttemptIntent(matchId, matchToken, session, body);
  }

  @Post(":id/attempt/start")
  public startAttempt(
    @Param("id") matchId: string,
    @Headers("x-match-token") matchToken: string,
    @CurrentSession() session: RankedSessionContext,
    @Body() body: AttemptStartDto,
  ) {
    return this.matches.startAttempt(matchId, matchToken, session, body);
  }

  @Post(":id/attempt/end")
  public endAttempt(
    @Param("id") matchId: string,
    @Headers("x-match-token") matchToken: string,
    @CurrentSession() session: RankedSessionContext,
    @Body() body: AttemptEndDto,
  ) {
    return this.matches.endAttempt(matchId, matchToken, session, body);
  }

  @Post(":id/attempt/progress")
  public updateAttemptProgress(
    @Param("id") matchId: string,
    @Headers("x-match-token") matchToken: string,
    @CurrentSession() session: RankedSessionContext,
    @Body() body: AttemptProgressDto,
  ) {
    return this.matches.updateAttemptProgress(matchId, matchToken, session, body);
  }

  @Post(":id/heartbeat")
  public heartbeat(
    @Param("id") matchId: string,
    @Headers("x-match-token") matchToken: string,
    @CurrentSession() session: RankedSessionContext,
  ) {
    return this.matches.heartbeat(matchId, matchToken, session);
  }
}
