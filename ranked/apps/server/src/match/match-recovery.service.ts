import {
  Inject,
  Injectable,
  Logger,
  type OnApplicationBootstrap,
} from "@nestjs/common";
import { SERVER_CLOCK, type ServerClock } from "../common/runtime.module.js";
import type { RankedConfigSnapshot } from "../config/ranked-config.document.js";
import { DATABASE, type DatabasePort } from "../database/database.port.js";

interface RecoverableMatchRow {
  readonly id: string;
  readonly source_payload: RankedConfigSnapshot | string;
}

const parseSnapshot = (value: RankedConfigSnapshot | string): RankedConfigSnapshot =>
  typeof value === "string" ? (JSON.parse(value) as RankedConfigSnapshot) : structuredClone(value);

@Injectable()
export class MatchRecoveryService implements OnApplicationBootstrap {
  private readonly logger = new Logger(MatchRecoveryService.name);

  public constructor(
    @Inject(DATABASE) private readonly database: DatabasePort,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
  ) {}

  public async onApplicationBootstrap(): Promise<void> {
    await this.recover();
  }

  public async recover(): Promise<{ cancelled: number; resumed: number }> {
    const now = this.clock.now();
    const result = await this.database.transaction(async (transaction) => {
      const matches = await transaction.query<RecoverableMatchRow>(
        `SELECT match.id, snapshot.source_payload
         FROM ranked_matches match
         JOIN ranked_config_snapshots snapshot ON snapshot.id = match.config_snapshot_id
         WHERE match.match_type = 'RANKED_PVP'
           AND match.state NOT IN ('MATCH_RESULT', 'CANCELLED')
         ORDER BY match.created_at, match.id
         FOR UPDATE OF match`,
      );
      let cancelled = 0;
      let resumed = 0;
      for (const match of matches.rows) {
        const snapshot = parseSnapshot(match.source_payload);
        const action = snapshot.operational.failurePolicy?.restartRecoveryAction;
        if (action === "RESUME") {
          resumed += 1;
          continue;
        }
        const update = await transaction.query(
          `UPDATE ranked_matches
           SET state = 'CANCELLED', cancellation_reason = 'SERVER_RESTART_RECOVERY',
               finished_at = $2, deadline_at = NULL,
               state_version = state_version + 1
           WHERE id = $1 AND state NOT IN ('MATCH_RESULT', 'CANCELLED')`,
          [match.id, now.toISOString()],
        );
        cancelled += update.rowCount;
      }
      return { cancelled, resumed };
    });
    if (result.cancelled > 0 || result.resumed > 0) {
      this.logger.log(
        `Ranked restart recovery: cancelled=${result.cancelled}, resumed=${result.resumed}`,
      );
    }
    return result;
  }
}
