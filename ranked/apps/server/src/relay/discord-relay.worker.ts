import {
  Inject,
  Injectable,
  Logger,
  type OnApplicationBootstrap,
  type OnModuleDestroy,
} from "@nestjs/common";
import { SERVER_CLOCK, type ServerClock } from "../common/runtime.module.js";
import {
  SERVER_ENVIRONMENT,
  type DiscordRelayEnvironment,
  type ServerEnvironment,
} from "../config/server-environment.js";
import { DATABASE, type DatabasePort } from "../database/database.port.js";
import { formatDiscordRelayMessage } from "./discord-message.js";
import { DISCORD_TRANSPORT, type DiscordTransport } from "./discord-transport.js";
import type { RankedRelayEventType } from "./outbox.service.js";

interface OutboxRow {
  readonly id: string;
  readonly event_type: RankedRelayEventType;
  readonly payload: unknown;
  readonly attempts: number;
}

@Injectable()
export class DiscordRelayWorker implements OnApplicationBootstrap, OnModuleDestroy {
  private readonly logger = new Logger(DiscordRelayWorker.name);
  private readonly config: DiscordRelayEnvironment | null;
  private timer: NodeJS.Timeout | null = null;
  private running = false;

  public constructor(
    @Inject(DATABASE) private readonly database: DatabasePort,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
    @Inject(SERVER_ENVIRONMENT) environment: ServerEnvironment,
    @Inject(DISCORD_TRANSPORT) private readonly transport: DiscordTransport,
  ) {
    this.config = environment.discordRelay;
  }

  public onApplicationBootstrap(): void {
    if (!this.config) {
      this.logger.log("Discord Ranked relay is disabled");
      return;
    }
    this.timer = setInterval(() => void this.deliverOnce(), this.config.pollMs);
    this.timer.unref();
    void this.deliverOnce();
    this.logger.log("Discord Ranked relay worker started");
  }

  public onModuleDestroy(): void {
    if (this.timer) clearInterval(this.timer);
    this.timer = null;
  }

  public async deliverOnce(): Promise<void> {
    if (!this.config || this.running) return;
    this.running = true;
    try {
      const rows = await this.claim(this.config);
      for (const row of rows) await this.deliver(row, this.config);
    } catch (error) {
      this.logger.error(`Discord outbox iteration failed: ${this.errorText(error)}`);
    } finally {
      this.running = false;
    }
  }

  private async claim(config: DiscordRelayEnvironment): Promise<readonly OutboxRow[]> {
    const now = this.clock.now();
    const leasedUntil = new Date(now.getTime() + config.leaseMs);
    return this.database.transaction(async (transaction) => {
      const result = await transaction.query<OutboxRow>(
        `WITH pending AS (
           SELECT id
           FROM ranked_outbox_events
           WHERE delivered_at IS NULL AND abandoned_at IS NULL
             AND available_at <= $1 AND attempts < $2
           ORDER BY created_at, id
           FOR UPDATE SKIP LOCKED
           LIMIT $3
         )
         UPDATE ranked_outbox_events event
         SET attempts = event.attempts + 1, available_at = $4
         FROM pending
         WHERE event.id = pending.id
         RETURNING event.id, event.event_type, event.payload, event.attempts`,
        [now.toISOString(), config.maximumAttempts, config.batchSize, leasedUntil.toISOString()],
      );
      return result.rows;
    });
  }

  private async deliver(row: OutboxRow, config: DiscordRelayEnvironment): Promise<void> {
    try {
      const content = formatDiscordRelayMessage({ eventType: row.event_type, payload: row.payload });
      await this.transport.send(config.webhookUrl, content, config.requestTimeoutMs);
      await this.database.query(
        `UPDATE ranked_outbox_events
         SET delivered_at = $2, last_error = NULL
         WHERE id = $1 AND delivered_at IS NULL`,
        [row.id, this.clock.now().toISOString()],
      );
    } catch (error) {
      const message = this.errorText(error).slice(0, 500);
      const finalAttempt = row.attempts >= config.maximumAttempts;
      const retryDelay = Math.min(
        config.retryMaximumMs,
        config.retryBaseMs * 2 ** Math.max(0, row.attempts - 1),
      );
      const now = this.clock.now();
      await this.database.query(
        `UPDATE ranked_outbox_events
         SET last_error = $2,
             available_at = $3,
             abandoned_at = CASE WHEN $4 THEN $5 ELSE abandoned_at END
         WHERE id = $1 AND delivered_at IS NULL`,
        [
          row.id,
          message,
          new Date(now.getTime() + retryDelay).toISOString(),
          finalAttempt,
          now.toISOString(),
        ],
      );
      this.logger.warn(
        `Discord relay event ${row.id} failed on attempt ${row.attempts}` +
          (finalAttempt ? " and was abandoned" : " and will retry") +
          `: ${message}`,
      );
    }
  }

  private errorText(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
  }
}

