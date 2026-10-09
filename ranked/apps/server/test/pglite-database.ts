import { PGlite } from "@electric-sql/pglite";
import type {
  DatabasePort,
  SqlExecutor,
  SqlResult,
} from "../src/database/database.port.js";

export class PgliteDatabase implements DatabasePort {
  public constructor(public readonly client: PGlite) {}

  public async query<Row>(
    sql: string,
    values: readonly unknown[] = [],
  ): Promise<SqlResult<Row>> {
    const result = await this.client.query<Row>(sql, [...values]);
    return {
      rows: result.rows,
      rowCount: result.affectedRows ?? result.rows.length,
    };
  }

  public async transaction<T>(operation: (client: SqlExecutor) => Promise<T>): Promise<T> {
    // PGlite has one connection. Its transaction API serializes whole callbacks;
    // issuing independent BEGIN/COMMIT calls lets concurrent requests share a
    // transaction and gives misleading row-lock/idempotence test results.
    return this.client.transaction(async transaction => operation({
      query: async <Row>(sql: string, values: readonly unknown[] = []): Promise<SqlResult<Row>> => {
        const result = await transaction.query<Row>(sql, [...values]);
        return { rows: result.rows, rowCount: result.affectedRows ?? result.rows.length };
      },
    }));
  }

  public async ping(): Promise<boolean> {
    await this.client.query("SELECT 1");
    return true;
  }
}
