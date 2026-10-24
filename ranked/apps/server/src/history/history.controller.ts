import type { HistoryOrder, HistoryResult } from "./history-options.js";
import { Controller, Get, Header, Query, Param, UseGuards } from "@nestjs/common";
import { IsIn, IsOptional, IsUUID } from "class-validator";
import { CurrentSession } from "../session/current-session.decorator.js";
import { SessionGuard } from "../session/session.guard.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { HistoryService, type HistoryScope } from "./history.service.js";

export class HistoryQueryDto {
  @IsOptional()
  @IsIn(["ALL", "SOLO_RANKED", "SURVIVAL"])
  public scope?: HistoryScope;
  @IsOptional() @IsIn(['true','false']) public summary?:string;
  @IsOptional() @IsIn(['NEWEST','OLDEST']) public order?:HistoryOrder;
  @IsOptional() @IsIn(['ALL','WIN','LOSS','CHAMPION','PODIUM','ELIMINATED','IN_PROGRESS']) public result?:HistoryResult;
}

class HistoryDetailParams {
  @IsIn(['SOLO_RANKED','SURVIVAL']) public mode!: 'SOLO_RANKED'|'SURVIVAL';
  @IsUUID() public matchId!:string;
}

@Controller("api/ranked/history")
@UseGuards(SessionGuard)
export class HistoryController {
  public constructor(private readonly history: HistoryService) {}

  @Get()
  @Header("Cache-Control", "no-store")
  public list(@CurrentSession() session: RankedSessionContext, @Query() query: HistoryQueryDto) {
    return this.history.history(session, query.scope ?? "ALL",{order:query.order,result:query.result,summary:query.summary==='true'});
  }
  @Get(':mode/:matchId')
  @Header('Cache-Control','no-store')
  public detail(@CurrentSession() session:RankedSessionContext,@Param() params:HistoryDetailParams){
    return this.history.detail(session,params.mode,params.matchId);
  }

}
