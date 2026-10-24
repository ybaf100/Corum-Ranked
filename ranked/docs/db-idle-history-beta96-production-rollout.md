# DB idle/history beta.96 production rollout — 2026-10-24

The owner explicitly authorized merge, publication and deployment after reviewing PR #32. The validated implementation was merged at `8f18653e3d7d38a48c0b9c27601fb1b6f1497fc9` with automatic deployment and publication suppressed.

The release synchronizes mod, workspace/lockfile and admission metadata to `v0.5.0-beta.96` (previous `v0.5.0-beta.95`). Both new immutable Release/Debug packages must be publicly verified before a separate `[deploy-lightsail] [build only]` activation. Existing beta.95 objects must not be overwritten.

This release contains event-driven idle DB workers, authoritative round history, native LevelInfoLayer navigation, achievement/title presentation and Survival native return/re-entry fixes described in [the implementation report](db-idle-history-redesign.md). It adds no migrations and must not rerun the beta.95 achievement reset or change its activation epoch. Production Neon settings are unchanged.

Before activation, verify all native package builds, R2 publication and full package byte count/SHA256/manifest/platform contents. Activate only on Singapore using pinned SSH and the existing rollback workflow, then verify live health/ready/config and the Railway update-only bridge. The latter remains a proxy, never a second database writer.

The isolated virtual-time idle SQL measurement (5,784 → 0 in 360 seconds) does not establish production Neon suspension. Actual sleep/resume and physical-device UI/touch/native gameplay remain separate verification items. `/ready` and public config requests can themselves query PostgreSQL; avoid periodic readiness polling to measure idle SQL.

## Verified package publication and activation

- Package source: `4a322d13c76d6b39aef1b37f725fde750b3cb49d`.
- [Server CI](https://github.com/ybaf100/Corum-Ranked/actions/runs/40090065732), [Release builds/package/R2 publication](https://github.com/ybaf100/Corum-Ranked/actions/runs/40090065774), and [Debug ON/OFF builds/package/R2 publication](https://github.com/ybaf100/Corum-Ranked/actions/runs/40090065837) all succeeded.
- Full public Release download: 8,726,435 bytes, SHA256 `70670db981681aa12c50f6a0ab7416e918d4cf9a9b902430aae1eb5fbd5f88a3`.
- Full public Debug download: 9,267,848 bytes, SHA256 `dacb42ac2f13ecd25df3f78ba3619a67e708ee6d41d941ccee2d267cb7c46f31`.
- Each downloaded manifest/channel marker matches beta.96 and contains Windows, iOS, Android32 and Android64 native binaries. Both immutable descriptors match the downloaded bytes.
- Pre-deploy Production: 47 players including bots (3 human accounts), 48 Solo matches / 53 attempts, 87 Survival matches / 11,620 attempts, 4 achievement unlocks / 4 AP ledger rows / 30 total AP. Achievement activation epoch remains `2026-10-24T04:59:47.102Z`; 0020 and 0029 are already applied.
- Pre-deploy active work: 0 Solo matches, 1 Survival match, 0 queued Survival players. Active work legitimately requires SQL; this observation is not an idle/suspension test. Neon metadata reports a 300-second suspend timeout; no setting was changed.
- This explicit activation commit requests the existing Singapore Lightsail deployment, with single-writer startup recovery and rollback protection. Package bytes were verified before activation; deployment completion and live checks are recorded separately after the workflow finishes.
