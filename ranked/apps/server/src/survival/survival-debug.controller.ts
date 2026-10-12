import { Body, Controller, Get, Header, Inject, Param, ParseUUIDPipe, Post, UseGuards } from "@nestjs/common";
import { SessionGuard } from "../session/session.guard.js";
import { AdmissionVersionGuard } from "../session/admission-version.guard.js";
import { CurrentSession } from "../session/current-session.decorator.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { SurvivalDebugService } from "./survival-debug.service.js";
import { CreateSurvivalDebugDto, ForceSurvivalDebugDto, SurvivalDebugEventDto } from "./survival-debug.dto.js";

@Controller("api/ranked/survival/debug") @UseGuards(SessionGuard)
export class SurvivalDebugController {
  public constructor(@Inject(SurvivalDebugService) private readonly debug:SurvivalDebugService) {}
  @Post("matches") @UseGuards(AdmissionVersionGuard)
  public create(@CurrentSession() session:RankedSessionContext,@Body() body:CreateSurvivalDebugDto) { return this.debug.create(session,body); }
  @Get("active") @Header("Cache-Control","no-store")
  public active(@CurrentSession() session:RankedSessionContext) { return this.debug.active(session); }
  @Get("matches/:id") @Header("Cache-Control","no-store")
  public state(@Param("id",new ParseUUIDPipe()) id:string,@CurrentSession() session:RankedSessionContext) { return this.debug.view(id,session); }
  @Post("matches/:id/events")
  public event(@Param("id",new ParseUUIDPipe()) id:string,@CurrentSession() session:RankedSessionContext,@Body() body:SurvivalDebugEventDto) {
    return this.debug.event(id,session,body);
  }
  @Post("matches/:id/force-rank")
  public force(@Param("id",new ParseUUIDPipe()) id:string,@CurrentSession() session:RankedSessionContext,@Body() body:ForceSurvivalDebugDto) {
    return this.debug.force(id,session,body);
  }
  @Post("matches/:id/acknowledge")
  public acknowledge(@Param("id",new ParseUUIDPipe()) id:string,@CurrentSession() session:RankedSessionContext) { return this.debug.acknowledge(id,session); }
}
