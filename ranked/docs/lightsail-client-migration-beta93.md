# Corum Ranked beta.93 full Lightsail release + Railway upgrade bridge

- Main production API: https://api.3-34-236-183.sslip.io
- Legacy host: https://corum-ranked-production.up.railway.app
- Client endpoint consumers: Ranked, Survival, Spectator, Debug Admin, Debug Spectate, and production endpoint regression test all point to Lightsail.
- Version is synchronized across `mod.json`, Node workspaces/lockfile and Nest exact-version admission gate to `v0.5.0-beta.93`.
- Railway legacy service is **read-only update bridge** matched by its Railway project/service ID. `GET /api/ranked/config` passes through public Lightsail update metadata; non-health, non-config requests return HTTP 426. No Nest app or Neon DB connection starts there.
- The `[deploy-lightsail]` commit marker triggers one-time production Lightsail deployment AND debug/release immutable R2 package publishing; ordinary pushes preserve their operator gates. Full success requires all workflows green.
- Server rollout remains at beta.92 until both beta.93 R2 packages are public. This is intended to let existing installed clients receive an update instead of getting locked out.
- Bridge must resolve publicly and return beta.93 package metadata after publication; existing auto-updater verifies version, SHA-256, length and package channel.
- Both immutable R2 packages (only usable once actually published):
  - Release: https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/release-v0.5.0-beta.93/Corum-Ranked-v0.5.0-beta.93-release.geode
  - Debug: https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/debug-v0.5.0-beta.93/Corum-Ranked-v0.5.0-beta.93-debug.geode
- Verify both legacy/new health/config URLs, `/ready`, published descriptors, and client install. Do not claim verified legacy update until the Railway 404 is replaced by the upgrade bridge.
