# DB idle/history beta.96 production rollout — 2026-10-24

The owner explicitly authorized merge, publication and deployment after reviewing PR #32. The validated implementation was merged at `8f18653e3d7d38a48c0b9c27601fb1b6f1497fc9` with automatic deployment and publication suppressed.

The release synchronizes mod, workspace/lockfile and admission metadata to `v0.5.0-beta.96` (previous `v0.5.0-beta.95`). Both new immutable Release/Debug packages must be publicly verified before a separate `[deploy-lightsail] [build only]` activation. Existing beta.95 objects must not be overwritten.

This release contains event-driven idle DB workers, authoritative round history, native LevelInfoLayer navigation, achievement/title presentation and Survival native return/re-entry fixes described in [the implementation report](db-idle-history-redesign.md). It adds no migrations and must not rerun the beta.95 achievement reset or change its activation epoch. Production Neon settings are unchanged.

Before activation, verify all native package builds, R2 publication and full package byte count/SHA256/manifest/platform contents. Activate only on Singapore using pinned SSH and the existing rollback workflow, then verify live health/ready/config and the Railway update-only bridge. The latter remains a proxy, never a second database writer.

The isolated virtual-time idle SQL measurement (5,784 → 0 in 360 seconds) does not establish production Neon suspension. Actual sleep/resume and physical-device UI/touch/native gameplay remain separate verification items. `/ready` and public config requests can themselves query PostgreSQL; avoid periodic readiness polling to measure idle SQL.
