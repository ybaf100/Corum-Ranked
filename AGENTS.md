# Corum-Ranked repository instructions

## Deployment policy (authoritative)

- `main` is the authoritative source for the Corum Ranked server and mod.
- Production game server: AWS Lightsail Singapore `https://api.52-74-37-130.sslip.io` (Neon production also Singapore). Retired Seoul must not receive deployments. The original Railway service (`9a50ad5f-c812-4200-b97a-0296492097cd` / `f8f77cca-8054-47bb-b7e7-21581f394f0f`) is **update-only**: its bootstrap serves `/api/ranked/config` proxied read-only from Singapore Lightsail; all gameplay writes return 426, and Railway never initializes the Neon-backed Nest app.
- For Singapore deploys use verified GitHub environment lightsail-production variable `LIGHTSAIL_HOST=52.74.37.130` and verified `LIGHTSAIL_SSH_KNOWN_HOSTS`; keep `StrictHostKeyChecking` enabled.
- Deploy to Lightsail using manual `deploy` or an explicit main commit marker `[deploy-lightsail]`; ordinary pushes deploy only if `LIGHTSAIL_AUTO_DEPLOY=true`. A `[no-deploy]` push is always skipped, including when auto-deploy is enabled; manual `deploy` is still available. Keep a single authoritative Neon writer, and verify live `/health` and `/ready`.
- A `[build only]` marker controls only optional R2 publishing in the Ranked mod workflows and does not replace automatic Railway deployment. Keep build-only and R2 release decisions separate from the server deploy decision.
- After changing the client version, verify the Lightsail deploy, both debug/release R2 package publications, client mod updates, and the Railway upgrade bridge. Never treat a green build as proof of completed publication or physical-device behavior.
- GitHub Actions workflows `ranked-ci.yml`, `build-ranked-mod.yml`, and `build-ranked-debug.yml` already trigger automatically on the appropriate pushes. Verify their actual run status; a workflow run being created does not imply its jobs executed or succeeded. Investigate zero-step/zero-runner-time failures at the GitHub Actions account/runner level.
- Immutable Cloudflare R2 package publication is a separate release stage, gated by `RANKED_PUBLISH_R2` and successful package builds. Do not bypass this protection or expose R2 credentials solely to force publishing.
- Version changes must synchronize `corum-ranked-mod/mod.json`, Ranked workspace package metadata/lockfile, and `ranked/apps/server/src/session/client-version.ts`.
- Do not describe an untested iOS/Android native change as verified. Server deploy success does not mean a compatible packaged client was published.
