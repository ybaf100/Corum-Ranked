import { Module } from "@nestjs/common";
import { SessionModule } from "../session/session.module.js";
import { EliminationEffectController } from "./elimination-effect.controller.js";
import { EliminationEffectService } from "./elimination-effect.service.js";

@Module({
  imports: [SessionModule],
  controllers: [EliminationEffectController],
  providers: [EliminationEffectService],
  exports: [EliminationEffectService],
})
export class CosmeticsModule {}
