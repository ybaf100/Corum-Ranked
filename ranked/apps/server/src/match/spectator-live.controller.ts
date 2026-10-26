import {
  Body,
  Controller,
  Get,
  Headers,
  Param,
  Post,
  Query,
  UseGuards,
} from "@nestjs/common";
import { CurrentSession } from "../session/current-session.decorator.js";
import { SessionGuard } from "../session/session.guard.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { SpectatorLiveService } from "./spectator-live.service.js";

@Controller("api/ranked/matches/:id/spectator-live")
@UseGuards(SessionGuard)
export class SpectatorLiveController {
  public constructor(private readonly spectatorLive: SpectatorLiveService) {}

  @Post("publish")
  public publish(
    @Param("id") matchId: string,
    @Headers("x-match-token") matchToken: string,
    @CurrentSession() session: RankedSessionContext,
    @Body() body: unknown,
  ) {
    return this.spectatorLive.publish(matchId, matchToken, session, body);
  }

  @Get()
  public subscribe(
    @Param("id") matchId: string,
    @Headers("x-match-token") matchToken: string,
    @CurrentSession() session: RankedSessionContext,
    @Query("protocolVersion") protocolVersion?: string,
    @Query("attemptId") attemptId?: string,
    @Query("generation") generation?: string,
    @Query("afterSequence") afterSequence?: string,
    @Query("worldHistory") worldHistory?: string,
    @Query("cosmeticRevision") cosmeticRevision?: string,
  ) {
    return this.spectatorLive.subscribe(
      matchId,
      matchToken,
      session,
      protocolVersion,
      attemptId,
      generation,
      afterSequence,
      worldHistory,
      cosmeticRevision,
    );
  }
}
