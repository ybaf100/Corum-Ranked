import { Body, ConflictException, Controller, HttpCode, Inject, Post, UseGuards } from "@nestjs/common";
import { CurrentSession } from "../session/current-session.decorator.js";
import { SessionGuard } from "../session/session.guard.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { DATABASE, type DatabasePort } from "../database/database.port.js";
import { SERVER_CLOCK, type ServerClock } from "../common/runtime.module.js";
import { DebugLogChunkDto } from "./debug-logs.dto.js";

const UPLOADS_PER_DAY = 12;

/** "Send recent logs" from the Debug client's main menu: the current and the
 * two previous Geode log files, stored as chunks for developer diagnosis. */
@Controller("api/ranked/debug/logs")
@UseGuards(SessionGuard)
export class DebugLogsController {
  public constructor(@Inject(DATABASE) private readonly db: DatabasePort,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock) {}

  @Post()
  @HttpCode(200)
  public async chunk(@CurrentSession() session: RankedSessionContext, @Body() body: DebugLogChunkDto) {
    if (session.clientBuildChannel !== "debug") throw new ConflictException("DEBUG_BUILD_REQUIRED");
    if (body.part >= body.totalParts) throw new ConflictException("INVALID_LOG_PART");
    const now = this.clock.now();
    return this.db.transaction(async (tx) => {
      await tx.query("SELECT pg_advisory_xact_lock(hashtext($1))", [`debug-logs:${session.playerId}`]);
      const recent = await tx.query<{ uploads: string; current: boolean }>(
        `SELECT count(DISTINCT upload_id)::text AS uploads, bool_or(upload_id = $3) AS current
         FROM ranked_debug_log_chunks WHERE player_id = $1 AND received_at > $2`,
        [session.playerId, new Date(now.getTime() - 86_400_000).toISOString(), body.uploadId],
      );
      const row = recent.rows[0];
      if (!row?.current && Number(row?.uploads ?? 0) >= UPLOADS_PER_DAY) throw new ConflictException("DEBUG_LOG_LIMIT");
      await tx.query(
        `INSERT INTO ranked_debug_log_chunks (upload_id, player_id, client_version, file_index, file_name, part, total_parts, content, received_at)
         VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
         ON CONFLICT (upload_id, file_index, part) DO NOTHING`,
        [body.uploadId, session.playerId, session.clientVersion ?? "unknown", body.fileIndex, body.fileName,
          body.part, body.totalParts, body.content.replace(/\u0000/g, ""), now.toISOString()],
      );
      return { received: true, uploadId: body.uploadId, fileIndex: body.fileIndex, part: body.part };
    });
  }
}
