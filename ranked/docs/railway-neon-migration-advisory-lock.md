# Railway–Neon migration advisory lock fix

In the failed v0.5.0-beta.83 Railway deployment on 2026-10-18, PostgreSQL showed the new application waiting on `pg_advisory_lock`, blocked by an idle backend session from a previous deployment. The application had been acquiring a session-scoped lock via Neon's PgBouncer transaction-pooling hostname. The unlock may land on a different backend and cannot reliably release the original lock.

Bootstrap schema migrations now use a separate direct-session Neon connection, created by removing the provider's `-pooler` hostname segment and retaining the same database, credentials and TLS settings. Ordinary gameplay still uses the existing pooler. A new migration lock key avoids inheriting the stranded v1 advisory lock. No tables or existing DB sessions are forcibly removed.

Verify Railway /health and /ready as well as the boot-time schema status. All existing migration scripts remain unchanged.
