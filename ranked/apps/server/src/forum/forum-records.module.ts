import { Module } from '@nestjs/common';
import { ForumRecordsService } from './forum-records.service.js';
import { FORUM_RECORDS_SOURCE, PublicForumRecordsSource } from './forum-records.source.js';
@Module({
  providers: [ForumRecordsService, { provide: FORUM_RECORDS_SOURCE, useFactory: () => new PublicForumRecordsSource() }],
  exports: [ForumRecordsService],
})
export class ForumRecordsModule {}
