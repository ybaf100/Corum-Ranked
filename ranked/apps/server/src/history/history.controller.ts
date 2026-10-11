import { Controller, Get, Header, Query, UseGuards } from "@nestjs/common";
import { IsIn, IsOptional } from "class-validator";
import { CurrentSession } from "../session/current-session.decorator.js";
import { SessionGuard } from "../session/session.guard.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { HistoryService, type HistoryScope } from "./history.service.js";

export class HistoryQueryDto {
  @IsOptional()
  @IsIn(["ALL", "SOLO_RANKED", "SURVIVAL"])
  public scope?: HistoryScope;
}

@Controller("api/ranked/history")
@UseGuards(SessionGuard)
export class HistoryController {
  public constructor(private readonly history: HistoryService) {}

  @Get()
  @Header("Cache-Control", "no-store")
  public list(@CurrentSession() session: RankedSessionContext, @Query() query: HistoryQueryDto) {
    return this.history.history(session, query.scope ?? "ALL");
  }
}
