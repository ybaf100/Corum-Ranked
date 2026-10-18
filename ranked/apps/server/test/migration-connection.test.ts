import { describe, it, expect } from "vitest";
import { readFile } from "node:fs/promises";
import { migrationConnectionString } from "../src/database/migration-connection.js";

describe("Neon direct migration advisory lock connection", () => {
  it("keeps database, role, credentials and TLS options when removing the Neon pooler suffix", () => {
    const pooled = "postgresql://role:p%40ss@ep-unit-pooler.c-3.ap-southeast-1.aws.neon.tech/neondb?sslmode=require&channel_binding=require";
    const direct = new URL(migrationConnectionString(pooled));
    const source = new URL(pooled);
    expect(direct.hostname).toBe("ep-unit.c-3.ap-southeast-1.aws.neon.tech");
    expect(direct.username).toBe(source.username);
    expect(direct.password).toBe(source.password);
    expect(direct.pathname).toBe(source.pathname);
    expect(direct.search).toBe(source.search);
  });
  it("does not rewrite a direct Neon host or another PostgreSQL provider", () => {
    for (const uri of [
      "postgresql://role:secret@ep-unit.c-3.ap-southeast-1.aws.neon.tech/db?sslmode=require",
      "postgresql://role:secret@some-pooler.internal:5432/db",
      "postgresql://role:secret@not-neon-pooler.example/db",
    ]) expect(migrationConnectionString(uri)).toBe(uri);
  });
  it("isolates migration advisory locking from the gameplay pool and retires both pools", async () => {
    const source = await readFile(new URL("../src/database/database.service.ts", import.meta.url), "utf8");
    expect(source).toContain('const MIGRATION_LOCK_KEY = "corum-ranked-schema-migrations-v2"');
    expect(source).toContain("const client = await this.migrationPool.connect()");
    expect(source).toContain("migrationConnectionString(environment.databaseUrl)");
    expect(source).toContain("await Promise.all([this.pool.end(), this.migrationPool.end()])");
  });
});
