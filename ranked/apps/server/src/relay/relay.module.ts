import { Module } from "@nestjs/common";
import { RuntimeModule } from "../common/runtime.module.js";
import { ServerEnvironmentModule } from "../config/server-environment.module.js";
import { DatabaseModule } from "../database/database.module.js";
import { DiscordRelayWorker } from "./discord-relay.worker.js";
import { DISCORD_TRANSPORT, FetchDiscordTransport } from "./discord-transport.js";
import { OutboxService } from "./outbox.service.js";

@Module({
  imports: [DatabaseModule, RuntimeModule, ServerEnvironmentModule],
  providers: [
    OutboxService,
    DiscordRelayWorker,
    { provide: DISCORD_TRANSPORT, useClass: FetchDiscordTransport },
  ],
  exports: [OutboxService],
})
export class RelayModule {}

