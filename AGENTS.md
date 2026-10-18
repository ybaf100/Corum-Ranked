# Corum-Ranked repository instructions

## Deployment policy (authoritative)

- `main` is the authoritative source for the Corum Ranked server and mod.
- Production moved to Railway: project `corum-ranked` (`9a50ad5f-c812-4200-b97a-0296492097cd`), service `corum-ranked` (`f8f77cca-8054-47bb-b7e7-21581f394f0f`), production environment `48a806c0-3aed-41bf-ab3a-f5c2b8015eb4`. The service follows this repository's `main` branch. The public endpoint is `https://corum-ranked-production.up.railway.app`. Do not restore the former Render endpoint or service.
- Use ordinary commit messages for server releases and verify the resulting Railway deployment commit. Preserve the direct Neon migration connection/session-lock behavior. Do not change existing environment variables or database state for this process.
- A `[build only]` marker controls only optional R2 publishing in the Ranked mod workflows and does not replace automatic Railway deployment. Keep build-only and R2 release decisions separate from the server deploy decision.
- After merging a version-gated client/server change, check that Railway reaches `SUCCESS` on the current `main` commit. If auto-deploy is on, do **not** trigger a duplicate manual deploy. Only manually deploy if GitHub's commit was skipped or an automatic deployment failed to start.
- GitHub Actions workflows `ranked-ci.yml`, `build-ranked-mod.yml`, and `build-ranked-debug.yml` already trigger automatically on the appropriate pushes. Verify their actual run status; a workflow run being created does not imply its jobs executed or succeeded. Investigate zero-step/zero-runner-time failures at the GitHub Actions account/runner level.
- Immutable Cloudflare R2 package publication is a separate release stage, gated by `RANKED_PUBLISH_R2` and successful package builds. Do not bypass this protection or expose R2 credentials solely to force publishing.
- Version changes must synchronize `corum-ranked-mod/mod.json`, Ranked workspace package metadata/lockfile, and `ranked/apps/server/src/session/client-version.ts`.
- Do not describe an untested iOS/Android native change as verified. Server deploy success does not mean a compatible packaged client was published.
