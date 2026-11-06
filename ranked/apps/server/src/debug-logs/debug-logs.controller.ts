import { Body, ConflictException, Controller, HttpCode, Inject, Logger, Post, UseGuards } from "@nestjs/common";
import { CurrentSession } from "../session/current-session.decorator.js";
import { SessionGuard } from "../session/session.guard.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { DATABASE, type DatabasePort } from "../database/database.port.js";
import { SERVER_CLOCK, type ServerClock } from "../common/runtime.module.js";
import { SERVER_ENVIRONMENT, type ServerEnvironment } from "../config/server-environment.js";
import { DebugLogChunkDto } from "./debug-logs.dto.js";

const UPLOADS_PER_DAY = 12;

/** "Send recent logs" from the Debug client's main menu: the current and the
 * two previous Geode log files arrive in chunks, are rebuilt into .log files
 * and posted to the debug-log Discord webhook. Delivered chunks are removed. */
@Controller("api/ranked/debug/logs")
@UseGuards(SessionGuard)
export class DebugLogsController {
  private readonly logger = new Logger(DebugLogsController.name);
  public constructor(@Inject(DATABASE) private readonly db: DatabasePort,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
    @Inject(SERVER_ENVIRONMENT) private readonly environment: ServerEnvironment) {}

  @Post()
  @HttpCode(200)
  public async chunk(@CurrentSession() session: RankedSessionContext, @Body() body: DebugLogChunkDto) {
    if (session.clientBuildChannel !== "debug") throw new ConflictException("DEBUG_BUILD_REQUIRED");
    if (body.part >= body.totalParts) throw new ConflictException("INVALID_LOG_PART");
    const totalFiles = body.totalFiles ?? 1;
    if (body.fileIndex >= totalFiles) throw new ConflictException("INVALID_LOG_FILE");
    const now = this.clock.now();
    const complete = await this.db.transaction(async (tx) => {
      await tx.query("SELECT pg_advisory_xact_lock(hashtext($1))", [`debug-logs:${session.playerId}`]);
      const recent = await tx.query<{ uploads: string; current: boolean }>(
        `SELECT count(DISTINCT upload_id)::text AS uploads, bool_or(upload_id = $3) AS current
         FROM ranked_debug_log_chunks WHERE player_id = $1 AND received_at > $2`,
        [session.playerId, new Date(now.getTime() - 86_400_000).toISOString(), body.uploadId],
      );
      const row = recent.rows[0];
      if (!row?.current && Number(row?.uploads ?? 0) >= UPLOADS_PER_DAY) throw new ConflictException("DEBUG_LOG_LIMIT");
      await tx.query(
        `INSERT INTO ranked_debug_log_chunks (upload_id, player_id, client_version, file_index, file_name, part, total_parts, total_files, content, received_at)
         VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)
         ON CONFLICT (upload_id, file_index, part) DO NOTHING`,
        [body.uploadId, session.playerId, session.clientVersion ?? "unknown", body.fileIndex, body.fileName,
          body.part, body.totalParts, totalFiles, body.content.replace(/\u0000/g, ""), now.toISOString()],
      );
      const progress = await tx.query<{ file_index: number; parts: string; total_parts: number }>(
        `SELECT file_index, count(*)::text AS parts, max(total_parts) AS total_parts FROM ranked_debug_log_chunks
         WHERE upload_id = $1 AND player_id = $2 GROUP BY file_index`,
        [body.uploadId, session.playerId],
      );
      return progress.rows.length === totalFiles && progress.rows.every((file) => Number(file.parts) === Number(file.total_parts));
    });
    if (!complete) return { received: true, delivered: false, complete: false };
    const delivered = await this.deliver(body.uploadId, session).catch((error: unknown) => {
      this.logger.warn(`debug log delivery failed upload=${body.uploadId}: ${String(error)}`);
      return false;
    });
    return { received: true, complete: true, delivered };
  }

  private async deliver(uploadId: string, session: RankedSessionContext): Promise<boolean> {
    const webhook = this.environment.debugLogWebhookUrl;
    if (!webhook) { this.logger.warn(`debug log upload=${uploadId} stored; no Discord webhook configured`); return false; }
    const chunks = (await this.db.query<{ file_index: number; file_name: string; content: string; client_version: string }>(
      `SELECT file_index, file_name, content, client_version FROM ranked_debug_log_chunks
       WHERE upload_id = $1 AND player_id = $2 ORDER BY file_index, part`, [uploadId, session.playerId])).rows;
    const player = (await this.db.query<{ gd_username: string }>("SELECT gd_username FROM ranked_players WHERE id = $1", [session.playerId])).rows[0];
    const files = new Map<number, { name: string; text: string }>();
    for (const chunk of chunks) {
      const file = files.get(chunk.file_index) ?? { name: chunk.file_name, text: "" };
      file.text += chunk.content; files.set(chunk.file_index, file);
    }
    const form = new FormData();
    const labels = ["current", "previous", "older"];
    form.append("payload_json", JSON.stringify({
      content: `Debug logs · ${player?.gd_username ?? "unknown"} · ${chunks[0]?.client_version ?? "unknown"} · ${this.clock.now().toISOString()}`,
      allowed_mentions: { parse: [] },
    }));
    [...files.entries()].sort(([a], [b]) => a - b).forEach(([index, file], position) => {
      const name = `${labels[index] ?? `log${index}`}-${file.name.replace(/[^A-Za-z0-9._-]/g, "_")}`.replace(/(\.log)?$/, ".log");
      form.append(`files[${position}]`, new Blob([file.text], { type: "text/plain" }), name);
    });
    const response = await fetch(webhook, { method: "POST", body: form, signal: AbortSignal.timeout(30_000) });
    if (!response.ok) throw new Error(`Discord webhook returned HTTP ${response.status}`);
    // The chunks were only a transport buffer for this delivery.
    await this.db.query("DELETE FROM ranked_debug_log_chunks WHERE upload_id = $1 AND player_id = $2", [uploadId, session.playerId]);
    this.logger.log(`debug log upload=${uploadId} delivered files=${files.size}`);
    return true;
  }
}
