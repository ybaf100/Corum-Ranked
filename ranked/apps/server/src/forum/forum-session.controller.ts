import { Controller, Get, Post, UseGuards } from '@nestjs/common';
import { CurrentSession } from '../session/current-session.decorator.js';
import { SessionGuard } from '../session/session.guard.js';
import type { RankedSessionContext } from '../session/session.types.js';
import { ForumRecordsService } from './forum-records.service.js';
@Controller('api/ranked/session/forum-records')
@UseGuards(SessionGuard)
export class ForumSessionController {
  public constructor(private readonly records: ForumRecordsService) {}
  @Get()
  public read(@CurrentSession() session: RankedSessionContext) { return this.records.read(session.gdAccountId); }
  @Post('refresh')
  public refresh(@CurrentSession() session: RankedSessionContext) {
    return this.records.refreshOwn(session.gdAccountId, session.gdUsername);
  }
}
