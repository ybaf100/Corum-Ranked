import { Module } from "@nestjs/common";
import { SessionModule } from "../session/session.module.js";
import { AchievementModule } from "../achievement/achievement.module.js";
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
import { SPECTATOR_AUTHORITY_CACHE_MS, SpectatorLiveService } from "./spectator-live.service.js";
import { ForumRecordsModule } from "../forum/forum-records.module.js";
import { MatchPlayerInfoController } from "../forum/match-player-info.controller.js";

@Module({
  imports: [SessionModule, RelayModule, AchievementModule, ForumRecordsModule],
  controllers: [MatchController, SpectatorLiveController, MatchPlayerInfoController],
  providers: [
    MatchAccessService,
    MatchService,
    MatchRecoveryService,
    SpectatorLiveService,
    { provide: MATCH_RUNTIME_STATE, useClass: InMemoryMatchRuntimeState },
    // Spectator relay reuses one locked authority read for at most 250 ms.
    { provide: SPECTATOR_AUTHORITY_CACHE_MS, useValue: 250 },
  ],
  exports: [MatchService, SpectatorLiveService],
})
export class MatchModule {}

