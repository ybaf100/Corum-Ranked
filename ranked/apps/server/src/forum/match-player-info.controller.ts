import { BadRequestException, Controller, Get, Headers, Inject, Param, UseGuards } from '@nestjs/common';
import { DATABASE, type DatabasePort } from '../database/database.port.js';
import { MatchAccessService } from '../match/match-access.service.js';
import { CurrentSession } from '../session/current-session.decorator.js';
import { SessionGuard } from '../session/session.guard.js';
import type { RankedSessionContext } from '../session/session.types.js';
import { ForumRecordsService } from './forum-records.service.js';
import { COJONG } from './forum-records.source.js';
import {readPublicPlayerProfiles} from './public-player-profiles.js';

@Controller('api/ranked/matches')
@UseGuards(SessionGuard)
export class MatchPlayerInfoController {
  public constructor(@Inject(DATABASE) private readonly database: DatabasePort,
    private readonly access: MatchAccessService, private readonly records: ForumRecordsService) {}
  @Get(':id/player-info/:side')
  public async read(@Param('id') id: string, @Param('side') side: string,
    @Headers('x-match-token') token: string, @CurrentSession() session: RankedSessionContext) {
    if (side !== 'A' && side !== 'B') throw new BadRequestException('Invalid player side');
    await this.access.authorize(this.database, id, session, token);
    const result = await this.database.query<{
      match_type: string; account_a: string; account_b: string; name_a: string; name_b: string;
    }>(`SELECT m.match_type, a.gd_account_id::text AS account_a, b.gd_account_id::text AS account_b,
                 a.gd_username AS name_a, b.gd_username AS name_b
        FROM ranked_matches m JOIN ranked_players a ON a.id = m.player_a_id
        JOIN ranked_players b ON b.id = m.player_b_id WHERE m.id = $1`, [id]);
    const row = result.rows[0]!;
    const botReference = row.match_type === 'DEBUG_BOT' && side === 'B';
    const accountId = botReference ? COJONG.accountId : side === 'A' ? row.account_a : row.account_b;
    const username = botReference ? COJONG.username : side === 'A' ? row.name_a : row.name_b;
    return { ...(await this.records.read(accountId)), gdUsername: username, botReference,
      profile:botReference?null:(await readPublicPlayerProfiles(this.database,[accountId]))[accountId]??null };
  }
}
