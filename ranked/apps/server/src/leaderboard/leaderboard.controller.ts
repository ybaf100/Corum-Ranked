import { Controller, Get, Header } from "@nestjs/common";
import { LeaderboardService } from "./leaderboard.service.js";

@Controller("api/ranked/leaderboard")
export class LeaderboardController {
  public constructor(private readonly leaderboard: LeaderboardService) {}

  @Get()
  @Header("Cache-Control", "public, max-age=15, stale-while-revalidate=45")
  public list() {
    return this.leaderboard.list();
  }
}
