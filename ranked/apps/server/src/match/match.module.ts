import { Module } from "@nestjs/common";
import { SessionModule } from "../session/session.module.js";
import { RelayModule } from "../relay/relay.module.js";
import { MatchAccessService } from "./match-access.service.js";
import { MatchController } from "./match.controller.js";
import { MatchRecoveryService } from "./match-recovery.service.js";
import {
  InMemoryMatchRuntimeState,
  MATCH_RUNTIME_STATE,
} from "./match-runtime-state.js";
import { MatchService } from "./match.service.js";
import { SpectatorLiveController } from "./spectator-live.controller.js";
import { SpectatorLiveService } from "./spectator-live.service.js";

@Module({
  imports: [SessionModule, RelayModule],
  controllers: [MatchController, SpectatorLiveController],
  providers: [
    MatchAccessService,
    MatchService,
    MatchRecoveryService,
    SpectatorLiveService,
    { provide: MATCH_RUNTIME_STATE, useClass: InMemoryMatchRuntimeState },
  ],
  exports: [MatchService, SpectatorLiveService],
})
export class MatchModule {}
