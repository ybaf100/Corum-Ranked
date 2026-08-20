import {
  Inject,
  Injectable,
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
  private readonly pool: Pool;

  public constructor(
    @Inject(SERVER_ENVIRONMENT) environment: ServerEnvironment,
  ) {
    this.pool = new Pool({ connectionString: environment.databaseUrl });
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
      await client.query("ROLLBACK");
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
