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

const PVP_RESOURCE_DEADLINE_FLOOR_SECONDS = 60;
const PVP_VISIBLE_PREPARE_SECONDS = 10;
const PVP_PREPARE_DEADLINE_OFFSET_MS =
  (PVP_RESOURCE_DEADLINE_FLOOR_SECONDS - PVP_VISIBLE_PREPARE_SECONDS) * 1_000;

const withVisiblePrepareDeadline = <T>(payload: T): T => {
  if (!payload || typeof payload !== "object") return payload;
  const state = payload as Record<string, unknown>;
  if (
    state.state !== "ROUND_PREPARE" &&
    state.state !== "DEATHMATCH_PREPARE"
  ) return payload;
  if (typeof state.deadlineAt !== "string") return payload;
  const resourceDeadlineMs = Date.parse(state.deadlineAt);
  if (!Number.isFinite(resourceDeadlineMs)) return payload;
  return {
    ...state,
    deadlineAt: new Date(resourceDeadlineMs - PVP_PREPARE_DEADLINE_OFFSET_MS).toISOString(),
  } as T;
};

@Controller("api/ranked/matches")
@UseGuards(SessionGuard)
export class MatchController {
  public constructor(private readonly matches: MatchService) {}

  @Get()
  public recent(@CurrentSession() session: RankedSessionContext) {
    return this.matches.history(session);
  }

  @Get(":id/state")
  public async state(
    @Param("id") matchId: string,
    @Headers("x-match-token") matchToken: string,
    @CurrentSession() session: RankedSessionContext,
  ) {
    return withVisiblePrepareDeadline(
      await this.matches.state(matchId, matchToken, session),
    );
  }

  @Get(":id")
  public async history(
    @Param("id") matchId: string,
    @Headers("x-match-token") matchToken: string,
    @CurrentSession() session: RankedSessionContext,
  ) {
    return withVisiblePrepareDeadline(
      await this.matches.state(matchId, matchToken, session),
    );
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
