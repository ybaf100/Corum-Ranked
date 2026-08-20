import { Module } from "@nestjs/common";
import { SessionModule } from "../session/session.module.js";
import { RankedConfigModule } from "../config/ranked-config.module.js";
import { QueueController } from "./queue.controller.js";
import { QueueService } from "./queue.service.js";

@Module({
  imports: [SessionModule, RankedConfigModule],
  controllers: [QueueController],
  providers: [QueueService],
  exports: [QueueService],
})
export class QueueModule {}
