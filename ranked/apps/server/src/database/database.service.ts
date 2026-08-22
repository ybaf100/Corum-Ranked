import {
  Inject,
  Injectable,
  Logger,
  type OnApplicationShutdown,
  type OnModuleInit,
} from "@nestjs/common";
import { readFile } from "node:fs/promises";
import { Pool, type PoolClient, type QueryResultRow } from "pg";
import {
  SERVER_ENVIRONMENT,
  type ServerEnvironment,
} from "../config/server-environment.js";
import type { DatabasePort, SqlExecutor, SqlResult } from "./database.port.js";

const MIGRATION_LOCK_KEY = "corum-ranked-schema-migrations-v1";

const REQUIRED_MIGRATIONS = [
  "0001_initial_ranked.sql",
  "0002_debug_bot_match.sql",
  "0002_attempt_start_leases.sql",
  "0003_debug_rating_playable_maps.sql",
  "0004_alpha37_schema_hardening.sql",
] as const;

export interface RankedSchemaStatus {
  readonly ready: boolean;
  readonly missing: readonly string[];
}

type SchemaProbeRow = QueryResultRow & {
  ranked_schema_migrations: boolean;
  ranked_matches: boolean;
  ranked_rounds: boolean;
  ranked_attempts: boolean;
  ranked_deathmatch_attempts: boolean;
  ranked_attempt_start_leases: boolean;
  match_type: boolean;
  debug_config: boolean;
  round_alternate_level_id: boolean;
  round_playable_level_id: boolean;
  attempt_played_level_id: boolean;
  deathmatch_attempt_played_level_id: boolean;
};

@Injectable()
export class DatabaseService implements DatabasePort, OnModuleInit, OnApplicationShutdown {
  private readonly logger = new Logger(DatabaseService.name);
  private readonly pool: Pool;

  public constructor(
    @Inject(SERVER_ENVIRONMENT) environment: ServerEnvironment,
  ) {
    this.pool = new Pool({
      connectionString: environment.databaseUrl,
      max: 5,
      idleTimeoutMillis: 30_000,
      connectionTimeoutMillis: 10_000,
      keepAlive: true,
    });
    // pg-pool emits idle-client failures on the Pool itself. Without an error
    // listener Node treats that event as uncaught and kills the entire Render
    // process when Neon rotates or drops a pooled TLS connection.
    this.pool.on("error", (error) => {
      this.logger.error(`Unexpected idle PostgreSQL connection error: ${error.message}`);
    });
  }

  public async onModuleInit(): Promise<void> {
    // Database schema is part of server compatibility. Never let Render report a
    // deploy as healthy while the running code references a migration that Neon
    // has not received yet.
    await this.applyRequiredMigrations();
    const schema = await this.schemaStatus();
    if (!schema.ready) {
      throw new Error(`Corum Ranked database schema is incomplete: ${schema.missing.join(", ")}`);
    }
  }

  public query<Row>(
    sql: string,
    values: readonly unknown[] = [],
  ): Promise<SqlResult<Row>> {
    return this.pool.query<QueryResultRow>(sql, [...values]).then((result) => ({
      rows: result.rows as Row[],
      rowCount: result.rowCount ?? 0,
    }));
  }

  public async transaction<T>(operation: (client: SqlExecutor) => Promise<T>): Promise<T> {
    const client = await this.pool.connect();
    try {
      await client.query("BEGIN");
      const result = await operation(new PgClientAdapter(client));
      await client.query("COMMIT");
      return result;
    } catch (error) {
      try {
        await client.query("ROLLBACK");
      } catch (rollbackError) {
        const message = rollbackError instanceof Error ? rollbackError.message : String(rollbackError);
        this.logger.warn(`PostgreSQL rollback failed after connection error: ${message}`);
      }
      throw error;
    } finally {
      client.release();
    }
  }

  public async ping(): Promise<boolean> {
    try {
      await this.pool.query("SELECT 1");
      return true;
    } catch {
      return false;
    }
  }

  public async schemaStatus(): Promise<RankedSchemaStatus> {
    try {
      const result = await this.pool.query<SchemaProbeRow>(`
        SELECT
          to_regclass('public.ranked_schema_migrations') IS NOT NULL AS ranked_schema_migrations,
          to_regclass('public.ranked_matches') IS NOT NULL AS ranked_matches,
          to_regclass('public.ranked_rounds') IS NOT NULL AS ranked_rounds,
          to_regclass('public.ranked_attempts') IS NOT NULL AS ranked_attempts,
          to_regclass('public.ranked_deathmatch_attempts') IS NOT NULL AS ranked_deathmatch_attempts,
          to_regclass('public.ranked_attempt_start_leases') IS NOT NULL AS ranked_attempt_start_leases,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_matches' AND column_name = 'match_type'
          ) AS match_type,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_matches' AND column_name = 'debug_config'
          ) AS debug_config,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_rounds' AND column_name = 'alternate_level_id'
          ) AS round_alternate_level_id,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_rounds' AND column_name = 'playable_level_id'
          ) AS round_playable_level_id,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_attempts' AND column_name = 'played_level_id'
          ) AS attempt_played_level_id,
          EXISTS (
            SELECT 1 FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = 'ranked_deathmatch_attempts' AND column_name = 'played_level_id'
          ) AS deathmatch_attempt_played_level_id
      `);
      const row = result.rows[0];
      if (!row) return { ready: false, missing: ["schema probe returned no row"] };

      const required: ReadonlyArray<readonly [keyof SchemaProbeRow, string]> = [
        ["ranked_schema_migrations", "table ranked_schema_migrations"],
        ["ranked_matches", "table ranked_matches"],
        ["ranked_rounds", "table ranked_rounds"],
        ["ranked_attempts", "table ranked_attempts"],
        ["ranked_deathmatch_attempts", "table ranked_deathmatch_attempts"],
        ["ranked_attempt_start_leases", "table ranked_attempt_start_leases"],
        ["match_type", "ranked_matches.match_type"],
        ["debug_config", "ranked_matches.debug_config"],
        ["round_alternate_level_id", "ranked_rounds.alternate_level_id"],
        ["round_playable_level_id", "ranked_rounds.playable_level_id"],
        ["attempt_played_level_id", "ranked_attempts.played_level_id"],
        ["deathmatch_attempt_played_level_id", "ranked_deathmatch_attempts.played_level_id"],
      ];
      const missing = required.filter(([key]) => !row[key]).map(([, label]) => label);

      if (row.ranked_schema_migrations) {
        const applied = await this.pool.query<{ filename: string }>(
          "SELECT filename FROM ranked_schema_migrations WHERE filename = ANY($1::text[])",
          [[...REQUIRED_MIGRATIONS]],
        );
        const appliedNames = new Set(applied.rows.map((item) => item.filename));
        for (const filename of REQUIRED_MIGRATIONS) {
          if (!appliedNames.has(filename)) missing.push(`migration ${filename}`);
        }
      }

      return { ready: missing.length === 0, missing };
    } catch (error) {
      return {
        ready: false,
        missing: [error instanceof Error ? error.message : "schema probe failed"],
      };
    }
  }

  private async applyRequiredMigrations(): Promise<void> {
    const client = await this.pool.connect();
    let lockHeld = false;
    try {
      // Render can briefly overlap old/new instances. A PostgreSQL advisory lock
      // guarantees only one process mutates the Neon schema at a time.
      await client.query("SELECT pg_advisory_lock(hashtext($1))", [MIGRATION_LOCK_KEY]);
      lockHeld = true;

      await client.query(`
        CREATE TABLE IF NOT EXISTS ranked_schema_migrations (
          filename TEXT PRIMARY KEY,
          applied_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
        )
      `);

      const baseline = await client.query<{ exists: boolean }>(
        "SELECT to_regclass('public.ranked_matches') IS NOT NULL AS exists",
      );
      if (baseline.rows[0]?.exists) {
        // Legacy alpha databases predate the ledger. Presence of ranked_matches
        // proves 0001 was applied; record it without re-running the non-trivial
        // initial schema script.
        await this.recordMigration(client, "0001_initial_ranked.sql");
      }

      for (const filename of REQUIRED_MIGRATIONS) {
        if (await this.migrationApplied(client, filename)) continue;
        this.logger.log(`Applying Ranked migration ${filename}`);
        await this.executeMigration(client, filename);
        await this.recordMigration(client, filename);
      }
    } catch (error) {
      // Migration files own their BEGIN/COMMIT boundary. ROLLBACK is harmless
      // when no transaction is active and ensures a failed script cannot leave a
      // transaction pinned to the advisory-lock connection.
      try {
        await client.query("ROLLBACK");
      } catch {
        // Ignore; the original migration error is the one that matters.
      }
      const message = error instanceof Error ? error.message : String(error);
      this.logger.error(`Ranked database migration failed: ${message}`);
      throw error;
    } finally {
      if (lockHeld) {
        try {
          await client.query("SELECT pg_advisory_unlock(hashtext($1))", [MIGRATION_LOCK_KEY]);
        } catch (error) {
          const message = error instanceof Error ? error.message : String(error);
          this.logger.warn(`Could not release Ranked migration lock: ${message}`);
        }
      }
      client.release();
    }
  }

  private async migrationApplied(client: PoolClient, filename: string): Promise<boolean> {
    const result = await client.query<{ exists: boolean }>(
      "SELECT EXISTS (SELECT 1 FROM ranked_schema_migrations WHERE filename = $1) AS exists",
      [filename],
    );
    return result.rows[0]?.exists === true;
  }

  private async recordMigration(client: PoolClient, filename: string): Promise<void> {
    await client.query(
      "INSERT INTO ranked_schema_migrations (filename) VALUES ($1) ON CONFLICT (filename) DO NOTHING",
      [filename],
    );
  }

  private async executeMigration(client: PoolClient, filename: string): Promise<void> {
    // Both src/database and dist/database are four directories below ranked/.
    // Render retains ranked/migrations next to the compiled apps/server/dist.
    const migrationUrl = new URL(`../../../../migrations/${filename}`, import.meta.url);
    const sql = await readFile(migrationUrl, "utf8");
    await client.query(sql);
  }

  public async onApplicationShutdown(): Promise<void> {
    await this.pool.end();
  }
}

class PgClientAdapter implements SqlExecutor {
  public constructor(private readonly client: PoolClient) {}

  public query<Row>(sql: string, values: readonly unknown[] = []): Promise<SqlResult<Row>> {
    return this.client.query<QueryResultRow>(sql, [...values]).then((result) => ({
      rows: result.rows as Row[],
      rowCount: result.rowCount ?? 0,
    }));
  }
}
