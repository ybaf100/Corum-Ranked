# Corum-Ranked repository instructions

## Deployment policy (authoritative)

- `main` is the authoritative source for the Corum Ranked server and mod.
- Standing user authorization: unless a later request explicitly limits a stage, finish every patch with a normal main push, Release and Debug R2 publication, and Singapore production server activation in the same task. Preserve concurrent user changes. Verify the new immutable client packages before activating their required version; carry out the existing gated workflows without asking for deployment permission again, and report any external execution blocker accurately.
- For Singapore deploys use verified GitHub environment lightsail-production variable `LIGHTSAIL_HOST=52.74.37.130` and verified `LIGHTSAIL_SSH_KNOWN_HOSTS`; keep `StrictHostKeyChecking` enabled.
- Deploy to Lightsail using manual `deploy` or an explicit main commit marker `[deploy-lightsail]`; ordinary pushes deploy only if `LIGHTSAIL_AUTO_DEPLOY=true`. A `[no-deploy]` push is always skipped, including when auto-deploy is enabled; manual `deploy` is still available. Keep a single authoritative Neon writer, and verify live `/health` and `/ready`.
- A `[build only]` marker controls only optional R2 publishing in the Ranked mod workflows and does not control Lightsail deployment. Keep build-only and R2 release decisions separate from the server deploy decision.
- After changing the client version, verify the Lightsail deploy, both debug/release R2 package publications, client mod updates and the live Singapore API. Never treat a green build as proof of completed publication or physical-device behavior.
- GitHub Actions workflows `ranked-ci.yml`, `build-ranked-mod.yml`, and `build-ranked-debug.yml` already trigger automatically on the appropriate pushes. Verify their actual run status; a workflow run being created does not imply its jobs executed or succeeded. Investigate zero-step/zero-runner-time failures at the GitHub Actions account/runner level.
- Immutable Cloudflare R2 package publication is a separate release stage, gated by `RANKED_PUBLISH_R2` and successful package builds. Do not bypass this protection or expose R2 credentials solely to force publishing.
- Version changes must synchronize `corum-ranked-mod/mod.json`, Ranked workspace package metadata/lockfile, and `ranked/apps/server/src/session/client-version.ts`.
- Do not describe an untested iOS/Android native change as verified. Server deploy success does not mean a compatible packaged client was published.
- Sole supported hosting target: AWS Lightsail Singapore `https://api.52-74-37-130.sslip.io`, with Neon Singapore as its single authoritative writer. Railway and Render support, deployment targets, update bridges and client fallback URLs are retired by the user's 2026-10-25 instruction. Preserve historical rollout records as history; do not restore those platforms or deploy retired Seoul.

- The Lightsail push workflow can build its Docker image concurrently with the first Release/Debug builds. Its CI-only publication barrier verifies both immutable channels (full bytes/hash, embedded identity/version/channel, all four platforms) before upload/activation. A missing channel waits within the job timeout; corrupt or incompatible packages fail closed. No second activation push is required for a `[deploy-lightsail] [publish-r2]` source push.
