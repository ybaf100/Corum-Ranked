import { Body,Controller,Get,Header,Inject,Post,UseGuards } from "@nestjs/common";
import { SessionGuard } from "../session/session.guard.js";
import { AdmissionVersionGuard } from "../session/admission-version.guard.js";
import { CurrentSession } from "../session/current-session.decorator.js";
import { EnvironmentRecheckDto } from "../session/session.dto.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { SurvivalQueueService } from "./survival-queue.service.js";
import { SurvivalRateService } from "./survival-rate.service.js";
import { FillSurvivalBotsDto } from "./survival-online.dto.js";
@Controller("api/ranked/survival/queue") @UseGuards(SessionGuard)
export class SurvivalQueueController {
  public constructor(@Inject(SurvivalQueueService)private readonly queue:SurvivalQueueService,@Inject(SurvivalRateService)private readonly rates:SurvivalRateService){}
  @Post("join") @UseGuards(AdmissionVersionGuard)
  public async join(@CurrentSession()s:RankedSessionContext,@Body()body:EnvironmentRecheckDto){await this.rates.check(s.playerId,"queue");return this.queue.join(s,body);}
  @Post("leave") public async leave(@CurrentSession()s:RankedSessionContext){await this.rates.check(s.playerId,"queue");return this.queue.leave(s);}
  @Post("heartbeat") public async heartbeat(@CurrentSession()s:RankedSessionContext){await this.rates.check(s.playerId,"queue");return this.queue.heartbeat(s);}
  @Get("status") @Header("Cache-Control","no-store")
  public async status(@CurrentSession()s:RankedSessionContext){await this.rates.check(s.playerId,"state");return this.queue.status(s);}
  @Post("fill-bots") @UseGuards(AdmissionVersionGuard)
  public async fill(@CurrentSession()s:RankedSessionContext,@Body()body:FillSurvivalBotsDto){await this.rates.check(s.playerId,"fill");return this.queue.fill(s,body);}
}
