import {Body,Controller,Get,Inject,Param,ParseUUIDPipe,Post,Query,UseGuards,Header} from '@nestjs/common';
import {SessionGuard} from '../session/session.guard.js';
import {CurrentSession} from '../session/current-session.decorator.js';
import type {RankedSessionContext} from '../session/session.types.js';
import {SurvivalSpectatorLiveService} from './survival-spectator-live.service.js';
@Controller('api/ranked/survival/online/matches/:id/spectator-live')
@UseGuards(SessionGuard)
export class SurvivalSpectatorLiveController {
  constructor(@Inject(SurvivalSpectatorLiveService)private readonly live:SurvivalSpectatorLiveService){}
  @Get('targets') @Header('Cache-Control','no-store')
  targets(@Param('id',new ParseUUIDPipe())id:string,@CurrentSession()s:RankedSessionContext){return this.live.targets(id,s);}
  @Post('publish')
  publish(@Param('id',new ParseUUIDPipe())id:string,@CurrentSession()s:RankedSessionContext,@Body()body:unknown){return this.live.publish(id,s,body);}
  @Get() @Header('Cache-Control','no-store')
  subscribe(@Param('id',new ParseUUIDPipe())id:string,@CurrentSession()s:RankedSessionContext,@Query()q:Record<string,string|undefined>){return this.live.subscribe(id,s,q);}
  @Post('leave')
  leave(@Param('id',new ParseUUIDPipe())id:string,@CurrentSession()s:RankedSessionContext){return this.live.leave(id,s);}
}
