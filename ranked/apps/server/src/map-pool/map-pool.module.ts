import { Module } from "@nestjs/common";
import { RankedConfigModule } from "../config/ranked-config.module.js";
import { SessionModule } from "../session/session.module.js";
import { MapPoolController } from "./map-pool.controller.js";
import { MapPoolService } from "./map-pool.service.js";

@Module({
  imports: [RankedConfigModule, SessionModule],
  controllers: [MapPoolController],
  providers: [MapPoolService],
})
export class MapPoolModule {}
