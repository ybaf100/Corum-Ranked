import { Body,Controller,Get,Header,Inject,Param,ParseUUIDPipe,Post,Query,UseGuards,UseFilters } from "@nestjs/common";
import { IsInt,IsOptional,IsString,Length,Min } from "class-validator";
import { Type } from "class-transformer";
import { SessionGuard } from "../session/session.guard.js";
import { CurrentSession } from "../session/current-session.decorator.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { SurvivalOnlineEventDto } from "./survival-online.dto.js";
import { SurvivalOnlineService } from "./survival-online.service.js";
import { SurvivalRateService } from "./survival-rate.service.js";
import { SurvivalDomainFilter } from "./survival-domain.filter.js";
export class SurvivalObservedRoundDto {
  @IsOptional() @IsString() @Length(1,80) public roundKey?:string;
  @IsOptional() @Type(()=>Number) @IsInt() @Min(1) public mapGeneration?:number;
}
@Controller("api/ranked/survival/online") @UseGuards(SessionGuard)
@UseFilters(SurvivalDomainFilter)
export class SurvivalOnlineController {
  public constructor(@Inject(SurvivalOnlineService)private readonly online:SurvivalOnlineService,@Inject(SurvivalRateService)private readonly rates:SurvivalRateService){}
  @Get("active") @Header("Cache-Control","no-store")
  public async active(@CurrentSession() session:RankedSessionContext){await this.rates.check(session.playerId,"state");return this.online.active(session);}
  @Get("matches/:id") @Header("Cache-Control","no-store")
  public async view(@Param("id",new ParseUUIDPipe())id:string,@CurrentSession() session:RankedSessionContext,@Query() query:SurvivalObservedRoundDto){
    await this.rates.check(session.playerId,"state");return this.online.view(id,session,query.roundKey,query.mapGeneration);
  }
  @Post("matches/:id/events")
  public async event(@Param("id",new ParseUUIDPipe())id:string,@CurrentSession() session:RankedSessionContext,@Body() body:SurvivalOnlineEventDto){
    await this.rates.check(session.playerId,"event");return this.online.event(id,session,body);
  }
  @Post("matches/:id/acknowledge")
  public async acknowledge(@Param("id",new ParseUUIDPipe())id:string,@CurrentSession() session:RankedSessionContext){await this.rates.check(session.playerId,"queue");return this.online.acknowledge(id,session);}
}
