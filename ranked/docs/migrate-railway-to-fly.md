# Corum-Ranked Railway → Fly.io migration (prepared; NOT cut over)

> Status: **configuration prepared only**. This PR does not deploy anything, switch the client/server URL, stop Railway, or touch the production database. Do **not** merge/deploy without reviewing billing, secret handling, and the cutover window.

## Why this target

- Corum-Ranked is a continuously queried NestJS API, with PostgreSQL data already in Neon.
- Deploy one always-on Fly Machine in Tokyo (`nrt`); `auto_stop_machines = "off"`.
- Proposed 512 MiB / 2 shared CPU preset is **paid**, metered, with network charges; check the current Fly regional price before provisioning.
- Do not use a new Fly-managed Postgres: reuse Neon.

## Live Railway source (observed 2026-10-21)

- Project/service: `corum-ranked`, branch `main`, root directory `ranked`.
- Build: `npm ci --include=dev && npm run build`.
- Start: `npm run start --workspace @corum-ranked/server`.
- Health: `/health`; readiness: `/ready`.
- Railway URL used by existing Geode clients: `https://corum-ranked-production.up.railway.app`.
- Railway's `sleepApplication=true` currently makes uptime dependent on sleep behavior.
- Railway OAuth hides environment **values** (only names were available). Secrets must be supplied from the owner's own secure source; **never** copy values into Git or PR comments.
- Railway is not retired by this PR.

## 1. Create and prepare the Fly app

1. Create/log into a Fly.io account, review payment settings and caps/alerts before proceeding.
2. Use `fly apps create <unique-name>` (choose the desired organization). Replace the example `app` name in `ranked/fly.toml` with the actual name.
3. Review `primary_region = "nrt"`, `[[vm]]` sizing, and always-on behavior in `ranked/fly.toml`.
4. From the repository, deploy the `ranked/` directory as build context:
   ```sh
   cd ranked
   fly deploy --config fly.toml --remote-only
   ```
5. Docker build uses Node 22, npm workspaces, and the existing NestJS start command; runtime listens on `PORT=8080`.

**Deploying without necessary secrets is expected to fail startup. Set secrets first.**

## 2. Secrets and settings

Required in Fly Secrets (not in `fly.toml` or Git):
- `DATABASE_URL` (prefer an isolated **Neon staging branch** for first deployment/tests)
- `RANKED_CONFIG_URL`
- `RANKED_SESSION_TOKEN_SECRET` (production minimum 32 characters)
- `RANKED_CONFIG_REFRESH_MS`
- `RANKED_CONFIG_FETCH_TIMEOUT_MS`

Copy and review each relevant Railway application variable by **name** and retrieve its value privately from its original source:
- `CORS_ORIGINS`
- `RANKED_CSMP_FETCH_TIMEOUT_MS`
- `RANKED_DOWNLOAD_BASE_URL`
- `RANKED_DOWNLOAD_CDN_BASE_URL` and `RANKED_MOD_UPDATES_JSON`, if used
- `SURVIVAL_GD_RELAY_ORIGIN`, `SURVIVAL_GD_RELAY_TOKEN`
- `ENABLE_DEBUG_BOT_MATCH`, `DEBUG_BOT_PASSWORD`
- `ENABLE_DEBUG_SPECTATE`, `DEBUG_SPECTATE_PASSWORD`
- `DISCORD_WEBHOOK_URL` and `DISCORD_OUTBOX_*` settings when Discord relay is enabled

Do not copy any `RAILWAY_*` injected variable. Never print DB URLs, signing secrets, or webhook URLs in CI logs.

## 3. Staging smoke test BEFORE attaching production Neon

1. Use a Neon **staging branch**/separate test DB, not the live DB, to avoid two server processes finalizing the same matches/outbox concurrently.
2. Deploy and verify:
   ```sh
   fly status
   curl -f https://<your-app>.fly.dev/health
   curl -f https://<your-app>.fly.dev/ready
   ```
3. Confirm migrations run, config loads, and a test account can use session, matchmaking, rounds, scoring and Survival.
4. Ensure the Fly instance does not go to sleep when idle; inspect actual logs, memory, CPU, requests, and cold-start behavior.
5. Treat successful `/health` alone as **insufficient** for cutover approval.

## 4. Production cutover plan

**Do not run two independent production server processes against the same live Neon DB during normal matchmaking.** Multiple polling/background workers can race for games, session ownership, settlements and Discord outbox even if most writes are idempotent.

1. Back up/verify Neon production state and confirm a rollback plan.
2. Announce a maintenance window; stop admitting new matches and allow in-flight matches to complete or settle safely.
3. Disable/suspend the Railway runtime for the cutover interval **only after** the staging checks pass. Do not delete its project/configuration yet.
4. Change Fly's `DATABASE_URL` to the **same live Neon DB**, set required production secrets, and boot one Fly instance.
5. Verify `/health`, `/ready`, config, migrations, session auth, and the two game modes.
6. Update the Geode client backend base URL from the Railway domain to the actual Fly `*.fly.dev` domain (or, preferably, a stable owned custom domain). Rebuild with the current exact client/server version gate, update system, and platform variants in sync.
7. Publish the compatible Geode mod/update manifest through the existing R2 release pipeline and test real Windows/Android/iOS clients. Old binaries hardcoding Railway do **not** automatically follow the move.
8. Confirm migration on two real test clients; watch errors and pending matches.
9. Retain Railway config for rollback until validation is complete, then retire it deliberately. Never delete Neon or R2 storage.

**Best long-term option:** use an owned custom API domain and introduce an explicit, secure endpoint migration mechanism so the next host change does not force all users to replace hardcoded endpoint binaries.

## 5. Optional manual GitHub deployment

After merging the workflow to `main`, create a repository secret named `FLY_API_TOKEN` with a scoped token issued by Fly.io. The workflow is **manual-only** and uses the checked-out main commit. Do not trigger before Fly app, secrets and cutover policy are ready.

## Acceptance/rollback

- API health + readiness, Solo Ranked and Survival end-to-end, DB migration, AP/title/achievement history, reconnection, and R2 updates must pass.
- One production writer/server instance is authoritative throughout cutover.
- If Fly deployment fails before traffic migration, leave the Railway production source alone. If migration fails after cutover, stop Fly production first, restore Railway, and revert the client endpoint/traffic plan; never run conflicting writers.
