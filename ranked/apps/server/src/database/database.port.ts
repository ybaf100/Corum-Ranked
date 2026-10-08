export const DATABASE = Symbol("DATABASE");

export interface SqlResult<Row> {
  readonly rows: readonly Row[];
  readonly rowCount: number;
}

export interface SqlExecutor {
  query<Row>(sql: string, values?: readonly unknown[]): Promise<SqlResult<Row>>;
}

export interface DatabasePort extends SqlExecutor {
  transaction<T>(operation: (client: SqlExecutor) => Promise<T>): Promise<T>;
  ping(): Promise<boolean>;
}
