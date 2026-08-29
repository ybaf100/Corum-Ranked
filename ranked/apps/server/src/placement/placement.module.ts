import { Module } from "@nestjs/common";
import { RankedConfigModule } from "../config/ranked-config.module.js";
import { SessionModule } from "../session/session.module.js";
import { PlacementController } from "./placement.controller.js";
import { PlacementService } from "./placement.service.js";

@Module({
  imports: [SessionModule, RankedConfigModule],
  controllers: [PlacementController],
  providers: [PlacementService],
  exports: [PlacementService],
})
export class PlacementModule {}
