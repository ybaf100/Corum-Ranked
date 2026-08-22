import {
  Inject,
  Injectable,
  Logger,
  type OnApplicationShutdown,
} from "@nestjs/common";
import { Pool, type PoolClient, type QueryResultRow } from "pg";
import {
  SERVER_ENVIRONMENT,
  type ServerEnvironment,
} from "../config/server-environment.js";
import type { DatabasePort, SqlExecutor, SqlResult } from "./database.port.js";

@Injectable()
export class DatabaseService implements DatabasePort, OnApplicationShutdown {
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
