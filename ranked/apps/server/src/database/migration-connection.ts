/**
 * Advisory locks are session-scoped. Neon -pooler hosts use transaction
 * pooling and may run UNLOCK on a different PostgreSQL backend.
 * Only bootstrap migration calls need a direct session, not gameplay queries.
 * Retain the same database/role/TLS parameters; never log the resulting URL.
 */
export const migrationConnectionString = (databaseUrl: string): string => {
  const url = new URL(databaseUrl);
  // This is an intentionally narrow Neon-specific transformation; do not
  // rewrite a third-party database hostname or an already-direct connection.
  if (!url.hostname.endsWith(".neon.tech") || !url.hostname.includes("-pooler.")) {
    return databaseUrl;
  }
  url.hostname = url.hostname.replace("-pooler.", ".");
  return url.toString();
};
