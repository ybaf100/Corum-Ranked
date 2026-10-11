import { Controller, Get, Header, Query } from "@nestjs/common";
import { Type } from "class-transformer";
import { IsInt, IsOptional, Max, Min } from "class-validator";
import { LeaderboardService } from "./leaderboard.service.js";

export class LeaderboardPageDto {
  @IsOptional() @Type(() => Number) @IsInt() @Min(0) @Max(1_000_000)
  public offset?: number;
  @IsOptional() @Type(() => Number) @IsInt() @Min(1) @Max(50)
  public limit?: number;
}

@Controller("api/ranked/leaderboard")
export class LeaderboardController {
  public constructor(private readonly leaderboard: LeaderboardService) {}

  @Get()
  @Header("Cache-Control", "public, max-age=15, stale-while-revalidate=45")
  public list(@Query() query: LeaderboardPageDto) {
    return this.leaderboard.list(query.offset ?? 0, query.limit);
  }
}
