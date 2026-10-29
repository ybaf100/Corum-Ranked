import {SurvivalBadgesService} from "./survival-badges.service.js";
import { Controller, Get, Header, Post, Query, UseGuards } from "@nestjs/common";
import { LeaderboardPageDto } from "../leaderboard/leaderboard.controller.js";
import { CurrentSession } from "../session/current-session.decorator.js";
import { SessionGuard } from "../session/session.guard.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { SurvivalService } from "./survival.service.js";

@Controller("api/ranked/survival")
export class SurvivalController {
  public constructor(private readonly survival: SurvivalService, private readonly badges:SurvivalBadgesService) {}
  @Get("badges") @UseGuards(SessionGuard) @Header("Cache-Control", "no-store")
  public collection(@CurrentSession() session:RankedSessionContext){return this.badges.collection(session.playerId);}
  @Get("leaderboard") @Header("Cache-Control", "no-store")
  public leaderboard(@Query() query: LeaderboardPageDto) { return this.survival.leaderboard(query.offset ?? 0, query.limit,query.search); }
  @Get("profile") @UseGuards(SessionGuard) @Header("Cache-Control", "no-store")
  public profile(@CurrentSession() session: RankedSessionContext) { return this.survival.profile(session.playerId); }
  @Get("history") @UseGuards(SessionGuard) @Header("Cache-Control", "no-store")
  public history(@CurrentSession() session: RankedSessionContext) { return this.survival.history(session.playerId); }
  @Post("debug-bot/match") @UseGuards(SessionGuard)
  public debugMatch() { return this.survival.rejectNewMatch(); }
}
