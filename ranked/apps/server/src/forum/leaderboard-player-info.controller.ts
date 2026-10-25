import {BadRequestException,Controller,Get,Header,Inject,NotFoundException,Param,UseGuards} from '@nestjs/common';
import {DATABASE,type DatabasePort} from '../database/database.port.js';
import {SessionGuard} from '../session/session.guard.js';
import {ForumRecordsService} from './forum-records.service.js';
import {readPublicPlayerProfiles} from './public-player-profiles.js';
/** Display-only lookup of a registered public player; never schedules a provider refresh. */
@Controller('api/ranked/leaderboard/player-info') @UseGuards(SessionGuard)
export class LeaderboardPlayerInfoController {
 constructor(@Inject(DATABASE)private readonly db:DatabasePort,@Inject(ForumRecordsService)private readonly records:ForumRecordsService){}
 @Get(':accountId') @Header('Cache-Control','no-store')
 async read(@Param('accountId') accountId:string){
  if(!/^[1-9]\d{0,9}$/.test(accountId) || Number(accountId)>2147483647)throw new BadRequestException('Invalid GD account');
  const player=(await this.db.query<{gd_username:string}>('SELECT gd_username FROM ranked_players WHERE gd_account_id=$1',[accountId])).rows[0];
  if(!player)throw new NotFoundException('Ranked player not found');
  return {...await this.records.read(accountId),gdUsername:player.gd_username,profile:(await readPublicPlayerProfiles(this.db,[accountId]))[accountId]??null};
 }
}
