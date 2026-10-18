# Railway + Neon advisory lock migration startup fix (beta.83)

## Confirmed live evidence
At 2026-10-18 11:37 UTC the new Railway deployment was stuck waiting on `SELECT pg_advisory_lock(hashtext($1))` (backend PID 4522; PostgreSQL wait_event=advisory). `pg_blocking_pids(4522)` returned backend PID 647. Backend 647 began at 10:46:19 UTC (a previous successful deploy), was **idle**, and retained the session advisory lock. The application uses Neon `-pooler` (PgBouncer transaction pooling), where LOCK and UNLOCK on one client connection need not use the same PostgreSQL backend.

## Fix
Regular gameplay still connects to the existing pooled Neon URL. Startup migrations alone use a separate, max-one direct Neon connection derived from the same URL by removing `-pooler` from the proven `.neon.tech` hostname. It preserves credentials, database and TLS query, never logs secrets. Advisory lock v2 is acquired/released on this direct session, avoiding the orphaned v1 pooler lock and preventing the same pooling bug on subsequent deployments. Other PostgreSQL hosts are not rewritten. On graceful shutdown both pools close.

No SQL tables, game states, sessions or legacy database connections are manually deleted or forcibly terminated. Migration scripts and checks are unchanged.

## Validation
The unit test checks safe URL transformation, separate migration pool ownership, and lock-key separation; GitHub server CI validates build/migrations. Confirm Railway startup /health and /ready after deployment. The old pooled v1 session lock may remain until its upstream backend disconnects, but new migrations no longer depend on that lock key.
