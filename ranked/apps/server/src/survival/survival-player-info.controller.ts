import {Controller,Get,Header,Inject,Param,ParseUUIDPipe,UseGuards,ForbiddenException,NotFoundException} from '@nestjs/common';
import {CurrentSession} from '../session/current-session.decorator.js';
import {SessionGuard} from '../session/session.guard.js';
import type {RankedSessionContext} from '../session/session.types.js';
import {SurvivalEngineService} from './survival-engine.service.js';
import {SurvivalRateService} from './survival-rate.service.js';
import {ForumRecordsService} from '../forum/forum-records.service.js';
/** Read the existing forum cache for this match's exact participant; never
 * accept a client-selected GD account or trigger external refreshes here. */
@Controller('api/ranked/survival/matches')
@UseGuards(SessionGuard)
export class SurvivalPlayerInfoController {
 constructor(@Inject(SurvivalEngineService)private readonly engine:SurvivalEngineService,
  @Inject(SurvivalRateService)private readonly rates:SurvivalRateService,
  @Inject(ForumRecordsService)private readonly records:ForumRecordsService){}
 @Get(':matchId/player-info/:participantId') @Header('Cache-Control','no-store')
 async read(@Param('matchId',new ParseUUIDPipe()) matchId:string,@Param('participantId') participantId:string,@CurrentSession() session:RankedSessionContext){
  await this.rates.check(session.playerId,'state');
  const match=await this.engine.get(matchId);
  if(!match.players.some(p=>p.playerId===session.playerId))throw new ForbiddenException('Not the Survival participant');
  const player=match.players.find(p=>p.participantId===participantId);
  if(!player)throw new NotFoundException('Survival participant not found');
  if(player.kind==='BOT' || !player.gdAccountId)return {participantId,gdAccountId:null,gdUsername:player.name,bot:true,records:[]};
  return {...await this.records.read(player.gdAccountId),participantId,gdUsername:player.name,bot:false};
 }
}
