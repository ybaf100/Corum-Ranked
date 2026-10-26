import { Body, Controller, Get, Headers, Inject, Param, Post, Query, UseGuards } from "@nestjs/common";
import { CurrentSession } from "../session/current-session.decorator.js";
import { SessionGuard } from "../session/session.guard.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { DebugSpectateService } from "./debug-spectate.service.js";

@Controller("api/ranked/debug/spectate")
export class DebugSpectateController {
  public constructor(@Inject(DebugSpectateService) private readonly debug: DebugSpectateService) {}
  @Get("live-setting") @UseGuards(SessionGuard)
  public setting(@Headers("x-corum-debug-spectate") client?: string) {
    return this.debug.liveSetting(client);
  }
  @Post("live-setting") @UseGuards(SessionGuard)
  public setSetting(@Body() body: unknown, @Headers("x-corum-debug-spectate") client?: string) {
    return this.debug.setLiveSetting(body, client);
  }
  @Post("recordings") @UseGuards(SessionGuard)
  public record(@CurrentSession() session: RankedSessionContext, @Body() body: unknown,
    @Headers("x-corum-debug-spectate") client?: string) { return this.debug.createRecording(session, body, client); }
  @Post("recordings/:id/frames")
  public frames(@Param("id") id: string, @Body() body: unknown,
    @Headers("x-debug-spectate-token") token?: string, @Headers("x-corum-debug-spectate") client?: string) {
    return this.debug.upload(id, token, body, client);
  }
  @Post("recordings/:id/finalize")
  public finalize(@Param("id") id: string, @Body() body: unknown,
    @Headers("x-debug-spectate-token") token?: string, @Headers("x-corum-debug-spectate") client?: string) {
    return this.debug.finalize(id, token, body, client);
  }
  @Post("playbacks") @UseGuards(SessionGuard)
  public playback(@CurrentSession() session: RankedSessionContext, @Body() body: unknown,
    @Headers("x-corum-debug-spectate") client?: string) { return this.debug.createPlayback(session, body, client); }
  @Get("playbacks/:id/spectator-live")
  public stream(@Param("id") id: string, @Headers("x-debug-spectate-token") token?: string,
    @Headers("x-corum-debug-spectate") client?: string, @Query("protocolVersion") protocol?: string,
    @Query("attemptId") attemptId?: string, @Query("generation") generation?: string,
    @Query("afterSequence") after?: string, @Query("cosmeticRevision") cosmetics?:string, @Query("worldHistory") history?:string) { return this.debug.subscribe(id, token, client, protocol, attemptId, generation, after, cosmetics,history); }
  @Post("playbacks/:id/close")
  public close(@Param("id") id: string, @Headers("x-debug-spectate-token") token?: string,
    @Headers("x-corum-debug-spectate") client?: string) { return this.debug.closePlayback(id, token, client); }
}
