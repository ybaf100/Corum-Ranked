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
    await this.client.exec("BEGIN");
    try {
      const result = await operation(this);
      await this.client.exec("COMMIT");
      return result;
    } catch (error) {
      await this.client.exec("ROLLBACK");
      throw error;
    }
  }

  public async ping(): Promise<boolean> {
    await this.client.query("SELECT 1");
    return true;
  }
}
