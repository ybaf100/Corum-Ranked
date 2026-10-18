# Corum-Ranked repository instructions

## Deployment policy (authoritative)

- `main` is the authoritative source for the Corum Ranked server and mod.
- The Render service `Corum-Ranked` (`srv-da3j3s2fngtc73964pmg`) is connected to this repository's `main` branch with **commit-triggered auto-deploy** and root directory `ranked`.
- **Do not add `[skip render]` to merge commits or any commits intended to deploy server changes.** This marker caused Render to ignore beta.77 and beta.78 updates. A normal `main` commit changing `ranked/**` should trigger an automatic Render deployment. Do not change existing environment variables or database state for this process.
- A `[build only]` marker controls only optional R2 publishing in the Ranked mod workflows and does not replace automatic Render deployment. Keep build-only and R2 release decisions separate from the server deploy decision.
- After merging a version-gated client/server change, check that Render reaches `live` on the current `main` commit. If auto-deploy is on, do **not** trigger a duplicate manual deploy. Only manually deploy if GitHub's commit was skipped or an automatic deployment failed to start.
- GitHub Actions workflows `ranked-ci.yml`, `build-ranked-mod.yml`, and `build-ranked-debug.yml` already trigger automatically on the appropriate pushes. Verify their actual run status; a workflow run being created does not imply its jobs executed or succeeded. Investigate zero-step/zero-runner-time failures at the GitHub Actions account/runner level.
- Immutable Cloudflare R2 package publication is a separate release stage, gated by `RANKED_PUBLISH_R2` and successful package builds. Do not bypass this protection or expose R2 credentials solely to force publishing.
- Version changes must synchronize `corum-ranked-mod/mod.json`, Ranked workspace package metadata/lockfile, and `ranked/apps/server/src/session/client-version.ts`.
- Do not describe an untested iOS/Android native change as verified. Server deploy success does not mean a compatible packaged client was published.
