/**
 * Neon -pooler hosts provide transaction pooling, not backend-affine sessions.
 * Migration advisory locks must use a direct session connection so LOCK and
 * UNLOCK reach the same PostgreSQL backend. Gameplay retains the pooled URL.
 * Never log this URL: it contains the same credentials as DATABASE_URL.
 */
export function migrationConnectionString(databaseUrl: string): string {
  const url = new URL(databaseUrl);
  if (!url.hostname.endsWith(".neon.tech") || !url.hostname.includes("-pooler.")) {
    return databaseUrl;
  }
  url.hostname = url.hostname.replace("-pooler.", ".");
  return url.toString();
}
