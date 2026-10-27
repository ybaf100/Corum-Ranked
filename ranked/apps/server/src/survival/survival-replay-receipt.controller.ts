import {Controller,Get,Header,Inject,Param,ParseUUIDPipe,UseGuards,ForbiddenException} from '@nestjs/common';
import {type SurvivalMatch} from '@corum-ranked/rules';
import {DATABASE,type DatabasePort} from '../database/database.port.js';
import {SessionGuard} from '../session/session.guard.js';
import {CurrentSession} from '../session/current-session.decorator.js';
import type {RankedSessionContext} from '../session/session.types.js';
import {SurvivalRateService} from './survival-rate.service.js';
import {survivalReplayReceipt} from './survival-replay-receipt.js';
@Controller('api/ranked/survival/replays') @UseGuards(SessionGuard)
export class SurvivalReplayReceiptController{
 constructor(@Inject(DATABASE)private readonly db:DatabasePort,@Inject(SurvivalRateService)private readonly rate:SurvivalRateService){}
 @Get('matches/:id/attempts/:attempt') @Header('Cache-Control','no-store')
 async receipt(@Param('id',new ParseUUIDPipe())id:string,@Param('attempt',new ParseUUIDPipe())attempt:string,@CurrentSession()session:RankedSessionContext){
  await this.rate.check(session.playerId,'state');
  const row=(await this.db.query<{snapshot:SurvivalMatch;participant_id:string}>(`SELECT m.snapshot,p.participant_id FROM sur_matches m
    JOIN sur_online_players p ON p.match_id=m.id WHERE m.id=$1 AND p.player_id=$2
    UNION ALL SELECT m.snapshot,'human' AS participant_id FROM sur_matches m JOIN sur_debug_matches d ON d.match_id=m.id
    WHERE m.id=$1 AND d.human_player_id=$2 AND $3='debug'`,[id,session.playerId,session.clientBuildChannel])).rows[0];
  if(!row)throw new ForbiddenException('Not your Survival record');
  return survivalReplayReceipt(row.snapshot,row.participant_id,attempt);
 }
}
