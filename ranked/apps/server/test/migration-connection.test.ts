import { describe, it, expect } from "vitest";
import { migrationConnectionString } from "../src/database/migration-connection.js";

describe("advisory-lock migration connection", () => {
  it("uses Neon direct session host while preserving credentials, DB, SSL and binding", () => {
    const pooled = "postgresql://role:p%40ss@ep-unit-pooler.c-3.ap-southeast-1.aws.neon.tech/neondb?sslmode=require&channel_binding=require";
    const direct = new URL(migrationConnectionString(pooled));
    const source = new URL(pooled);
    expect(direct.hostname).toBe("ep-unit.c-3.ap-southeast-1.aws.neon.tech");
    expect(direct.username).toBe(source.username);
    expect(direct.password).toBe(source.password);
    expect(direct.pathname).toBe("/neondb");
    expect(direct.searchParams.get("sslmode")).toBe("require");
    expect(direct.searchParams.get("channel_binding")).toBe("require");
  });
  it("does not rewrite direct Neon or non-Neon PostgreSQL URLs", () => {
    for (const uri of [
      "postgresql://role:secret@ep-unit.c-3.ap-southeast-1.aws.neon.tech/db?sslmode=require",
      "postgresql://role:secret@my-pooler.internal:5432/db",
      "postgresql://role:secret@not-neon-pooler.example/db",
    ]) expect(migrationConnectionString(uri)).toBe(uri);
  });
  it("separates advisory-locked bootstrap connections from gameplay pool connections", async () => {
    const source = (await import("node:fs/promises")).readFile;
    const text = await source(new URL("../src/database/database.service.ts", import.meta.url), "utf8");
    expect(text).toContain('const MIGRATION_LOCK_KEY = "corum-ranked-schema-migrations-v2"');
    expect(text).toContain("const client = await this.migrationPool.connect()");
    expect(text).toContain("migrationConnectionString(environment.databaseUrl)");
    expect(text).toContain("await Promise.all([this.pool.end(), this.migrationPool.end()])");
  });
});
